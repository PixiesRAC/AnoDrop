#include "DhtPublisher.h"

#include <libtorrent/alert_types.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "LobbyDataSerializer.h"
#include "CryptoUtils.h"
#include "Dropper.h"
#include "HandshakeStateEngine.h"
#include "DhtSignalingService.h"
#include "DhtChatUtils.h"
#include "DhtMessageCodec.h"
#include "TimeUtils.h"

DhtPublisher::~DhtPublisher()
{
    shutdown();
}

void DhtPublisher::shutdown()
{
    // Shut down the thread pool FIRST. This cancels pending tasks and waits for
    // running tasks to finish. It prevents Use-After-Free of any captured
    // references (e.g. from LobbyFetcher handlers).
    m_networkWorker.shutdown();

    // Explicitly shut down alert thread and callbacks in DhtManager
    if (m_dhtManager)
    {
        m_dhtManager->shutdown();
    }

    // Explicitly destroy the DhtManager (and its alert thread) AFTER
    // so no active network tasks try to access a destroyed libtorrent session.
    m_dhtManager.reset();
}

void DhtPublisher::setDhtManager(std::unique_ptr<DhtManager> dhtManager)
{
    m_dhtManager = std::move(dhtManager);
}

void DhtPublisher::enqueueChatMessage(const struct ChatPublishParams &params)
{
    (void)m_networkWorker.enqueue(params.lobbyName, [this, params]()
                                  { this->publishChatMessage(params); });
}

HandshakeKeyService::ChannelKeys DhtPublisher::deriveChannelKeys(const std::string &passphrase)
{
    std::vector<unsigned char> seed = network::DhtMessageCodec::deriveSeed(passphrase);
    if (seed.empty())
    {
        SPDLOG_ERROR("Failed to derive seed");
        throw std::runtime_error("Failed to derive seed");
    }

    // Use an empty salt to prevent channel correlation (S3)
    std::string salt = "";

    HandshakeKeyService::ChannelKeys info(seed, salt);

    SPDLOG_DEBUG("Public Key (Hex): {}", info.pubKeyHex);
    SPDLOG_DEBUG("Target ID (SHA-1): {}", info.targetIDHex);

    return info;
}

void DhtPublisher::dispatchHandshakeMessage(const std::string &lobbyName, const std::string &pseudo)
{
    if (!m_dhtManager)
    {
        throw std::runtime_error("DHT Manager is not initialized");
    }
    if (!m_dhtManager || !m_dhtManager->getLobbyStore())
    {
        throw std::runtime_error("Lobby Store is not initialized");
    }

    auto ciphertextOpt = HandshakeStateEngine::prepareLobbyPayload(m_dhtManager->getLobbyStore(), lobbyName, pseudo);
    if (!ciphertextOpt)
    {
        SPDLOG_WARN("Skipping handshake dispatch: could not prepare payload for lobby {}", lobbyName);
        return;
    }
    const auto &ciphertext = *ciphertextOpt;

    std::string encryptedMessage(ciphertext.begin(), ciphertext.end());

    std::vector<unsigned char> seed = network::DhtMessageCodec::deriveSeed(lobbyName);
    if (seed.empty())
    {
        SPDLOG_ERROR("Failed to derive seed");
        throw std::runtime_error("Failed to derive seed");
    }
    HandshakeKeyService::ChannelKeys info(seed);

    Dropper dropper(m_dhtManager->getSession(), info.publicKey, info.privateKey);
    dropper.dropHandShakeMessage(encryptedMessage, lobbyName, m_dhtManager->getLobbyStore(), m_dhtManager.get());
}


void DhtPublisher::publishChatMessage(const ChatPublishParams &params)
{
    if (!m_dhtManager)
    {
        SPDLOG_ERROR("DhtPublisher::publishChatMessage failed: DHT Manager is not initialized");
        return;
    }

    // 1. Update sequence target key with the new seq num
    std::vector<unsigned char> seqSeed = network::DhtMessageCodec::deriveSeed(params.sequenceTargetKey);
    if (seqSeed.empty())
    {
        SPDLOG_ERROR("Failed to derive seed");
        throw std::runtime_error("Failed to derive seed");
    }
    HandshakeKeyService::ChannelKeys seqInfo(seqSeed);
    Dropper seqDropper(m_dhtManager->getSession(), seqInfo.publicKey, seqInfo.privateKey);
    if (seqDropper.dropChatMessage(params, true))
    {
        // 2. Drop the actual message to the message target key
        std::vector<unsigned char> msgSeed = network::DhtMessageCodec::deriveSeed(params.messageTargetKey);
        if (msgSeed.empty())
        {
            SPDLOG_ERROR("Failed to derive seed");
            throw std::runtime_error("Failed to derive seed");
        }
        HandshakeKeyService::ChannelKeys msgInfo(msgSeed);
        Dropper msgDropper(m_dhtManager->getSession(), msgInfo.publicKey, msgInfo.privateKey);
        msgDropper.dropChatMessage(params, false, m_dhtManager.get());
    }
}

