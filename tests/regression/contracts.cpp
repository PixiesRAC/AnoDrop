#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <unistd.h>

#include "Checks.h"
#include "Logger.h"
#include "HandshakePayloadCodec.h"
#include "HandshakeStateEngine.h"
#include "HandshakePreparation.h"
#include "AckHandler.h"
#include "DhtMessageCodec.h"
#include "LobbyUpdateCodec.h"
#include "Lobby.h"
#include "LobbyDataSerializer.h"
#include "LobbyStoreTestAccess.h"
#include "LobbyTestAccess.h"
#include "MessageStatusResolver.h"
#include "Terminal.h"

template<class T> concept RawStoreCommit = requires(T& store, const LobbyUpdate& update) {
    store.commit("raw", update);
};
template<class T> concept RawStoreTransact = requires(T& store) {
    store.transact("raw", [](const LobbyData&) { return LobbyUpdate{}; });
};
static_assert(!RawStoreCommit<LobbyStore> && !RawStoreTransact<LobbyStore>);
static_assert(network::DhtMessageCodec::fitsBep44("hello"));
using json = nlohmann::json;
void reentrantSend(const std::filesystem::path& root);
void outgoingCommands(const std::filesystem::path& root);
void networkWorkflows(const std::filesystem::path& root);
void handshakeSequenceContract(const std::filesystem::path &root, bool locallyReady, bool finalized);
void handshakeRejectionContract(const std::filesystem::path &root);
void check(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}
LobbyData sample()
{
    LobbyData d{};
    d.metadata.lobby_name = "regression";
    d.metadata.status = LobbyStatus::HANDSHAKE_DONE;
    d.metadata.my_pseudo = "Alice";
    d.metadata.other_pseudo = "Bob";
    d.metadata.fake_timezone = {"test", 3600};
    return d;
}
ChatMessage msg(int64_t seq, MessageDirection direction = MessageDirection::OUT,
                std::string content = "hello")
{
    return {direction, content, 1700000000, seq};
}
void setHistory(LobbyData& d, std::initializer_list<ChatMessage> messages)
{
    d.history.records.clear();
    for (const auto& message : messages) d.history.append(message);
}
void status(const LobbyData &d, size_t i, const std::string &expected, int color)
{
    std::map<int64_t, std::vector<int64_t>> forward;
    std::map<int64_t, int64_t> reverse;
    ui::MessageStatusResolver::buildResyncMaps(d.history.materialize(), forward, reverse);
    auto actual = ui::MessageStatusResolver::resolveStatus(d.history.materialize().at(i), {d.metadata, d.sync}, forward, reverse);
    check(actual == std::pair(expected, color), "Incorrect UI status: " + actual.first + " expected " + expected);
}
void patch(Lobby &lobby, json p)
{
    auto update = p.get<LobbyUpdate>();
    LobbyTestAccess::onUpdate(lobby, update);
}
void patch(Lobby &lobby, LobbyUpdate &update)
{
    LobbyTestAccess::onUpdate(lobby, update);
}
struct Temp
{
    std::filesystem::path path;
    Temp()
    {
        std::string pattern = (std::filesystem::temp_directory_path() / "anodrop-contract-XXXXXX").string();
        auto result = mkdtemp(pattern.data());
        if (!result)
            throw std::runtime_error("mkdtemp failed");
        path = result;
    }
    ~Temp()
    {
        std::filesystem::remove_all(path);
    }
};
void writeFixture(const std::string &dir)
{
    LobbyStoreTestAccess store("regression-password", dir);
    auto d = sample();
    d.sync.my_seq_num = 1;
    d.sync.dht_acked_seq_nums.through = 1;
    d.sync.peer_acked_seq_nums.through = 1;
    json p = {{"status", d.metadata.status}, {"my_pseudo", "Alice"}, {"my_seq_num", 1},
              {"dht_acked_seq_nums", d.sync.dht_acked_seq_nums}, {"peer_acked_seq_nums", d.sync.peer_acked_seq_nums}};
    p["new_message"] = msg(1);
    store.commit(d.metadata.lobby_name, p.get<LobbyUpdate>());
}
int main(int argc, char **argv)
{
    std::map<std::string, std::function<void()>> cases{
        {"reentrant_send", [] { Temp t; reentrantSend(t.path); }},
        {"outgoing_commands", [] { Temp t; outgoingCommands(t.path); }},
        {"network_workflows", [] { Temp t; networkWorkflows(t.path); }},
        {"transaction_contracts", [] {
            Temp t;
            LobbyStoreTestAccess store("pw", t.path.string());
            bool rejected = false;
            try {
                store.transact("nested", [&](const LobbyData&) {
                    store.execute("nested", lobby::BeginHandshake{"Alice"});
                    return LobbyUpdate{};
                });
            } catch (const std::logic_error&) { rejected = true; }
            check(rejected && !store.getLobby("nested"), "Nested transaction deadlocked or changed state");
            LobbyRegistry registry;
            auto created = registry.getOrCreate("reentrant", [&] {
                check(!registry.find("reentrant"), "Unexpected registry entry");
                LobbyData data; data.metadata.lobby_name = "reentrant";
                return std::make_shared<Lobby>(data);
            });
            check(created && registry.find("reentrant") == created, "Registry factory failed");
        }},
        {"domain_commands", [] {
            Temp t;
            {
                LobbyStoreTestAccess store("pw", t.path.string());
                store.execute("commands", lobby::BeginHandshake{"Alice"});
                const auto journal = t.path / "transactions.v1.enc";
                auto reject = [&](lobby::Command command) {
                    const auto bytes = std::filesystem::file_size(journal);
                    bool rejected = false;
                    try { store.execute("commands", std::move(command)); }
                    catch (const std::invalid_argument&) { rejected = true; }
                    check(rejected, "Invalid command accepted");
                    check(std::filesystem::file_size(journal) == bytes, "Rejected command changed journal");
                };
                reject(lobby::BeginHandshake{"Other"});
                reject(lobby::StoreOutgoingMessage{msg(2)});
                reject(lobby::StoreOutgoingMessage{msg(1, MessageDirection::IN)});
                reject(lobby::ReceiveMessage{msg(1)});
                reject(lobby::ConfirmPublication{1});
                reject(lobby::RecordPeerReceipts{AckState{1, {}}});
                reject(lobby::MarkRead{1});
                auto observer = store.getLobby("commands");
                check(observer->historySize() == 0 && observer->getNetworkState().my_seq_num == 0,
                      "Rejected command changed state");
                store.execute("commands", lobby::StoreOutgoingMessage{msg(1)});
                store.execute("commands", lobby::ConfirmPublication{1});
                store.execute("commands", lobby::RecordPeerReceipts{AckState{1, {}}});
                store.execute("commands", lobby::ReceiveMessage{msg(1, MessageDirection::IN)});
                const auto bytes = std::filesystem::file_size(journal);
                store.execute("commands", lobby::ReceiveMessage{msg(1, MessageDirection::IN)});
                check(observer->historySize() == 2 && std::filesystem::file_size(journal) == bytes,
                      "Duplicate receive appended history or journal");
                store.execute("commands", lobby::MarkResynced{{1}});
                store.execute("commands", lobby::MarkRead{2});
                store.execute("commands", lobby::SetVisibility{true});
                LobbyUpdate pending;
                pending.status = LobbyStatus::CONFIRMATION_PENDING;
                store.commit("commands", pending);
                store.execute("commands", lobby::CompleteHandshake{});
                check(observer->getMetadata().status == LobbyStatus::HANDSHAKE_DONE, "Handshake not completed");
                store.execute("commands", lobby::FlagAnomaly{"keep anomaly"});
                const auto beforeLateConfirmation = std::filesystem::file_size(journal);
                store.execute("commands", lobby::CompleteHandshake{});
                check(observer->getMetadata().status == LobbyStatus::ANOMALY &&
                      std::filesystem::file_size(journal) == beforeLateConfirmation,
                      "Late confirmation overwrote anomaly");
            }
            LobbyStoreTestAccess reopened("pw", t.path.string());
            auto observer = reopened.getLobby("commands");
            const auto data = observer->getLobbyDataWithoutHistory();
            check(observer->historySize() == 2 && data.metadata.read_messages_count == 2 &&
                  data.metadata.is_hidden && data.metadata.status == LobbyStatus::ANOMALY &&
                  data.sync.my_seq_num == 1 && data.sync.dht_acked_seq_nums.acknowledges(1) &&
                  data.sync.peer_acked_seq_nums.acknowledges(1) &&
                  data.sync.resynced_seq_nums == std::vector<std::int64_t>{1}, "Command effects lost on restart");
        }},
        {"statuses", []
         {
             auto d = sample();
             setHistory(d, {msg(1), msg(1, MessageDirection::IN)});
             status(d, 0, " [Sending...]", 4);
             status(d, 1, "", 0);
             d.sync.dht_acked_seq_nums.through = 1;
             status(d, 0, " [Sent]", 2);
             d.sync.peer_acked_seq_nums.through = 1;
             status(d, 0, " [Ack]", 3);
             d.sync.dht_acked_seq_nums.through = 0;
             status(d, 0, " [Ack]", 3);
             d.sync.peer_acked_seq_nums = {0, {2}};
             status(d, 0, " [Sending...]", 4);
         }},
        {"resync", []
         {
             auto d = sample();
             setHistory(d, {msg(1), msg(2, MessageDirection::OUT, "[Resync of Seq:1 12:00:00] hello")});
             d.sync.resynced_seq_nums = {1};
             status(d, 0, " [Sending resync...]", 4);
             d.sync.dht_acked_seq_nums = {0, {2}};
             status(d, 0, " [Sent (via resync)]", 5);
             d.sync.peer_acked_seq_nums = {0, {2}};
             status(d, 0, " [Ack (via resync)]", 3);
             d.sync.peer_acked_seq_nums = {1, {}};
             status(d, 1, " [Ack]", 3);
             d.history.append(msg(3, MessageDirection::OUT, "[Resync of Seq:invalid xx] hello"));
             status(d, 2, " [Sending...]", 4);
         }},
        {"ack_gaps", []
         {
             AckState a{0, {5, 3, 1, 3}};
             a.compress();
             check(a.through == 1 && a.selective == std::vector<uint64_t>({3, 5}), "gap incorrectly acknowledged");
             check(!a.acknowledges(2) && a.acknowledges(3), "selective ACK mismatch");
             a.selective.insert(a.selective.end(), {2, 4});
             a.compress();
             check(a.through == 5 && a.selective.empty(), "ACK compression failed");
             const auto parsed = AckHandler::parseAcks(AckHandler::compressAcks(AckState{3, {5, 8}}));
             check(parsed.through == 3 && parsed.selective == std::vector<uint64_t>({5, 8}),
                   "AckHandler::parseAcks roundtrip failed");
         }},
        {"sequence_dedup", []
         {
             Lobby l(sample());
             patch(l, {{"new_message", msg(2, MessageDirection::IN)}});
             patch(l, {{"new_message", msg(1, MessageDirection::IN)}});
             auto duplicate = json{{"new_message", msg(2, MessageDirection::IN)}}.get<LobbyUpdate>();
             patch(l, duplicate);
             check(!duplicate.new_message, "duplicate would be written to disk");
             patch(l, {{"new_message", msg(1)}, {"my_seq_num", 1}});
             patch(l, {{"message_sent_confirm", 1}});
             auto d = l.getLobbyData();
             check(d.history.materialize().size() == 3, "dedup must be per direction");
             check(d.sync.downloaded_seq_nums.through == 2 && d.sync.my_seq_num == 1 && d.sync.dht_acked_seq_nums.through == 1, "sequence state mismatch");
             patch(l, {{"new_message", msg(4, MessageDirection::IN, "[Resync of Seq:3 12:00:00] missing")}});
             check(l.getLobbyData().sync.downloaded_seq_nums.through == 4, "resync gap not closed");
         }},
        {"json_roundtrip", []
         {
             auto d = sample();
             d.sync.my_seq_num = 5;
             d.sync.announced_seq_num = 8;
             d.sync.dht_acked_seq_nums = {2, {4, 5}};
             d.sync.peer_acked_seq_nums = {1, {5}};
             d.sync.downloaded_seq_nums = {4, {8}};
             d.sync.resynced_seq_nums = {3};
             setHistory(d, {msg(5)});
             d.metadata.read_messages_count = 1;
             json j = d;
             check(j["outbound"]["my_seq_num"] == 5 && j["inbound"]["announced_seq_num"] == 8, "JSON schema mismatch");
             check(j.get<LobbyData>() == d, "JSON round trip loses fields");
         }},
        {"snapshot_schema", []
         {
             json j = {{"lobby_name", "legacy"}, {"my_seq_num", 7}, {"announced_seq_num", 9}, {"peer_acked_seq_nums", {{"through", 4}, {"selective", {7}}}}};
             bool rejected = false;
             try { (void)j.get<LobbyData>(); }
             catch (const json::exception&) { rejected = true; }
             check(rejected, "Obsolete flat snapshot must not be silently imported");
         }},
        {"persistence", []
         {
             Temp t;
             {
                 LobbyStoreTestAccess store("pw", t.path.string());
                 store.commit("regression", json({{"status", 1}, {"my_pseudo", "Alice"}}).get<LobbyUpdate>());
                 store.commit("regression", json({{"new_message", msg(1)}, {"my_seq_num", 1}}).get<LobbyUpdate>());
                 store.commit("regression", json({{"new_message", msg(1)}, {"my_seq_num", 1}}).get<LobbyUpdate>());
                 store.commit("regression", json({{"message_sent_confirm", 1}}).get<LobbyUpdate>());
                 store.commit("regression", json({{"peer_acked_seq_nums", AckState{1, {}}}}).get<LobbyUpdate>());
             }
             LobbyStoreTestAccess reloaded("pw", t.path.string());
             auto l = reloaded.getLobby("regression");
             check(bool(l), "lobby lost on restart");
             auto d = l->getLobbyData();
             check(d.history.materialize().size() == 1 && d.sync.my_seq_num == 1 && d.sync.peer_acked_seq_nums.through == 1 && d.sync.dht_acked_seq_nums.through == 1, "disk state mismatch after drain/reload");
             status(d, 0, " [Ack]", 3);
         }},
        {"compaction", []
         {
             Temp t;
             {
                 LobbyStoreTestAccess store("regression-password", t.path.string());
                 json p1 = {{"status", 1}, {"my_pseudo", "Alice"}, {"my_seq_num", 1}};
                 p1["new_message"] = msg(1, MessageDirection::OUT, "first message");
                 store.commit("lobby-compact", p1.get<LobbyUpdate>());

                 for (int i = 0; i < 50; ++i) {
                     json ph = {{"last_heartbeat", 1700000000 + i * 30}};
                     store.commit("lobby-compact", ph.get<LobbyUpdate>());
                 }

                 json p2 = {{"my_seq_num", 2}};
                 p2["new_message"] = msg(2, MessageDirection::OUT, "second message");
                 store.commit("lobby-compact", p2.get<LobbyUpdate>());

                 auto priorSnapshot = store.getLobby("lobby-compact")->getLobbyData();
                 const auto uncompactedSize = std::filesystem::file_size(t.path / "transactions.v1.enc");
                 store.compact();
                 const auto compactedSize = std::filesystem::file_size(t.path / "transactions.v1.enc");
                 check(compactedSize < uncompactedSize, "Compacted size should be smaller than uncompacted");
                 LobbyUpdate next;
                 next.new_message = msg(3, MessageDirection::OUT, "after compaction");
                 next.my_seq_num = 3;
                 store.commit("lobby-compact", next);
                 check(store.getLobby("lobby-compact")->historyPage(2, 1).at(0).content == "after compaction",
                       "New history unreadable after compaction");
                 check(priorSnapshot.history.materialize().at(0).content == "first message",
                       "Compaction invalidated a previously acquired history snapshot");
                 bool locked = false;
                 try { LobbyStoreTestAccess duplicate("regression-password", t.path.string()); }
                 catch (const std::runtime_error&) { locked = true; }
                 check(locked, "Compaction released the active store lock");
                 store.compact();
                 LobbyUpdate heartbeat;
                 heartbeat.last_heartbeat = 1700000000 + 49 * 30;
                 heartbeat.my_pseudo = "Alice after two compactions";
                 store.commit("lobby-compact", heartbeat);
             }
             {
                 LobbyStoreTestAccess reloaded("regression-password", t.path.string());
                 auto obs = reloaded.getLobby("lobby-compact");
                 check(bool(obs), "Lobby lost after compaction");
                 auto data = obs->getLobbyData();
                 check(data.metadata.status == LobbyStatus::HANDSHAKE_DONE, "Status corrupted");
                 check(data.metadata.my_pseudo == "Alice after two compactions", "Post-compaction metadata lost");
                 check(data.metadata.last_heartbeat == 1700000000 + 49 * 30, "Latest heartbeat lost");
                 auto msgs = data.history.materialize();
                 check(msgs.size() == 3, "Messages count mismatch after compaction");
                 check(msgs[2].content == "after compaction", "Post-compaction append lost");
                 check(msgs[0].content == "first message", "Message 1 content mismatch");
                 check(msgs[1].content == "second message", "Message 2 content mismatch");
             }
         }},
        {"compaction_pages", [] {
            Temp t;
            {
                LobbyStoreTestAccess store("pw", t.path.string());
                for (std::int64_t seq = 1; seq <= 300; ++seq) {
                    LobbyUpdate update;
                    update.new_message = msg(seq, MessageDirection::OUT, "page-" + std::to_string(seq));
                    update.my_seq_num = seq;
                    store.commit("pages", update);
                }
                store.compact();
                auto page = store.getLobby("pages")->historyPage(255, 3);
                check(page.size() == 3 && page[0].content == "page-256" && page[2].content == "page-258",
                      "Compaction lost records across page boundary");
            }
            LobbyStoreTestAccess reopened("pw", t.path.string());
            auto lobby = reopened.getLobby("pages");
            check(lobby->historySize() == 300 && lobby->historyPage(299, 1).at(0).content == "page-300",
                  "Paged compacted journal did not survive restart");
        }},
        {"password", []
         {
             Temp t;
             writeFixture(t.path.string());
             {
                 LobbyStoreTestAccess s("regression-password", t.path.string());
                 s.updateMasterPassword("changed");
             }
             bool rejected = false;
             try
             {
                 LobbyStoreTestAccess s("regression-password", t.path.string());
             }
             catch (...)
             {
                 rejected = true;
             }
             check(rejected, "old password still accepted");
             LobbyStoreTestAccess s("changed", t.path.string());
             check(s.getLobby("regression")->getLobbyData().history.materialize().size() == 1, "password change lost history");
         }},
        {"terminal", []
         {
             Terminal t;
             t.feed("\033[2J\033[3;4Hold status [Sending...]");
             t.feed("\033[3;4Hnew status [Ack]\033[K");
             check(t.lines[2].substr(3, 16) == "new status [Ack]", "terminal cursor or erase failed");
             check(t.text().find("Sending") == std::string::npos, "stale status retained");
             t.feed("\033[4;");
             t.feed("2Hfragment\033[31m colored\033[0m");
             check(t.lines[3].substr(1, 16) == "fragment colored", "fragmented ANSI/color handling failed");
             t.feed("\033[2J");
             check(t.text().find("Ack") == std::string::npos, "screen clear retained old ACK");
             t.feed("\033[1;1Hx\033[4b");
             check(t.lines[0].substr(0, 5) == "xxxxx", "terminal repeat-character failed");
         }},
        {"disk_reader", []
         {
             Temp t;
             writeFixture(t.path.string());
             regression::StoreReader reader(t.path);
             auto all = reader.read();
             check(all.size() == 1, "disk reader lost lobby");
             auto d = all[0].get<LobbyData>();
             regression::validate(d);
             check(d.history.materialize().size() == 1 && d.history.materialize()[0].seq_num == 1, "history decode failed");
             {
                 // The functional runner reads while AnoDrop holds the exclusive store lock.
                 LobbyStoreTestAccess live("regression-password", t.path.string());
                 LobbyUpdate update;
                 update.new_message = msg(2);
                 update.my_seq_num = 2;
                 live.commit("regression", update);
                 auto snapshot = reader.read().at(0).get<LobbyData>();
                 regression::validate(snapshot);
                 check(snapshot.sync.my_seq_num == 2 && snapshot.history.records.size() == 2,
                       "Live journal reader missed committed message");
             }
             const auto journal = t.path / "transactions.v1.enc";
             const auto original = regression::readText(journal);
             auto rejects = [&] {
                 bool failed = false;
                 try { (void)reader.read(); }
                 catch (const std::exception&) { failed = true; }
                 check(failed, "Corrupt journal snapshot accepted");
             };
             auto corrupt = original;
             corrupt.back() ^= 1;
             regression::writeText(journal, corrupt);
             rejects(); // AES-GCM authentication must fail.
             regression::writeText(journal, original.substr(0, original.size() - 1));
             rejects(); // In-progress/short records must be retried, never silently ignored.
             regression::writeText(journal, original);
             check(reader.read().at(0).get<LobbyData>().sync.my_seq_num == 2,
                   "Valid snapshot failed after rejected partial read");
         }},
        {"checker_mutations", []
         {
             auto d = sample();
             d.sync.my_seq_num = 1;
             setHistory(d, {msg(1)});
             regression::validate(d);
             auto rejects = [](const LobbyData &bad)
             {bool failed=false;try {regression::validate(bad);}catch(...) {failed=true;}check(failed,"checker accepted corrupt sequence state"); };
             auto bad = d;
             bad.history.append(msg(1));
             rejects(bad);
             bad = d;
             bad.sync.my_seq_num = 2;
             rejects(bad);
             bad = d;
             bad.sync.peer_acked_seq_nums.through = 2;
             rejects(bad);
             bad = d;
             bad.metadata.read_messages_count = 2;
             rejects(bad);
             regression::checkScreen(d, "[01/01/2026 | Seq:1] Alice: hello [Sending...]", "hello", MessageDirection::OUT);
             bool failed = false;
             try
             {
                 regression::checkScreen(d, "[01/01/2026 | Seq:1] Alice: hello [Ack]", "hello", MessageDirection::OUT);
             }
             catch (...)
             {
                 failed = true;
             }
             check(failed, "checker accepted wrong UI ACK");
         }},
        {"log_analysis", []
         {
             auto result = regression::analyzeLogs("\033[32m[error]\033[0m Failed to append history\n[error] DHT put failed (0 nodes accepted it) for seq: 3\n[warning] retry\n");
             check(result["errors"].size() == 1 && result["transient_dht_failures"].size() == 1 && result["warnings"].size() == 1, "log classification failed");
             Temp t;
             const auto diagnostic = t.path / "diagnostic.log";
             const auto previousLogger = spdlog::default_logger();
             Logger::init(diagnostic.string(), "trace");
             SPDLOG_INFO("[BOOT] Format check");
             SPDLOG_DEBUG("[BOOT] Source check");
             spdlog::default_logger()->flush();
             spdlog::set_default_logger(previousLogger);
             spdlog::drop("file_logger");
             const auto logged = regression::readText(diagnostic);
             check(logged.find('\x1b') == std::string::npos, "ANSI escape leaked into log file");
             check(logged.find("Z [info]") != std::string::npos, "Missing UTC timestamp");
             check(logged.find("contracts.cpp:") != std::string::npos, "Missing debug source");
             const auto permissions = std::filesystem::status(diagnostic).permissions();
             check((permissions & std::filesystem::perms::mask) ==
                   (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write), "Log permissions are not 0600");
             const auto sender = t.path / "sender.log", receiver = t.path / "receiver.log";
             const std::string id = "lobby=" + LogEvents::lobbyId("channel");
             const std::string transaction = "[MSG-OUT] " + id + " seq=1 write_ms=0.500 | Message persisted\n";
             const std::string received = "[MSG-IN] " + id + " seq=1 write_ms=0.500 | Message persisted\n";
             const std::string ack = "[ACK] " + id + " ack=" + json(AckState{1, {}}).dump() + " | Peer receipts persisted\n";
             regression::writeText(sender, transaction);
             regression::writeText(receiver, received);
             auto validateLogs = [&] {
                 regression::checkDeliveryLogs(regression::readText(sender), regression::readText(receiver),
                                               "channel", 1);
             };
             bool pending = false;
             try { validateLogs(); } catch (const std::runtime_error&) { pending = true; }
             check(pending, "Missing ACK diagnostic was accepted");
             // Simulate the later logger flush after the durable ACK is visible.
             regression::writeText(sender, transaction + ack);
             validateLogs();
             regression::writeText(sender, transaction + "[ACK] lobby=\"unrelated\" ack={} | Peer receipts persisted\n");
             pending = false;
             try { validateLogs(); } catch (const std::runtime_error&) { pending = true; }
             check(pending, "ACK log from another lobby was accepted");
         }},
        {"handshake_state", []
         {
             Temp t;
             handshakeSequenceContract(t.path, true, false);
         }},
        {"handshake_late_ready", []
         {
             Temp t;
             handshakeSequenceContract(t.path, true, true);
         }},
        {"handshake_late_pending", []
         {
             Temp t;
             handshakeSequenceContract(t.path, false, true);
         }},
        {"handshake_sequence_completion", []
         {
             Temp t;
             handshakeSequenceContract(t.path, false, false);
         }},
        {"handshake_rejections", []
         {
             Temp t;
             handshakeRejectionContract(t.path);
         }},
        {"imported_config", []
         {
             Temp t;
             auto input = t.path / "base.json";
             regression::writeText(input, json{{"store_path", "/never/touch"}, {"log_path", "/never/write"}, {"max_concurrent_fetches", 5000}, {"fetch_interval_ms", 9000}, {"pull_messages_on_startup", false}}.dump());
             auto result = regression::peerConfig(input, t.path / "Alice");
             check(result["max_concurrent_fetches"] == 5000 && result["fetch_interval_ms"] == 9000 && result["pull_messages_on_startup"] == false, "Runtime settings not preserved");
             check(result["store_path"] == (t.path / "Alice" / "store").string() && result["log_path"] == (t.path / "Alice" / "app.log").string(), "Imported configuration escaped isolated profile");
             check(result["log_level"] == "debug", "Missing diagnostic log level");
         }},
        {"delivery_observation", []
         {
             auto a = sample(), b = sample();
             setHistory(a, {msg(2)});
             setHistory(b, {msg(2, MessageDirection::IN)});
             a.sync.dht_acked_seq_nums = {0, {2}};
             auto observation = regression::deliveryObservation(a, b, "hello");
             check(observation["dht_ack"] == true && observation["peer_ack"] == false && observation["incoming_count"] == 1, "DHT ACK confused with peer ACK");
             a.sync.peer_acked_seq_nums = {0, {2}};
             check(regression::deliveryObservation(a, b, "hello")["peer_ack"] == true, "Selective peer ACK missed");
             a.sync.peer_acked_seq_nums = {2, {}};
             check(regression::deliveryObservation(a, b, "hello")["peer_ack"] == true, "Cumulative peer ACK missed");
         }},
        {"heartbeat", []
         {
             auto d = sample();
             check(!d.isPeerConnected(), "missing heartbeat connected");
             d.metadata.last_heartbeat = TimeUtils::computeFakeTimestamp(d.metadata.fake_timezone.offset_seconds);
             check(d.isPeerConnected(), "fresh heartbeat disconnected");
             d.metadata.last_heartbeat -= 301;
             check(!d.isPeerConnected(), "expired heartbeat connected");
         }}};
    try
    {
        if (argc == 3 && std::string(argv[1]) == "fixture")
        {
            writeFixture(argv[2]);
            return 0;
        }
        if (argc != 2 || !cases.contains(argv[1]))
            throw std::runtime_error("Provide a contract name or fixture <directory>");
        cases.at(argv[1])();
        std::cout << "PASS " << argv[1] << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
