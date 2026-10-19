#pragma once

#include <charconv>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include "DhtMessageCodec.h"
#include "DhtPublisher.h"
#include "LobbyStore.h"

class AckHandler
{
  public:
    explicit AckHandler(std::shared_ptr<DhtPublisher> dhtPublisher);
    ~AckHandler() = default;

    // Publish only the receipts reconstructed from durable incoming messages.
    void pushAcks(const LobbyData &lobbyData);

    // Called to fetch peer's ACK value and update our local LobbyStore
    void fetchPeerAcks(const LobbyData &lobbyData, std::shared_ptr<LobbyStore> lobbyStore);

    static std::string compressAcks(const AckState &state)
    {
        std::string result = std::to_string(state.through);
        for (uint64_t s : state.selective)
        {
            auto candidate = result + "," + std::to_string(s);
            if (!network::DhtMessageCodec::fitsBep44(candidate))
                break;
            result = std::move(candidate);
        }
        return result;
    }

    static AckState parseAcks(std::string_view ackStr)
    {
        AckState result;
        if (ackStr.empty())
            return result;

        if (!network::DhtMessageCodec::fitsBep44(ackStr))
            throw std::invalid_argument("ACK exceeds BEP44 limit");
        if (ackStr.back() == ',')
            throw std::invalid_argument("Trailing ACK separator");

        bool first = true;
        std::size_t pos = 0;
        while (pos <= ackStr.size())
        {
            const std::size_t comma = ackStr.find(',', pos);
            const std::string_view item = (comma == std::string_view::npos)
                                              ? ackStr.substr(pos)
                                              : ackStr.substr(pos, comma - pos);
            std::uint64_t val = 0;
            auto [ptr, ec] = std::from_chars(item.data(), item.data() + item.size(), val);
            if (ec != std::errc() || ptr != item.data() + item.size())
            {
                throw std::invalid_argument("Malformed ACK integer");
            }
            if (first)
            {
                result.through = val;
                first = false;
            }
            else if (result.selective.size() < 1000) // bounded
            {
                result.selective.push_back(val);
            }
            if (comma == std::string_view::npos)
                break;
            pos = comma + 1;
        }
        result.compress();
        return result;
    }

  private:
    std::shared_ptr<DhtPublisher> m_dhtPublisher;
    static constexpr char ACK_KEY[5] = "_ACK";

    struct CacheState
    {
        std::mutex ackMutex;
        std::unordered_map<std::string, std::string> lastPushedAckString;
        struct ReceivedAck {
            std::string encoded;
            AckState receipts;
        };
        std::unordered_map<std::string, ReceivedAck> lastFetchedPeerAckString;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastAckPushTime;
    };
    std::shared_ptr<CacheState> m_cache{std::make_shared<CacheState>()};
};
