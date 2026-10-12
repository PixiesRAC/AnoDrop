#include "DhtSignalingService.h"

#include <nlohmann/json.hpp>

#include "DhtManager.h"
#include "DhtPublisher.h"
#include "Dropper.h"
#include "HandshakeKeyService.h"
#include "LobbyDataSerializer.h"
#include "LobbyObserver.h"
#include "LobbyStore.h"

void DhtSignalingService::dropAnomalyMessage(DhtPublisher *dhtPublisher, const std::string &message, const std::string &lobbyName)
{
    if (!dhtPublisher)
        return;
    auto dhtManager = dhtPublisher->getDhtManager();
    if (!dhtManager)
        return;

    HandshakeKeyService::ChannelKeys keys = dhtPublisher->deriveChannelKeys(lobbyName);
    Dropper dropper(dhtManager->getSession(), keys.publicKey, keys.privateKey);
    dropper.dropAnomalyMessage(message, lobbyName);
}

void DhtSignalingService::dropHeartbeatMessage(DhtPublisher *dhtPublisher, int offsetSeconds, const std::string &lobbyName, const std::string &heartbeatKey)
{
    if (!dhtPublisher)
        return;
    auto dhtManager = dhtPublisher->getDhtManager();
    if (!dhtManager)
        return;

    HandshakeKeyService::ChannelKeys keys = dhtPublisher->deriveChannelKeys(heartbeatKey);
    Dropper dropper(dhtManager->getSession(), keys.publicKey, keys.privateKey);
    dropper.dropHeartbeatMessage(offsetSeconds, lobbyName);
}

void DhtSignalingService::dropAckMessage(DhtPublisher *dhtPublisher, const std::string &message, const std::string &lobbyName, const std::string &ackKey, int offsetSeconds)
{
    if (!dhtPublisher)
        return;
    auto dhtManager = dhtPublisher->getDhtManager();
    if (!dhtManager)
        return;

    HandshakeKeyService::ChannelKeys keys = dhtPublisher->deriveChannelKeys(ackKey);
    Dropper dropper(dhtManager->getSession(), keys.publicKey, keys.privateKey);
    dropper.dropAckMessage(message, lobbyName, offsetSeconds);
}

void DhtSignalingService::signalAndPersistAnomaly(DhtPublisher *dhtPublisher,
                                                  const std::string &lobbyName,
                                                  const std::string &anomalyMsg,
                                                  std::shared_ptr<LobbyStore> lobbyStore,
                                                  std::shared_ptr<LobbyObserver> lobbyObs)
{
    nlohmann::json anomalyPatch;
    anomalyPatch["status"] = LobbyStatus::ANOMALY;
    anomalyPatch["anomaly_message"] = anomalyMsg;

    if (lobbyStore)
    {
        lobbyStore->asyncUpdateLobbyData(lobbyName, anomalyPatch);
    }
    else if (lobbyObs)
    {
        lobbyObs->onUpdate(anomalyPatch);
    }

    dropAnomalyMessage(dhtPublisher, anomalyMsg, lobbyName);
}
