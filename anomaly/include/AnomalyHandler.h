#pragma once

#include "DhtPublisher.h"

class AnomalyHandler
{
  public:
    static constexpr const char* ANOMALY_KEY = "anomaly";
    static constexpr const char* MESSAGE_KEY = "message";

    AnomalyHandler(std::shared_ptr<DhtPublisher> dhtPublisher);
    ~AnomalyHandler() = default;

    void dispatchAnomalyMessage(const std::string &lobbyName, const std::string &message);
    void checkLobbyAnomaly(const std::string &lobbyName, std::shared_ptr<LobbyStore> lobbyStore);

  private:
    std::shared_ptr<DhtPublisher> m_dhtPublisher;
};
