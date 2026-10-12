#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "AckHandler.h"

#include <libtorrent/alert_types.hpp>
#include <libtorrent/entry.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <charconv>
#include "ChatConstants.h"
#include "DhtSignalingService.h"

AckHandler::AckHandler(std::shared_ptr<DhtPublisher> dhtPublisher)
    : m_dhtPublisher(std::move(dhtPublisher))
{
}

AckState AckHandler::parseAcks(const std::string &ackStr)
{
    AckState result;
    if (ackStr.empty())
        return result;

    std::stringstream ss(ackStr);
    std::string item;
    bool first = true;
    while (std::getline(ss, item, ','))
    {
        uint64_t val = 0;
        auto [ptr, ec] = std::from_chars(item.data(), item.data() + item.size(), val);
        if (ec == std::errc())
        {
            if (first)
            {
                result.through = val;
                first = false;
            }
            else
            {
                if (result.selective.size() < 1000) // bounded
                {
                    result.selective.push_back(val);
                }
            }
        }
    }
    std::sort(result.selective.begin(), result.selective.end());
    auto last = std::unique(result.selective.begin(), result.selective.end());
    result.selective.erase(last, result.selective.end());
    return result;
}

std::string AckHandler::compressAcks(const AckState &state)
{
    std::string result = std::to_string(state.through);
    for (uint64_t s : state.selective)
    {
        result += "," + std::to_string(s);
    }
    return result;
}

void AckHandler::pushAcks(const LobbyData &lobbyData)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager())
        return;

    std::string lobbyName = lobbyData.lobby_name;
    std::string key = lobbyData.private_node_discussion + lobbyData.my_x25519_pub + ACK_KEY;

    std::unique_lock<std::mutex> lock(m_cache->ackMutex);
    AckState &receivedSeqs = m_cache->receivedSeqsCache[lobbyName];

    // Seed/merge with downloaded_seq_nums which is always present even if history is omitted
    if (lobbyData.downloaded_seq_nums.through > receivedSeqs.through)
    {
        receivedSeqs.through = lobbyData.downloaded_seq_nums.through;
    }
    for (uint64_t s : lobbyData.downloaded_seq_nums.selective)
    {
        if (!receivedSeqs.acknowledges(s))
        {
            receivedSeqs.selective.push_back(s);
        }
    }

    // Also scan history if present
    for (auto it = lobbyData.history.messages.rbegin(); it != lobbyData.history.messages.rend(); ++it)
    {
        if (it->direction == MessageDirection::IN)
        {
            if (it->seq_num < 0 || static_cast<uint64_t>(it->seq_num) <= receivedSeqs.through)
                continue;
            
            if (!receivedSeqs.acknowledges(static_cast<uint64_t>(it->seq_num)))
            {
                receivedSeqs.selective.push_back(static_cast<uint64_t>(it->seq_num));
            }

            if (it->content.find(chat::RESYNC_PREFIX) == 0)
            {
                size_t start = chat::RESYNC_PREFIX.length();
                size_t end = it->content.find(" ", start);
                if (end != std::string::npos)
                {
                    uint64_t origSeq = 0;
                    auto [ptr, ec] = std::from_chars(it->content.data() + start, it->content.data() + end, origSeq);
                    if (ec == std::errc() && !receivedSeqs.acknowledges(origSeq))
                    {
                        receivedSeqs.selective.push_back(origSeq);
                    }
                }
            }
        }
    }

    receivedSeqs.compress();

    if (receivedSeqs.through == 0 && receivedSeqs.selective.empty())
    {
        SPDLOG_INFO("[AckHandler] Lobby {}: No received messages yet, nothing to push.", lobbyName);
        return;
    }

    std::string newCompressed = compressAcks(receivedSeqs);
    auto now = std::chrono::steady_clock::now();
    bool shouldPush = true;
    
    if (m_cache->lastPushedAckString[lobbyName] == newCompressed)
    {
        if (m_cache->lastAckPushTime.find(lobbyName) != m_cache->lastAckPushTime.end())
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_cache->lastAckPushTime[lobbyName]).count();
            if (elapsed < 60)
            {
                SPDLOG_INFO("[AckHandler] Lobby {}: ACKs unchanged ('{}'), skipping push.", lobbyName, newCompressed);
                shouldPush = false;
            }
        }
    }

    if (!shouldPush)
        return; // No change

    m_cache->lastAckPushTime[lobbyName] = now;
    SPDLOG_INFO("[AckHandler] Lobby {}: Preparing to push ACKs. New compressed ACKs: {}", lobbyName, newCompressed);
    
    // Unlock before network/callback operations
    lock.unlock();

    int offset = lobbyData.fake_timezone.offset_seconds;
    std::weak_ptr<DhtPublisher> weakPublisher = m_dhtPublisher;
    std::weak_ptr<CacheState> weakCache = m_cache;

    auto callback = [weakPublisher, weakCache, key, receivedSeqs, lobbyName, newCompressed, offset](const lt::dht_mutable_item_alert *get_mut)
    {
        std::string dhtStr;
        if (get_mut && get_mut->item.type() == lt::entry::string_t)
        {
            dhtStr = get_mut->item.string();
        }

        auto dhtPublisher = weakPublisher.lock();
        if (!dhtPublisher) return;

        dhtPublisher->enqueueNetworkTask(lobbyName, [dhtPublisher, weakCache, key, receivedSeqs, lobbyName, dhtStr, offset]() {
            auto cache = weakCache.lock();
            if (!cache) return;

            AckState existingSeqs;
            if (!dhtStr.empty())
            {
                SPDLOG_INFO("[AckHandler] Lobby {}: fetchDhtItem before push returned: '{}'", lobbyName, dhtStr);
                existingSeqs = parseAcks(dhtStr);
            }
            else
            {
                SPDLOG_INFO("[AckHandler] Lobby {}: fetchDhtItem before push returned no existing item.", lobbyName);
            }

            existingSeqs.through = std::max(existingSeqs.through, receivedSeqs.through);
            existingSeqs.selective.insert(existingSeqs.selective.end(), receivedSeqs.selective.begin(), receivedSeqs.selective.end());
            existingSeqs.compress();

            std::string finalCompressed = compressAcks(existingSeqs);
            SPDLOG_INFO("[AckHandler] Lobby {}: Merged received sequences into final compressed ACK string: '{}'", lobbyName, finalCompressed);

            DhtSignalingService::dropAckMessage(dhtPublisher.get(), finalCompressed, lobbyName, key, offset);
            {
                std::lock_guard<std::mutex> cb_lock(cache->ackMutex);
                cache->lastPushedAckString[lobbyName] = finalCompressed;
            }
            SPDLOG_INFO("[AckHandler] Lobby {}: Pushed ACK string to DHT: {}", lobbyName, finalCompressed);
        });
    };

    m_dhtPublisher->getDhtManager()->fetchDhtItem(key, lobbyName, callback, false, -1);
}

