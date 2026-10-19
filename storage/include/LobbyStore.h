#pragma once
#include <expected>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "EncryptedJournal.h"
#include "HandshakeStateEngine.h"
#include "IoWorker.h"
#include "LobbyCommand.h"
#include "LobbyData.h"
#include "LobbyRegistry.h"
#include "LobbyUpdate.h"
#include "StoreError.h"

class Lobby;
class HandshakePreparation;
class DhtChat;

class LobbyStore
{
  public:
    static constexpr const char *DEFAULT_STORE_DIR = "store";

    LobbyStore(const std::string &masterPassword, const std::string &storeDir = DEFAULT_STORE_DIR);
    ~LobbyStore();

    // Returns only after a durable commit; throws on closed/full queue or I/O failure.
    bool execute(const std::string &lobbyName, lobby::Command command);
    void updateMasterPassword(const std::string &newPassword);
    void compact();

    [[nodiscard]] std::shared_ptr<LobbyObserver> getLobby(const std::string &lobbyName);
    [[nodiscard]] std::vector<std::shared_ptr<LobbyObserver>> getLobbies();

    [[nodiscard]] std::expected<HandshakeTransitionResult, std::string> advanceHandshake(
        const std::string &lobbyName, const std::string &ownPayload,
        std::int64_t sequence, const std::string &value, bool found);

  protected:
    friend class HandshakePreparation;
    friend class DhtChat;

    bool transact(const std::string &lobbyName, std::function<LobbyUpdate(const LobbyData &)> mutator);
    bool commit(const std::string &lobbyName, const LobbyUpdate &update);
    void drainWorkerQueue() { m_ioWorker.stopAndDrain(); }

  private:
    struct MasterKeyBuffer
    {
        std::vector<unsigned char> bytes;
        ~MasterKeyBuffer()
        {
            if (!bytes.empty())
                OPENSSL_cleanse(bytes.data(), bytes.size());
        }
    };

    template <typename Fn>
    auto runOnIoWorker(Fn &&fn) -> std::invoke_result_t<std::decay_t<Fn> &>
    {
        using Result = std::invoke_result_t<std::decay_t<Fn> &>;
        if (m_ioWorker.isCurrentWorker())
            throw std::logic_error("Nested store operation is not allowed");
        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();
        const bool accepted = m_ioWorker.enqueue([this, task = std::forward<Fn>(fn), promise]() mutable
                                                 {
            try
            {
                if (m_needsRecovery)
                    throw StoreRecoveryRequired();
                if constexpr (std::is_void_v<Result>)
                {
                    task();
                    promise->set_value();
                }
                else
                {
                    promise->set_value(task());
                }
            }
            catch (...)
            {
                promise->set_exception(std::current_exception());
            } });
        if (!accepted)
            throw std::runtime_error("Store queue is closed or full");
        return future.get();
    }

    std::shared_ptr<LobbyObserver> getOrCreateLobby(const std::string &lobbyName);
    void compactUnlocked();
    std::size_t compactionThreshold() const;

    std::string m_storeDir;
    MessageLoader journalMessageLoader(EncryptedJournal::Location location) const;
    std::shared_ptr<MasterKeyBuffer> m_masterKey{std::make_shared<MasterKeyBuffer>()};
    std::unique_ptr<LobbyRegistry> m_lobbyRegistry;

    std::shared_ptr<EncryptedJournal> m_journal;
    bool m_needsRecovery{false}; // Accessed only by IoWorker.
    std::size_t m_transactionCount{0};
    IoWorker m_ioWorker;
};