void DhtPublisher::fetchDhtItem(const std::string &rawKeyString, std::shared_ptr<LobbyObserver> lobbyObs, bool isSeqNum, std::int64_t seqNumTarget)
{
    if (!m_dhtManager || !lobbyObs)
    {
        return;
    }

    std::string lobbyName = lobbyObs->getLobbyName();
    auto lobbyStore = m_dhtManager->getLobbyStore();

    auto callback = [this, lobbyObs, lobbyStore, isSeqNum, seqNumTarget, lobbyName](const lt::dht_mutable_item_alert *get_mut)
    {
        std::string valueStr;
        bool isUndefined = (get_mut->item.type() == lt::entry::undefined_t);
        if (!isUndefined)
        {
            if (get_mut->item.type() == lt::entry::string_t)
                valueStr = get_mut->item.string();
            else
                valueStr = get_mut->item.to_string();
        }

        std::int64_t dhtSeq = get_mut->seq;
        this->enqueueNetworkTask(lobbyName, [this, lobbyObs, lobbyStore, isSeqNum, seqNumTarget, isUndefined, valueStr, dhtSeq]()
                                 { this->handleDhtItemCallback(lobbyObs, lobbyStore, isSeqNum, seqNumTarget, isUndefined, valueStr, dhtSeq); });
    };

    m_dhtManager->fetchDhtItem(rawKeyString, lobbyName, callback, isSeqNum, seqNumTarget);
}

void DhtPublisher::handleDhtItemCallback(std::shared_ptr<LobbyObserver> lobbyObs, std::shared_ptr<LobbyStore> lobbyStore, bool isSeqNum, std::int64_t seqNumTarget, bool isUndefined, const std::string &valueStr, std::int64_t dhtSeq)
{
    if (lobbyObs->getMetadata().is_hidden)
    {
        SPDLOG_INFO("Ignoring DHT callback because lobby '{}' is hidden.", lobbyObs->getLobbyName());
        if (isSeqNum)
        {
            lobbyObs->clearFetchingSeq();
        }
        return;
    }

    LobbyData lobbyData = lobbyObs->getLobbyDataWithoutHistory();

    if (isSeqNum)
    {
        processSeqNumCallback(dhtSeq, lobbyData, lobbyObs, lobbyStore, isUndefined);
    }
    else
    {
        processMessagePayloadCallback(lobbyData, lobbyObs, lobbyStore, isUndefined, valueStr, seqNumTarget);
    }
}

void DhtPublisher::processSeqNumCallback(std::int64_t dhtSeq, const LobbyData &lobbyData, std::shared_ptr<LobbyObserver> lobbyObs, std::shared_ptr<LobbyStore> lobbyStore, bool isUndefined)
{
    if (!isUndefined)
    {
        if (lobbyData.announced_seq_num < dhtSeq)
        {
            nlohmann::json patch;
            patch["announced_seq_num"] = dhtSeq;
            if (lobbyStore)
            {
                lobbyStore->asyncUpdateLobbyData(lobbyData.lobby_name, patch);
            }
            else
            {
                lobbyObs->onUpdate(patch);
            }
            SPDLOG_INFO("🔄 Discovered new messages for lobby '{}'. Target seq: {}", lobbyData.lobby_name, dhtSeq);
        }

        // Check for any missing messages up to the announced sequence number
        this->fetchMissingMessages(dhtSeq, lobbyData, lobbyObs);
    }
    lobbyObs->clearFetchingSeq();
}

