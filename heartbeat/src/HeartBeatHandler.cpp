#include "HeartbeatHandler.h"

#include <libtorrent/alert_types.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "DhtSignalingService.h"
#include "LobbyDataSerializer.h"
#include "TimeUtils.h"

HeartbeatHandler::HeartbeatHandler(std::shared_ptr<DhtPublisher> dhtPublisher) : m_dhtPublisher(std::move(dhtPublisher))
{
}

void HeartbeatHandler::dispatchHeartbeatMessage(const LobbyData &lobbyData)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager())
    {
        return;
    }

    int offset = lobbyData.fake_timezone.offset_seconds;
    std::string lobby = lobbyData.lobby_name;
    std::string key = lobbyData.private_node_discussion + lobbyData.my_x25519_pub + HEARTBEAT_KEY;

    m_dhtPublisher->enqueueNetworkTask(lobby, [pub = m_dhtPublisher, offset, lobby, key]()
                                       { if (pub) DhtSignalingService::dropHeartbeatMessage(pub.get(), offset, lobby, key); });
}

void HeartbeatHandler::checkLobbyHeartbeat(const LobbyData &lobbyData, std::shared_ptr<LobbyStore> lobbyStore)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager() || !lobbyStore)
        return;

    std::string lobbyName = lobbyData.lobby_name;
    std::string peerHeartbeatKey = lobbyData.private_node_discussion + lobbyData.announced_x25519_pub + HEARTBEAT_KEY;
    int offset = lobbyData.fake_timezone.offset_seconds;

    std::weak_ptr<DhtPublisher> weakPublisher = m_dhtPublisher;
    auto callback = [lobbyName, lobbyStore, offset, weakPublisher](const lt::dht_mutable_item_alert *get_mut)
    {
        std::string valueStr;
        if (get_mut->item.type() == lt::entry::string_t)
            valueStr = get_mut->item.string();
        else
            valueStr = get_mut->item.to_string();

        if (auto pub = weakPublisher.lock())
        {
            pub->enqueueNetworkTask(lobbyName, [lobbyName, lobbyStore, offset, valueStr, weakPublisher]()
                                    {
                try
                {
                    if (valueStr.empty() || valueStr == "<uninitialized>")
                    {
                        // Item not found or empty
                        return;
                    }

                    size_t pos;
                    int64_t peerTimestamp = std::stoll(valueStr, &pos);
                    if (pos != valueStr.length())
                    {
                        throw std::invalid_argument("Trailing characters in heartbeat");
                    }

                    int64_t currentFakeTime = TimeUtils::computeFakeTimestamp(offset);

                    auto lobbyObs = lobbyStore->getLobby(lobbyName);
                    if (!lobbyObs) return;
                    auto currentLobbyData = lobbyObs->getLobbyDataWithoutHistory();

                    auto valResult = TimeUtils::validatePeerTimestamp(peerTimestamp, currentFakeTime, currentLobbyData.last_heartbeat);
                    if (valResult == TimestampValidationResult::NonMonotonic)
                    {
                        // Same or older heartbeat already processed from DHT, ignore silently
                        return;
                    }
                    if (valResult != TimestampValidationResult::Valid)
                    {
                        std::string anomalyMsg = "Invalid heartbeat timestamp received from peer";
                        SPDLOG_WARN("Lobby {}: Invalid heartbeat timestamp ({}) received (result: {}). Setting to ANOMALY.",
                                    lobbyName, peerTimestamp, static_cast<int>(valResult));

                        if (auto publisher = weakPublisher.lock())
                        {
                            DhtSignalingService::signalAndPersistAnomaly(publisher.get(), lobbyName, anomalyMsg, lobbyStore, lobbyObs);
                        }
                        return;
                    }

                    if (currentFakeTime - peerTimestamp > HEARTBEAT_TIMEOUT_SEC)
                    {
                        SPDLOG_WARN("Lobby {}: Peer is disconnected (Heartbeat timeout)", lobbyName);
                    }
                    else
                    {
                        SPDLOG_INFO("Lobby {}: Received heartbeat, peer is connected", lobbyName);
                        nlohmann::json patch;
                        patch["last_heartbeat"] = peerTimestamp;
                        lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
                    }
                }
                catch (const std::exception &e)
                {
                    SPDLOG_ERROR("Lobby {}: Failed to parse heartbeat value: '{}'. Exception: {}", lobbyName, valueStr, e.what());
                } });
        }
    };

    m_dhtPublisher->getDhtManager()->fetchDhtItem(peerHeartbeatKey, lobbyName, callback, false, -1);
}
