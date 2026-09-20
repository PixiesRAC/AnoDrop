#include "core/DhtPublisher.h"
#include "chat/DhtChatUtils.h"
#include "crypto/HandshakeKeyService.h"
#include "crypto/HandshakePayloadCodec.h"
#include "lobby/HandshakeStateEngine.h"
#include "crypto/Hasher.h"
#include "network/DhtManager.h"
#include "network/Dropper.h"

#include <libtorrent/hasher.hpp>
#include <libtorrent/alert_types.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

void DhtPublisher::setDhtManager(std::unique_ptr<DhtManager> dhtManager)
{
    m_dhtManager = std::move(dhtManager);
}

void DhtPublisher::enqueueChatMessage(const struct ChatPublishParams &params)
{
    m_networkWorker.enqueue([this, params]() {
        this->publishChatMessage(params);
    });
}

HandshakeKeyService::ChannelKeys DhtPublisher::deriveChannelKeys(const std::string &passphrase)
{
    std::vector<unsigned char> seed = Hasher::getInstance().getHashMsg256(passphrase);

    // Generate salt to correctly compute DHT target ID
    auto argon2Bytes = Hasher::getInstance().generateArgon2Salt(passphrase);
    std::string salt(argon2Bytes.begin(), argon2Bytes.end());

    HandshakeKeyService::ChannelKeys info(seed, salt);

    SPDLOG_INFO("Public Key (Hex): {}", info.pubKeyHex);
    SPDLOG_INFO("Target ID (SHA-1): {}", info.targetIDHex);

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

    std::vector<unsigned char> seed = Hasher::getInstance().getHashMsg256(lobbyName);
    HandshakeKeyService::ChannelKeys info(seed);

    Dropper dropper(m_dhtManager->getSession(), info.publicKey, info.privateKey);
    dropper.dropHandShakeMessage(encryptedMessage, lobbyName, m_dhtManager->getLobbyStore());
}

void DhtPublisher::waitForPutConfirmed(const std::string &lobbyName)
{
    if (m_dhtManager)
    {
        m_dhtManager->waitForPutConfirmed(lobbyName);
    }
}

void DhtPublisher::dispatchAnomalyMessage(const std::string &lobbyName, const std::string &message)
{
    if (!m_dhtManager)
    {
        return;
    }

    m_networkWorker.enqueue([this, lobbyName, message]() {
        HandshakeKeyService::ChannelKeys keys = deriveChannelKeys(lobbyName);

        Dropper dropper(m_dhtManager->getSession(), keys.publicKey, keys.privateKey);
        dropper.dropAnomalyMessage(message, lobbyName);
    });
}

void DhtPublisher::checkLobbyAnomaly(const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore)
{
    if (!m_dhtManager || !lobbyStore) return;

    auto callback = [lobbyName, lobbyStore](const lt::dht_mutable_item_alert* get_mut) {
        if (get_mut->seq == INT64_MAX)
        {
            std::string valueStr;
            if (get_mut->item.type() == lt::entry::string_t)
                valueStr = get_mut->item.string();
            else
                valueStr = get_mut->item.to_string();

            auto existingDecryptedPayload = HandshakePayloadCodec::decryptDhtHandShakePayload(valueStr, lobbyName);
            if (existingDecryptedPayload && existingDecryptedPayload->contains("anomaly") && (*existingDecryptedPayload)["anomaly"] == true)
            {
                std::string anomalyMsg = (*existingDecryptedPayload).value("message", "Unknown anomaly");
                SPDLOG_WARN("Anomaly detected for lobby {}: {}", lobbyName, anomalyMsg);
                
                nlohmann::json patch;
                patch["status"] = LobbyStatus::ANOMALY;
                patch["anomaly_message"] = anomalyMsg;
                lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
            }
        }
    };

    m_dhtManager->fetchDhtItem(lobbyName, lobbyName, callback, false, -1);
}