void DhtPublisher::processMessagePayloadCallback(const LobbyData &lobbyData, std::shared_ptr<LobbyObserver> lobbyObs, std::shared_ptr<LobbyStore> lobbyStore, bool isUndefined, const std::string &valueStr, std::int64_t seqNumTarget)
{
    if (isUndefined || valueStr.empty())
    {
        SPDLOG_WARN("Received empty or undefined message payload for lobby '{}' (seq {}). Ignoring.", lobbyData.lobby_name, seqNumTarget);
        return;
    }
    SPDLOG_INFO("Received chat message payload from DHT!");

    auto parsedMsg = network::DhtMessageCodec::parseChatMessage(valueStr, seqNumTarget);
    if (!parsedMsg)
    {
        SPDLOG_WARN("{}. Ignored.", parsedMsg.error());
        return;
    }
    ChatMessage chatMsg = *parsedMsg;

    // Check for already downloaded seq_num
    if (lobbyData.downloaded_seq_nums.acknowledges(static_cast<std::uint64_t>(seqNumTarget)))
    {
        SPDLOG_DEBUG("Message seq {} already downloaded in lobby '{}'. Ignoring duplicate.", seqNumTarget, lobbyData.lobby_name);
        return;
    }

    // Validate fake_timestamp from peer using DhtMessageCodec: check for corruption/invalid time
    if (!network::DhtMessageCodec::isTimestampValid(chatMsg.fake_timestamp))
    {
        std::string anomalyMsg = "Corrupted timestamp received from peer";
        SPDLOG_WARN("Corrupted/extreme fake_timestamp ({}) received in lobby '{}' for seq {}. Setting to ANOMALY.",
                    chatMsg.fake_timestamp, lobbyData.lobby_name, seqNumTarget);

        DhtSignalingService::signalAndPersistAnomaly(this, lobbyData.lobby_name, anomalyMsg, lobbyStore, lobbyObs);
        return;
    }

    nlohmann::json patch;
    patch["new_message"] = chatMsg;

    if (lobbyStore)
    {
        lobbyStore->asyncUpdateLobbyData(lobbyData.lobby_name, patch);
    }
    else
    {
        lobbyObs->onUpdate(patch);
    }
}

void DhtPublisher::fetchMissingMessages(std::int64_t maxSeq, const LobbyData &lobbyData, std::shared_ptr<LobbyObserver> lobbyObs)
{
    int fetchCount = 0;
    auto now = std::chrono::steady_clock::now();
    const std::int64_t throughSeq = static_cast<std::int64_t>(lobbyData.downloaded_seq_nums.through);

    // Stop early at throughSeq: all sequences <= throughSeq are already contiguous and downloaded.
    for (std::int64_t i = maxSeq; i > throughSeq && fetchCount < m_maxConcurrentFetches; --i)
    {
        if (!lobbyData.downloaded_seq_nums.acknowledges(i))
        {
            bool shouldFetch = false;
            {
                std::lock_guard<std::mutex> lock(m_fetchAttemptsMutex);
                auto &attempts = m_missingFetchAttempts[lobbyData.lobby_name];
                auto it = attempts.find(i);
                if (it == attempts.end())
                {
                    shouldFetch = true;
                    attempts[i] = {now, 1};
                }
                else
                {
                    // Exponential backoff: 10s * 2^(attempt-1), capped at 42 seconds max.
                    constexpr int MAX_BACKOFF_SECONDS = 42;
                    int exponent = std::min(it->second.attemptCount - 1, 5);
                    int backoffSeconds = std::min(10 * (1 << exponent), MAX_BACKOFF_SECONDS);
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastAttempt).count();
                    if (elapsed >= backoffSeconds)
                    {
                        shouldFetch = true;
                        it->second.lastAttempt = now;
                        it->second.attemptCount++;
                    }
                }
            }

            if (shouldFetch)
            {
                ++fetchCount;
                SPDLOG_INFO("fetchMissingMessages: Requesting missing message Seq {} (Lobby {})", i, lobbyData.lobby_name);
                std::string targetKeySeq = lobbyData.private_node_discussion + lobbyData.announced_x25519_pub + std::format("{}", i);
                this->fetchDhtItem(targetKeySeq, lobbyObs, false, i);
            }
        }
        else
        {
            // Cleanup: if the message is present, ensure we don't track it anymore
            std::lock_guard<std::mutex> lock(m_fetchAttemptsMutex);
            m_missingFetchAttempts[lobbyData.lobby_name].erase(i);
        }
    }

    // Cleanup: purge any attempts for sequences that are now fully downloaded (<= throughSeq)
    {
        std::lock_guard<std::mutex> lock(m_fetchAttemptsMutex);
        auto itAttempts = m_missingFetchAttempts.find(lobbyData.lobby_name);
        if (itAttempts != m_missingFetchAttempts.end())
        {
            std::erase_if(itAttempts->second, [throughSeq](const auto &pair) {
                return pair.first <= throughSeq;
            });
        }
    }

    if (fetchCount == m_maxConcurrentFetches)
    {
        SPDLOG_WARN("Resynchronization truncated at {} messages to prevent flooding. Run fetch again later.", m_maxConcurrentFetches);
    }
}
