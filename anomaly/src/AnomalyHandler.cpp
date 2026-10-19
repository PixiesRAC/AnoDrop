#include "LogEvents.h"
#include "AnomalyHandler.h"

#include <nlohmann/json.hpp>
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
                                  {
        if (auto *mgr = publisher->getDhtManager())
            mgr->publishAnomaly(lobbyName, message); });
}

bool AnomalyHandler::checkLobbyAnomaly(const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore)
{
    if (!m_dhtPublisher || !m_dhtPublisher->getDhtManager() || !lobbyStore)
        return false;

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
            pub->enqueueNetworkTask(lobbyName, [lobbyName, lobbyStore, valueStr = std::move(valueStr)]()
                                    {
                auto existingDecryptedPayload = HandshakePayloadCodec::decryptDhtHandShakePayload(valueStr, lobbyName);
                if (existingDecryptedPayload && existingDecryptedPayload->contains(ANOMALY_KEY) && (*existingDecryptedPayload)[ANOMALY_KEY] == true)
                {
                    std::string anomalyMsg = (*existingDecryptedPayload).value(MESSAGE_KEY, "Unknown anomaly");
                    SPDLOG_WARN("[ANOMALY] Anomaly detected lobby={}", LogEvents::lobbyId(lobbyName));

                    lobbyStore->execute(lobbyName, lobby::FlagAnomaly{anomalyMsg});
                } });
        }
    };

    return m_dhtPublisher->getDhtManager()->fetchDhtItem(lobbyName, lobbyName, callback, {DhtFetchKind::Signaling, -1, "anomaly"});
}