void AckHandler::fetchPeerAcks(const LobbyData &lobbyData, std::shared_ptr<LobbyStore> lobbyStore)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager() || !lobbyStore)
        return;

    std::string lobbyName = lobbyData.lobby_name;
    std::string peerAckKey = lobbyData.private_node_discussion + lobbyData.announced_x25519_pub + ACK_KEY;

    std::weak_ptr<DhtPublisher> weakPublisher = m_dhtPublisher;
    std::weak_ptr<CacheState> weakCache = m_cache;
    auto callback = [lobbyName, lobbyStore, weakPublisher, weakCache](const lt::dht_mutable_item_alert *get_mut)
    {
        std::string valueStr;
        if (get_mut && get_mut->item.type() == lt::entry::string_t)
            valueStr = get_mut->item.string();

        if (auto pub = weakPublisher.lock())
        {
            pub->enqueueNetworkTask(lobbyName, [lobbyName, lobbyStore, valueStr, weakCache]() {
                auto cache = weakCache.lock();
                if (!cache) return;

                if (valueStr.empty() || valueStr == "uninitialized")
                {
                    SPDLOG_INFO("[AckHandler] Lobby {}: Peer ACKs on DHT are empty or uninitialized.", lobbyName);
                    return;
                }

                SPDLOG_INFO("[AckHandler] Lobby {}: Fetched raw peer ACK string from DHT: '{}'", lobbyName, valueStr);

                {
                    std::lock_guard<std::mutex> cb_lock(cache->ackMutex);
                    if (cache->lastFetchedPeerAckString[lobbyName] == valueStr)
                    {
                        SPDLOG_INFO("[AckHandler] Lobby {}: Fetched peer ACK string '{}' is same as last fetched, skipping.", lobbyName, valueStr);
                        return; // No new ACKs, avoid unnecessary disk writes
                    }
                }

                try
                {
                    AckState ackedSeqs = parseAcks(valueStr);
                    if (ackedSeqs.through > 0 || !ackedSeqs.selective.empty())
                    {
                        auto obs = lobbyStore->getLobby(lobbyName);
                        if (obs)
                        {
                            AckState currentAcks = obs->getLobbyDataWithoutHistory().peer_acked_seq_nums;
                            AckState merged = currentAcks;
                            merged.through = std::max(merged.through, ackedSeqs.through);
                            
                            merged.selective.insert(merged.selective.end(), ackedSeqs.selective.begin(), ackedSeqs.selective.end());
                            merged.compress();

                            if (!(merged == currentAcks))
                            {
                                {
                                    std::lock_guard<std::mutex> cb_lock(cache->ackMutex);
                                    cache->lastFetchedPeerAckString[lobbyName] = valueStr;
                                }
                                
                                nlohmann::json patch;
                                patch["peer_acked_seq_nums"] = merged;
                                lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
                                SPDLOG_INFO("[AckHandler] Lobby {}: Received NEW peer ACKs from DHT: {}", lobbyName, valueStr);
                            }
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    SPDLOG_ERROR("[AckHandler] Lobby {}: Failed to parse peer ACKs: '{}'. Exception: {}", lobbyName, valueStr, e.what());
                }
            });
        }
    };

    m_dhtPublisher->getDhtManager()->fetchDhtItem(peerAckKey, lobbyName, callback, false, -1);
}
