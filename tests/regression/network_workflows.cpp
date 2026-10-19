#include <filesystem>
#include <future>

#include "DhtChat.h"
#include "DhtTransport.h"
#include "HandshakeCoordinator.h"
#include "LobbyDataSerializer.h"
#include "LobbyStoreTestAccess.h"
#include "MessageReceiver.h"
#include "OutboxPublisher.h"
#include "PartitionedThreadPool.h"

void check(bool condition, const std::string &message);
namespace
{
struct FakeTransport final : DhtTransport
{
    struct Get
    {
        DhtRequest request;
        DhtItemCallback complete;
    };
    std::vector<Get> gets;
    std::vector<PutCompletion> puts;
    int announcements = 0, handshakes = 0, anomalies = 0;
    bool accept = true, fail = false;
    bool fetchDhtItem(const std::string &, const std::string &, DhtItemCallback cb, DhtRequest request) override
    {
        if (fail)
            throw std::runtime_error("Injected fetch failure");
        if (!accept)
            return false;
        gets.push_back({request, std::move(cb)});
        return true;
    }
    bool publishHandshake(const std::string &, const std::string &, std::int64_t, PutCompletion cb) override
    {
        ++handshakes;
        puts.push_back(std::move(cb));
        return true;
    }
    bool publishChatMessage(const ChatPublishParams &, PutCompletion cb = {}) override
    {
        puts.push_back(std::move(cb));
        return true;
    }
    bool publishSequenceAnnouncement(const ChatPublishParams &) override
    {
        ++announcements;
        return true;
    }
    bool publishAnomaly(const std::string &, const std::string &) override
    {
        ++anomalies;
        return true;
    }
    bool publishHeartbeat(int, const std::string &, const std::string &) override
    {
        return true;
    }
    bool publishAck(const std::string &, const std::string &, const std::string &, int) override
    {
        return true;
    }
};
void fence(PartitionedThreadPool &pool)
{
    std::promise<void> promise;
    auto future = promise.get_future();
    pool.post("workflow", [&]
              { promise.set_value(); });
    check(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Network executor stalled");
    future.get();
}
} // namespace

void networkWorkflows(const std::filesystem::path &root)
{
    LobbyStoreTestAccess store("pw", root.string());
    LobbyUpdate initial;
    initial.status = LobbyStatus::HANDSHAKE_DONE;
    initial.private_node_discussion = "channel";
    initial.announced_x25519_pub = "peer";
    store.commit("workflow", initial);
    auto lobby = store.getLobby("workflow");
    FakeTransport transport;
    PartitionedThreadPool pool(1);
    MessageReceiver receiver(transport, store, pool, 5);
    transport.accept = false;
    check(!receiver.fetchLatestSequence(lobby), "Rejected fetch accepted");
    transport.accept = true;
    transport.fail = true;
    bool threw = false;
    try
    {
        receiver.fetchLatestSequence(lobby);
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    check(threw, "Fetch failure lost");
    transport.fail = false;
    check(receiver.fetchLatestSequence(lobby), "Failed fetch left a pending marker");
    check(!receiver.fetchLatestSequence(lobby), "Duplicate in-flight sequence fetch");
    transport.gets.back().complete({DhtFetchStatus::Timeout, {}, 0});
    check(receiver.fetchLatestSequence(lobby), "Timeout left a pending marker");
    auto sequenceCallback = transport.gets.back().complete;
    sequenceCallback({DhtFetchStatus::Found, "", 1});
    fence(pool);
    check(transport.gets.back().request.kind == DhtFetchKind::Message, "Sequence did not schedule body");
    auto messageCallback = transport.gets.back().complete;
    ChatMessage message{MessageDirection::OUT, "incoming", TimeUtils::computeFakeTimestamp(0), 1};
    auto payload = nlohmann::json(message).dump();
    messageCallback({DhtFetchStatus::Found, payload, 1});
    messageCallback({DhtFetchStatus::Found, payload, 1});
    fence(pool);
    check(lobby->historySize() == 1 && lobby->getNetworkState().downloaded_seq_nums.acknowledges(1),
          "Duplicate callbacks duplicated the stored message");
    check(receiver.fetchLatestSequence(lobby), "Next sequence fetch rejected");
    auto late = transport.gets.back().complete;
    store.execute("workflow", lobby::SetVisibility{true});
    const auto count = transport.gets.size();
    late({DhtFetchStatus::Found, "", 2});
    fence(pool);
    check(transport.gets.size() == count, "Hidden lobby scheduled a late body fetch");
    store.execute("workflow", lobby::SetVisibility{false});

    store.execute("workflow", lobby::StoreOutgoingMessage{
                                  {MessageDirection::OUT, "outgoing", TimeUtils::computeFakeTimestamp(0), 1}});
    OutboxPublisher outbox(transport, store, pool);
    ChatPublishParams params;
    params.lobbyName = "workflow";
    params.targetSeqNum = 1;
    outbox.publishChatMessage(params);
    transport.puts.back()(false);
    fence(pool);
    check(transport.announcements == 0 && !lobby->getNetworkState().dht_acked_seq_nums.acknowledges(1),
          "Failed body PUT announced progress");
    outbox.publishChatMessage(params);
    auto published = transport.puts.back();
    published(true);
    fence(pool);
    check(transport.announcements == 1 && lobby->getNetworkState().dht_acked_seq_nums.acknowledges(1),
          "Successful body PUT did not confirm and announce");

    store.execute("handshake", lobby::BeginHandshake{"Alice"});
    HandshakeCoordinator handshake(transport, store, pool);
    handshake.dispatchHandshake("handshake", "Alice");
    auto staleHandshake = transport.gets.back().complete;
    store.execute("handshake", lobby::FlagAnomaly{"retain"});
    staleHandshake({DhtFetchStatus::Missing, {}, 0});
    fence(pool); // Single worker also fences the handshake partition.
    check(store.getLobby("handshake")->getStatus() == LobbyStatus::ANOMALY && transport.handshakes == 0,
          "Stale handshake overwrote a terminal state or published");

    pool.shutdown();
    messageCallback({DhtFetchStatus::Cancelled, {}, 1});
    bool rejected = false;
    try
    {
        messageCallback({DhtFetchStatus::Found, payload, 1});
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    check(rejected && lobby->historySize() == 2, "Stopped executor accepted late work");
}

void outgoingCommands(const std::filesystem::path &root)
{
    struct Queue : OutgoingMessageQueue
    {
        int attempts = 0;
        void enqueueChatMessage(const ChatPublishParams &) override
        {
            ++attempts;
            throw std::runtime_error("Injected queue saturation");
        }
    };
    auto store = std::make_shared<LobbyStoreTestAccess>("pw", root.string());
    LobbyUpdate initial;
    initial.status = LobbyStatus::HANDSHAKE_DONE;
    initial.private_node_discussion = "channel";
    store->commit("send", initial);
    auto queue = std::make_shared<Queue>();
    DhtChat chat(store->getLobby("send"), queue, store);
    for (int i = 0; i < 12; ++i)
    {
        const auto result = chat.sendMessage(std::string(1100, 'x'));
        check(!result && result.error() == SendError::TooLarge, "Rejected send consumed a rate-limit slot");
    }
    for (int i = 1; i <= 10; ++i)
    {
        const auto result = chat.sendMessage("durable");
        check(result && *result == i, "Saturated queue lost a durable outgoing message");
    }
    const auto limited = chat.sendMessage("limited");
    check(!limited && limited.error() == SendError::RateLimited && queue->attempts == 10 &&
              store->getLobby("send")->historySize() == 10,
          "Rate limit did not count committed sends");
    DhtChat failedWrites(store->getLobby("send"), queue, store);
    store->closeQueue();
    for (int i = 0; i < 12; ++i) {
        const auto failed = failedWrites.sendMessage("not stored");
        check(!failed && failed.error() == SendError::StorageFailed,
              "Failed storage consumed a rate-limit slot");
    }
    bool invalid = false;
    try
    {
        DhtChat incomplete({}, queue, store);
    }
    catch (const std::invalid_argument &)
    {
        invalid = true;
    }
    check(invalid, "Invalid chat dependencies accepted");
}

void reentrantSend(const std::filesystem::path& root)
{
    struct Queue final : OutgoingMessageQueue {
        DhtChat* chat{};
        bool nested = false;
        std::expected<std::int64_t, SendError> second = std::unexpected(SendError::Unavailable);
        void enqueueChatMessage(const ChatPublishParams&) override {
            if (!nested) { nested = true; second = chat->sendMessage("nested"); }
        }
    };
    auto store = std::make_shared<LobbyStoreTestAccess>("pw", root.string());
    LobbyUpdate initial;
    initial.status = LobbyStatus::HANDSHAKE_DONE;
    initial.private_node_discussion = "channel";
    store->commit("reentrant", initial);
    auto queue = std::make_shared<Queue>();
    DhtChat chat(store->getLobby("reentrant"), queue, store);
    queue->chat = &chat;
    auto first = chat.sendMessage("outer");
    check(first && *first == 1 && queue->second && *queue->second == 2,
          "Queue reentry deadlocked or lost sequence ordering");
}
