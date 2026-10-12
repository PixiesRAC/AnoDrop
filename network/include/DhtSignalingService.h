#pragma once

#include <memory>
#include <string>

class DhtPublisher;
class LobbyStore;
class LobbyObserver;

class DhtSignalingService
{
  public:
    static void dropAnomalyMessage(DhtPublisher *dhtPublisher, const std::string &message, const std::string &lobbyName);
    static void dropHeartbeatMessage(DhtPublisher *dhtPublisher, int offsetSeconds, const std::string &lobbyName, const std::string &heartbeatKey);
    static void dropAckMessage(DhtPublisher *dhtPublisher, const std::string &message, const std::string &lobbyName, const std::string &ackKey, int offsetSeconds);

    static void signalAndPersistAnomaly(DhtPublisher *dhtPublisher,
                                        const std::string &lobbyName,
                                        const std::string &anomalyMsg,
                                        std::shared_ptr<LobbyStore> lobbyStore,
                                        std::shared_ptr<LobbyObserver> lobbyObs = nullptr);
};
