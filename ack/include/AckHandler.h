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
    static constexpr char ACK_KEY[5] = "_ACK";

    struct CacheState
    {
        std::mutex ackMutex;
        std::unordered_map<std::string, std::string> lastPushedAckString;
        std::unordered_map<std::string, std::string> lastFetchedPeerAckString;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastAckPushTime;
        std::unordered_map<std::string, AckState> receivedSeqsCache;
        std::unordered_map<std::string, int64_t> lastContiguousSeq;
    };
    std::shared_ptr<CacheState> m_cache{std::make_shared<CacheState>()};
};
