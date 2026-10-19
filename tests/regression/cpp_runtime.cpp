#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include "Lobby.h"
#include "LobbyTestAccess.h"
#include "ChatViewModel.h"
#include "PartitionedThreadPool.h"
#include "IoWorker.h"

// Failure injection is confined to this test executable and this thread.
thread_local bool failAllocation = false;
thread_local int allocationsBeforeFailure = -1;
void* operator new(std::size_t size) {
    if (failAllocation || allocationsBeforeFailure == 0) throw std::bad_alloc();
    if (allocationsBeforeFailure > 0) --allocationsBeforeFailure;
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

template<class T>
concept PubliclyMutableLobby = requires(T& lobby, LobbyUpdate& update) { lobby.onUpdate(update); };
static_assert(!PubliclyMutableLobby<LobbyObserver>);
static_assert(!PubliclyMutableLobby<Lobby>);

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::atomic<int> readAttempts{};
// Controlled storage boundary for the real ChatViewModel + ChatTaskWorker.
DhtChat::DhtChat(std::shared_ptr<LobbyObserver> lobby, std::shared_ptr<OutgoingMessageQueue>, std::shared_ptr<LobbyStore>)
    : m_lobby(std::move(lobby)) {}
void DhtChat::markRead(std::size_t count) {
    if (++readAttempts == 1) throw std::runtime_error("Injected transient write failure");
    LobbyUpdate update; update.read_messages_count = count;
    LobbyTestAccess::onUpdate(*m_lobby, update);
}
void DhtChat::markResynced(std::vector<std::int64_t>) {
    throw std::runtime_error("Unexpected resync in UI read test");
}
std::expected<std::int64_t, SendError> DhtChat::sendMessage(const std::string&) {
    throw std::runtime_error("Unexpected network send in UI read test");
}
std::string DhtChat::getLobbyName() const { return m_lobby->getLobbyName(); }

void poolFailure() {
    std::atomic<int> active{};
    int launches = 0;
    bool failed = false;
    try {
        PartitionedThreadPool pool(3, [&](std::function<void()> task) {
            if (++launches == 3) throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
            return std::thread([&, task = std::move(task)] {
                ++active;
                task();
                --active;
            });
        });
    } catch (const std::system_error&) { failed = true; }
    require(failed && launches == 3 && active == 0, "Partial pool construction did not join existing workers");
    PartitionedThreadPool pool(1);
    std::promise<void> done;
    auto completion = done.get_future();
    require(pool.enqueue("same", [] { throw 42; }), "Exception task rejected");
    require(pool.enqueue("same", [&] { done.set_value(); }), "Follow-up task rejected");
    require(completion.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
            "Worker did not survive non-standard exception");
}

void lobbyFailures() {
    LobbyData data;
    data.metadata.lobby_name = "runtime";
    data.sync.my_seq_num = 1;
    data.history.append({MessageDirection::OUT, "original", 1700000000, 1});
    Lobby lobby(data);
    lobby.withDataView([&](const auto& metadata, const auto&, const auto&) {
        LobbyUpdate update;
        update.my_pseudo = "changed";
        LobbyTestAccess::onUpdate(lobby, update); // Must not run with the reader lock held.
        require(metadata.my_pseudo.empty(), "Snapshot mutated during callback");
    });
    require(lobby.getMetadata().my_pseudo == "changed", "Reentrant update failed");
    const auto before = lobby.getLobbyData();
    std::vector<HistoryRecord> replacement{{{MessageDirection::OUT, "replacement", 1700000000, 1}, {}}};
    bool failed = false;
    failAllocation = true;
    try { LobbyTestAccess::resetHistoryRecords(lobby, std::move(replacement)); }
    catch (const std::bad_alloc&) { failed = true; }
    failAllocation = false;
    require(failed, "Allocation failure was not injected");
    require(lobby.getLobbyData() == before, "Failed replacement modified lobby state");
    require(lobby.pendingOutgoing(0, 10).at(0).content == "original", "Failed replacement damaged index");
    LobbyUpdate duplicate;
    duplicate.new_message = before.history.materialize().at(0);
    LobbyTestAccess::onUpdate(lobby, duplicate);
    require(!duplicate.new_message && lobby.historySize() == 1, "Failed replacement damaged deduplication");
}

void readRetry() {
    LobbyData data;
    data.metadata.lobby_name = "read-retry";
    data.history.append({MessageDirection::IN, "incoming", 1700000000, 1});
    auto lobby = std::make_shared<Lobby>(data);
    DhtChat chat(lobby, {}, {});
    ui::ChatViewModel view(lobby, &chat);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (lobby->getMetadata().read_messages_count != 1 && std::chrono::steady_clock::now() < deadline) {
        (void)view.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(readAttempts >= 2 && lobby->getMetadata().read_messages_count == 1,
            "Read cursor was not retried after failed write");
}
void atomicUpdate() {
    bool succeeded = false;
    int failures = 0;
    for (int allowance = 0; allowance < 100 && !succeeded; ++allowance) {
        LobbyData data;
        data.metadata.lobby_name = "atomic";
        data.sync.my_seq_num = 1;
        data.history.append({MessageDirection::OUT, "original", 1700000000, 1});
        Lobby lobby(data);
        LobbyUpdate update;
        update.new_message = ChatMessage{MessageDirection::OUT, std::string(200, 'm'), 1700000001, 2};
        update.my_seq_num = 2;
        update.my_pseudo = std::string(200, 'p');
        update.other_pseudo = std::string(200, 'o');
        update.peer_acked_seq_nums = AckState{0, {2}};
        allocationsBeforeFailure = allowance;
        try { LobbyTestAccess::onUpdate(lobby, update); succeeded = true; }
        catch (const std::bad_alloc&) { ++failures; }
        allocationsBeforeFailure = -1;
        if (!succeeded) {
            require(lobby.getLobbyData() == data, "Allocation failure partially changed lobby");
            require(!lobby.consumeUpdateFlag(), "Failed update published a change notification");
            LobbyUpdate retry;
            retry.new_message = ChatMessage{MessageDirection::OUT, "retry", 1700000001, 2};
            retry.my_seq_num = 2;
            LobbyTestAccess::onUpdate(lobby, retry);
            require(lobby.historySize() == 2 && lobby.pendingOutgoing(0, 10).size() == 2,
                    "Failed update corrupted history indexes");
        } else require(lobby.historySize() == 2 && lobby.getNetworkState().my_seq_num == 2,
                       "Successful update incomplete");
    }
    require(succeeded && failures > 5, "Not enough allocation failure paths exercised");
}
void poolCapacity() {
    PartitionedThreadPool pool(1);
    std::promise<void> entered, release;
    auto ready = entered.get_future();
    auto gate = release.get_future().share();
    pool.post("same", [&] { entered.set_value(); gate.wait(); });
    ready.get();
    bool allAccepted = true;
    for (int i = 0; i < 1024; ++i) allAccepted &= pool.enqueue("same", [] {});
    const bool overflowAccepted = pool.enqueue("same", [] {});
    release.set_value();
    pool.shutdown();
    require(allAccepted && !overflowAccepted, "Pool capacity is not enforced");
}
void poolShutdown() {
    PartitionedThreadPool pool(1);
    std::promise<void> entered, proceed;
    auto ready = entered.get_future();
    auto release = proceed.get_future().share();
    pool.post("same", [&] { entered.set_value(); release.wait(); pool.shutdown(); });
    ready.get();
    std::jthread first([&] { pool.shutdown(); });
    std::jthread second([&] { pool.shutdown(); });
    proceed.set_value();
    first.join(); second.join();
    require(!pool.enqueue("same", [] {}), "Stopped pool accepted new work");
}
void ioLifecycle() {
    IoWorker worker;
    std::promise<void> entered, proceed;
    auto ready = entered.get_future();
    auto release = proceed.get_future().share();
    std::atomic<int> finished{};
    require(worker.enqueue([&] { entered.set_value(); release.wait(); worker.stopAndDrain(); ++finished; }), "Task rejected");
    require(worker.enqueue([&] { ++finished; }), "Queued task rejected");
    ready.get();
    std::jthread first([&] { worker.stopAndDrain(); });
    std::jthread second([&] { worker.stopAndDrain(); });
    proceed.set_value();
    first.join(); second.join();
    require(finished == 2 && !worker.enqueue([] {}), "Concurrent drain lost work or accepted new work");
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test name");
        const std::string test = argv[1];
        if (test == "pool_capacity") poolCapacity();
        else if (test == "atomic_update") atomicUpdate();
        else if (test == "pool_shutdown") poolShutdown();
        else if (test == "io_lifecycle") ioLifecycle();
        else if (test == "pool_failure") poolFailure();
        else if (test == "lobby_failure") lobbyFailures();
        else if (test == "read_retry") readRetry();
        else throw std::runtime_error("Unknown test");
        std::cout << "PASS " << test << '\n';
    } catch (const std::exception& error) {
        failAllocation = false;
        allocationsBeforeFailure = -1;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
