#pragma once
#include <chrono>
#include <deque>
#include <expected>
#include <mutex>

#include "LobbyObserver.h"
#include "OutgoingMessageQueue.h"
class LobbyStore;
enum class SendError
{
    TooLarge,
    RateLimited,
    StorageFailed,
    RecoveryRequired,
    Unavailable,
    SequenceExhausted
};
class DhtChat
{
  public:
    static constexpr std::int64_t RATE_LIMIT_WINDOW_SECONDS = 10;
    static constexpr int RATE_LIMIT_MAX_MESSAGES = 10;
    DhtChat(std::shared_ptr<LobbyObserver> lobby, std::shared_ptr<OutgoingMessageQueue> publisher,
            std::shared_ptr<LobbyStore> store);
    [[nodiscard]] std::expected<std::int64_t, SendError> sendMessage(const std::string &message);
    void markRead(std::size_t count);
    void markResynced(std::vector<std::int64_t> sequences);
    [[nodiscard]] std::string getLobbyName() const;

  private:
    std::shared_ptr<LobbyObserver> m_lobby;
    std::shared_ptr<OutgoingMessageQueue> m_publisher;
    std::shared_ptr<LobbyStore> m_store;
    // Serializes send reservations through durable commit. Never acquired by IoWorker.
    std::mutex m_sendMutex;
    std::deque<std::chrono::steady_clock::time_point> m_recentSends;
};
