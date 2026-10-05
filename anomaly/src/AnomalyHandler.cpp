#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "AnomalyHandler.h"

#include <libtorrent/alert_types.hpp>
#include <spdlog/spdlog.h>

#include "HandshakePayloadCodec.h"

AnomalyHandler::AnomalyHandler(std::shared_ptr<DhtPublisher> dhtPublisher)
    : m_dhtPublisher(std::move(dhtPublisher))
{
}

void AnomalyHandler::dispatchAnomalyMessage(const std::string &lobbyName, const std::string &message)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager())
    {
        return;
    }

    auto publisher = m_dhtPublisher;
    publisher->enqueueNetworkTask(lobbyName, [publisher, lobbyName, message]()
                                  { publisher->dropAnomalyMessage(message, lobbyName); });
}

void AnomalyHandler::checkLobbyAnomaly(const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager() || !lobbyStore)
        return;

    // We use a weak_ptr here to avoid keeping the DhtPublisher alive unnecessarily and 
    // to prevent crashes if the callback executes after the DhtPublisher is destroyed.
    std::weak_ptr<DhtPublisher> weakPublisher = m_dhtPublisher;
    auto callback = [lobbyName, lobbyStore, weakPublisher](const lt::dht_mutable_item_alert *get_mut)
    {
        if (get_mut->seq == INT64_MAX)
        {
            std::string valueStr;
            if (get_mut->item.type() == lt::entry::string_t)
                valueStr = get_mut->item.string();
            else
                valueStr = get_mut->item.to_string();

            if (auto pub = weakPublisher.lock())
            {
                pub->enqueueNetworkTask(lobbyName, [lobbyName, lobbyStore, valueStr]()
                                        {
                    auto existingDecryptedPayload = HandshakePayloadCodec::decryptDhtHandShakePayload(valueStr, lobbyName);
                    if (existingDecryptedPayload && existingDecryptedPayload->contains(ANOMALY_KEY) && (*existingDecryptedPayload)[ANOMALY_KEY] == true)
                    {
                        std::string anomalyMsg = (*existingDecryptedPayload).value(MESSAGE_KEY, "Unknown anomaly");
                        SPDLOG_WARN("Anomaly detected for lobby {}: {}", lobbyName, anomalyMsg);

                        nlohmann::json patch;
                        patch["status"] = LobbyStatus::ANOMALY;
                        patch["anomaly_message"] = anomalyMsg;
                        lobbyStore->asyncUpdateLobbyData(lobbyName, patch);
                    } });
            }
        }
    };

    m_dhtPublisher->getDhtManager()->fetchDhtItem(lobbyName, lobbyName, callback, false, -1);
}
