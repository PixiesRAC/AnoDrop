#include "LogEvents.h"
#include "HeartbeatHandler.h"

#include <charconv>
#include <spdlog/spdlog.h>

#include "SecureBuffer.h"
#include "TimeUtils.h"

HeartbeatHandler::HeartbeatHandler(std::shared_ptr<DhtPublisher> dhtPublisher) : m_dhtPublisher(std::move(dhtPublisher))
{
}

void HeartbeatHandler::dispatchHeartbeatMessage(const LobbyData &lobbyData)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager() || !lobbyData.crypto || lobbyData.crypto->private_node_discussion.empty())
    {
        return;
    }

    int offset = lobbyData.metadata.fake_timezone.offset_seconds;
    std::string lobby = lobbyData.metadata.lobby_name;
    SecureString key(lobbyData.crypto->private_node_discussion + lobbyData.crypto->my_x25519_pub + HEARTBEAT_KEY);

    m_dhtPublisher->enqueueNetworkTask(lobby, [pub = m_dhtPublisher, offset, lobby, key = std::move(key)]()
                                       {
        if (pub && pub->getDhtManager())
            pub->getDhtManager()->publishHeartbeat(offset, lobby, key); });
}

bool HeartbeatHandler::checkLobbyHeartbeat(const LobbyData &lobbyData, std::shared_ptr<LobbyStore> lobbyStore)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager() || !lobbyStore || !lobbyData.crypto || lobbyData.crypto->private_node_discussion.empty())
        return false;

    std::string lobbyName = lobbyData.metadata.lobby_name;
    std::string peerHeartbeatKey = lobbyData.crypto->private_node_discussion + lobbyData.crypto->announced_x25519_pub + HEARTBEAT_KEY;
    ScopedCleanse keyGuard(peerHeartbeatKey);

    std::weak_ptr<DhtPublisher> weakPublisher = m_dhtPublisher;
    auto callback = [lobbyName, lobbyStore, weakPublisher](DhtFetchResult result)
    {
        if (result.status == DhtFetchStatus::Timeout || result.status == DhtFetchStatus::Cancelled)
            return;
        if (!result.found() || result.value.empty())
            return;
        auto valueStr = std::move(result.value);

        if (auto pub = weakPublisher.lock())
        {
            pub->enqueueNetworkTask(lobbyName, [lobbyName, lobbyStore, valueStr = std::move(valueStr), weakPublisher]()
                                    {
                try
                {
                    std::int64_t peerTimestamp = 0;
                    auto [ptr, ec] = std::from_chars(valueStr.data(), valueStr.data() + valueStr.size(), peerTimestamp);
                    if (ec != std::errc() || ptr != valueStr.data() + valueStr.size())
                    {
                        throw std::invalid_argument("Malformed heartbeat timestamp");
                    }

                    auto lobbyObs = lobbyStore->getLobby(lobbyName);
                    if (!lobbyObs)
                        return;
                    const auto metadata = lobbyObs->getMetadata();
                    const std::int64_t currentFakeTime = TimeUtils::computeFakeTimestamp(metadata.fake_timezone.offset_seconds);

                    auto valResult = TimeUtils::validatePeerTimestamp(peerTimestamp, currentFakeTime, metadata.last_heartbeat);
                    if (valResult == TimestampValidationResult::NonMonotonic)
                    {
                        // Same or older heartbeat already processed from DHT, ignore silently
                        return;
                    }
                    if (valResult != TimestampValidationResult::Valid)
                    {
                        std::string anomalyMsg = "Invalid heartbeat timestamp received from peer";
                        SPDLOG_WARN("[PRESENCE] lobby={} | Invalid heartbeat timestamp ({}) received (result: {}). Setting to ANOMALY.",
                                    nlohmann::json(lobbyName).dump(), peerTimestamp, static_cast<int>(valResult));

                        lobbyStore->execute(lobbyName, lobby::FlagAnomaly{anomalyMsg});
                        if (auto publisher = weakPublisher.lock())
                        {
                            if (auto *mgr = publisher->getDhtManager())
                                mgr->publishAnomaly(lobbyName, anomalyMsg);
                        }
                        return;
                    }

                    if (currentFakeTime - peerTimestamp > HEARTBEAT_TIMEOUT_SEC)
                    {
                        SPDLOG_WARN("[PRESENCE] lobby={} | Peer is disconnected (Heartbeat timeout)", nlohmann::json(lobbyName).dump());
                    }
                    else
                    {
                        SPDLOG_INFO("[PRESENCE] lobby={} | Received heartbeat, peer is connected", nlohmann::json(lobbyName).dump());
                        lobbyStore->execute(lobbyName, lobby::RecordHeartbeat{peerTimestamp});
                    }
                }
                catch (const std::exception &e)
                {
                    SPDLOG_ERROR("[PRESENCE] lobby={} | Invalid heartbeat rejected", nlohmann::json(lobbyName).dump());
                } });
        }
    };

    return m_dhtPublisher->getDhtManager()->fetchDhtItem(peerHeartbeatKey, lobbyName, callback, {DhtFetchKind::Signaling, -1, "heartbeat"});
}