void DhtPublisher::publishChatMessage(const ChatPublishParams &params)
{
    if (!m_dhtManager)
    {
        SPDLOG_ERROR("DhtPublisher::publishChatMessage failed: DHT Manager is not initialized");
        return;
    }

    // 1. Update sequence target key with the new seq num
    std::vector<unsigned char> seqSeed = Hasher::getInstance().getHashMsg256(params.sequenceTargetKey);
    HandshakeKeyService::ChannelKeys seqInfo(seqSeed);
    Dropper seqDropper(m_dhtManager->getSession(), seqInfo.publicKey, seqInfo.privateKey);
    if (seqDropper.dropChatMessage(params, true))
    {
        // 2. Drop the actual message to the message target key
        std::vector<unsigned char> msgSeed = Hasher::getInstance().getHashMsg256(params.messageTargetKey);
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

    std::string lobbyName = lobbyObs->getLobbyData().lobby_name;
    auto lobbyStore = m_dhtManager->getLobbyStore();

    auto callback = [this, lobbyObs, lobbyStore, isSeqNum, seqNumTarget](const lt::dht_mutable_item_alert* get_mut) {
        this->handleDhtItemCallback(get_mut, lobbyObs, lobbyStore, isSeqNum, seqNumTarget);
    };

    m_dhtManager->fetchDhtItem(rawKeyString, lobbyName, callback, isSeqNum, seqNumTarget);
}

void DhtPublisher::handleDhtItemCallback(const lt::dht_mutable_item_alert* get_mut, std::shared_ptr<LobbyObserver> lobbyObs, std::shared_ptr<LobbyStore> lobbyStore, bool isSeqNum, std::int64_t seqNumTarget)
{
    std::string valueStr;
    bool isUndefined = (get_mut->item.type() == lt::entry::undefined_t);
    if (!isUndefined)
    {
        if (get_mut->item.type() == lt::entry::string_t)
        {
            valueStr = get_mut->item.string();
        }
        else
        {
            valueStr = get_mut->item.to_string();
        }
    }

    LobbyData lobbyData = lobbyObs->getLobbyData();
    
    if (lobbyData.is_hidden)
    {
        SPDLOG_INFO("Ignoring DHT callback because lobby '{}' is hidden.", lobbyData.lobby_name);
        if (isSeqNum)
        {
            lobbyObs->clearFetchingSeq();
        }
        return;
    }

    if (isSeqNum)
    {
        if (!isUndefined)
        {
            if (lobbyData.announced_seq_num < get_mut->seq)
            {
                nlohmann::json patch;
                patch["announced_seq_num"] = get_mut->seq;
                if (lobbyStore)
                {
                    lobbyStore->asyncUpdateLobbyData(lobbyData.lobby_name, patch);
                }
                else
                {
                    lobbyObs->onUpdate(patch);
                }
                SPDLOG_INFO("🔄 Discovered new messages for lobby '{}'. Target seq: {}", lobbyData.lobby_name, get_mut->seq);
            }

            // Check for any missing messages up to the announced sequence number
            int fetchCount = 0;
            for (std::int64_t i = 1; i <= get_mut->seq && fetchCount < m_maxConcurrentFetches; ++i)
            {
                bool haveMessage = false;
                for (const auto& msg : lobbyData.history.messages)
                {
                    if (msg.direction == MessageDirection::IN && msg.seq_num == i)
                    {
                        haveMessage = true;
                        break;
                    }
                }

                if (!haveMessage)
                {
                    ++fetchCount;
                    std::string targetKeySeq = lobbyData.private_node_discussion + lobbyData.announced_x25519_pub + std::format("{}", i);
                    this->fetchDhtItem(targetKeySeq, lobbyObs, false, i);
                }
            }
            if (fetchCount == m_maxConcurrentFetches)
            {
                SPDLOG_WARN("Resynchronization truncated at {} messages to prevent flooding. Run fetch again later.", m_maxConcurrentFetches);
            }
        }
        lobbyObs->clearFetchingSeq();
    }
    else
    {
        if (isUndefined || valueStr.empty())
        {
            SPDLOG_WARN("Received empty or undefined message payload for lobby '{}' (seq {}). Ignoring.", lobbyData.lobby_name, seqNumTarget);
            return;
        }
        SPDLOG_INFO("Received chat message payload from DHT!");
        
        ChatMessage chatMsg;
        try
        {
            nlohmann::json msgJson = nlohmann::json::parse(valueStr);
            chatMsg = msgJson.get<ChatMessage>();
            chatMsg.direction = MessageDirection::IN;
        }
        catch (const std::exception &e)
        {
            SPDLOG_WARN("Failed to parse incoming ChatMessage JSON: {}. Ignored.", e.what());
            return; // Fallback removed for security
        }

        chatMsg.seq_num = get_mut->seq;

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
}
