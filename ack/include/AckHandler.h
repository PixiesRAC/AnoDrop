#pragma once

#include "DhtPublisher.h"
#include "LobbyStore.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <vector>
#include <set>

class AckHandler
{
  public:
    AckHandler(std::shared_ptr<DhtPublisher> dhtPublisher);
    ~AckHandler() = default;

    // Called to fetch current DHT ACK value, merge with our received seqs, and PUT back if needed
    void pushAcks(const LobbyData &lobbyData);

    // Called to fetch peer's ACK value and update our local LobbyStore
    void fetchPeerAcks(const LobbyData &lobbyData, std::shared_ptr<LobbyStore> lobbyStore);

    static std::string compressAcks(const AckState& state);
    static AckState parseAcks(const std::string& ackStr);

  private:
    std::shared_ptr<DhtPublisher> m_dhtPublisher;
    std::mutex m_ackMutex;

    static constexpr char ACK_KEY[5] = "_ACK";
    std::unordered_map<std::string, std::string> m_lastPushedAckString;
    std::unordered_map<std::string, std::string> m_lastFetchedPeerAckString;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_lastAckPushTime;
    std::unordered_map<std::string, AckState> m_receivedSeqsCache;
    std::unordered_map<std::string, int64_t> m_lastContiguousSeq;
};
