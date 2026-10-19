#pragma once
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "CryptoUtils.h"
#include "LogEvents.h"
#include "LobbyDataSerializer.h"
#include "Lobby.h"
#include "LobbyTestAccess.h"
#include "LobbyUpdateCodec.h"

namespace regression
{
using json = nlohmann::json;
namespace fs = std::filesystem;
inline void require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
inline std::string readText(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}
inline void writeText(const fs::path &path, const std::string &text)
{
    std::ofstream out(path);
    out << text;
    require(bool(out), "Cannot write " + path.string());
}
inline std::vector<unsigned char> bytes(const std::string &text)
{
    return {text.begin(), text.end()};
}

// Import runtime settings, but never reuse a user's store or log destination.
inline json peerConfig(const fs::path &base, const fs::path &root)
{
    json config = {{"fetch_interval_ms", 5000}, {"max_concurrent_fetches", 5}, {"argon2_iterations", 5}, {"extra_salt_data_for_lobby_handshake", "AnoDrop"}, {"pull_messages_on_startup", true}};
    if (!base.empty())
    {
        auto supplied = json::parse(readText(base));
        require(supplied.is_object(), "Base configuration must be a JSON object");
        config.update(supplied);
    }
    config["store_path"] = (root / "store").string();
    config["log_path"] = (root / "app.log").string();
    config["log_level"] = "debug";
    return config;
}

struct WaitTimeout : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

inline json deliveryObservation(const LobbyData &source, const LobbyData &target, const std::string &token)
{
    json observation = {{"token", token}, {"outgoing_count", 0}, {"incoming_count", 0}};
    for (const auto &m : source.history.materialize())
        if (m.direction == MessageDirection::OUT && m.content == token)
        {
            observation["outgoing_count"] = observation["outgoing_count"].get<int>() + 1;
            observation["seq_num"] = m.seq_num;
            observation["dht_ack"] = source.sync.dht_acked_seq_nums.acknowledges(m.seq_num);
            observation["peer_ack"] = source.sync.peer_acked_seq_nums.acknowledges(m.seq_num);
        }
    for (const auto &m : target.history.materialize())
        if (m.direction == MessageDirection::IN && m.content == token)
        {
            observation["incoming_count"] = observation["incoming_count"].get<int>() + 1;
            observation["incoming_seq_num"] = m.seq_num;
        }
    observation["sender_dht_ack_state"] = source.sync.dht_acked_seq_nums;
    observation["sender_peer_ack_state"] = source.sync.peer_acked_seq_nums;
    observation["receiver_downloaded_state"] = target.sync.downloaded_seq_nums;
    return observation;
}

// Read a snapshot without acquiring the application lock or repairing its journal.
// Short records and authentication errors are failures, never silently skipped.
class StoreReader
{
    fs::path root;
    std::string cachedVault;
    std::vector<unsigned char> key;
    json decode(const std::string &blob, const std::string &aad)
    {
        auto plaintext = CryptoUtils::decryptAES256GCM(bytes(blob), key, bytes(aad));
        require(plaintext.has_value(), "Disk AES-GCM authentication failed: " + aad);
        return json::parse(plaintext->begin(), plaintext->end());
    }

  public:
    std::string password = "regression-password";
    explicit StoreReader(fs::path path) : root(std::move(path))
    {
    }
    json read()
    {
        auto vault = readText(root / "vault.enc");
        require(vault.size() == 76, "Invalid vault size");
        if (vault != cachedVault)
        {
            auto salt = bytes(vault.substr(0, 16));
            auto kek = CryptoUtils::deriveStoreKey(password, salt);
            auto result = CryptoUtils::decryptAES256GCM(bytes(vault.substr(16)), kek);
            require(result.has_value(), "Vault authentication failed");
            key = *result;
            cachedVault = vault;
        }
        json result = json::array();
        std::map<std::string, std::unique_ptr<Lobby>> lobbies;
        const auto path = root / "transactions.v1.enc";
        if (fs::exists(path)) {
            const auto blob = readText(path);
            std::size_t offset = 0;
            std::uint64_t index = 0;
            while (offset < blob.size()) {
                require(blob.size() - offset >= 4, "Incomplete journal header");
                const auto* header = reinterpret_cast<const unsigned char*>(blob.data() + offset);
                const std::uint32_t size = std::uint32_t(header[0]) | (std::uint32_t(header[1]) << 8)
                    | (std::uint32_t(header[2]) << 16) | (std::uint32_t(header[3]) << 24);
                offset += 4;
                require(size >= 28 && size <= 4 * 1024 * 1024 && size <= blob.size() - offset,
                        "Invalid/incomplete journal record");
                const auto record = decode(blob.substr(offset, size), "AnoDropJournal/v1/" + std::to_string(index++));
                require(record.at("version") == 1, "Unsupported journal version");
                const auto name = record.at("lobby").get<std::string>();
                if (!lobbies.contains(name)) {
                    LobbyData data;
                    data.metadata.lobby_name = name;
                    lobbies.emplace(name, std::make_unique<Lobby>(data));
                }
                auto update = record.at("update").get<LobbyUpdate>();
                LobbyTestAccess::onUpdate(*lobbies.at(name), update);
                offset += size;
            }
        }
        result = json::array();
        for (const auto& [name, lobby] : lobbies) result.push_back(lobby->getLobbyData());
        return result;
    }
};
inline void validateAck(const AckState &ack, int64_t bound, const std::string &label)
{
    require(bound >= 0 && ack.through <= static_cast<uint64_t>(bound), label + ": through exceeds counter");
    uint64_t previous = ack.through;
    for (auto s : ack.selective)
    {
        require(s > previous && s <= static_cast<uint64_t>(bound) && s != ack.through + 1, label + ": invalid/unsorted/uncompressed selective ACK");
        previous = s;
    }
}
inline void validate(const LobbyData &d)
{
    require(d.metadata.read_messages_count <= d.history.materialize().size(), "Read count exceeds history");
    require(d.metadata.status == LobbyStatus::PENDING_HANDSHAKE || d.metadata.status == LobbyStatus::HANDSHAKE_DONE || d.metadata.status == LobbyStatus::ANOMALY || d.metadata.status == LobbyStatus::CONFIRMATION_PENDING, "Invalid lobby status");
    std::set<std::pair<MessageDirection, int64_t>> seen;
    int64_t next = 1;
    for (const auto &m : d.history.materialize())
    {
        require(m.seq_num > 0, "Invalid message sequence");
        require(seen.emplace(m.direction, m.seq_num).second, "Duplicate sequence within direction");
        if (m.direction == MessageDirection::OUT)
            require(m.seq_num == next++, "Outgoing sequence gap or reorder");
        else
        {
            require(m.direction == MessageDirection::IN, "Invalid direction");
            require(m.seq_num <= d.sync.announced_seq_num && d.sync.downloaded_seq_nums.acknowledges(m.seq_num), "Incoming sequence not announced/downloaded");
        }
    }
    require(next - 1 == d.sync.my_seq_num, "Outgoing counter disagrees with persisted history");
    validateAck(d.sync.dht_acked_seq_nums, d.sync.my_seq_num, "DHT ACK");
    validateAck(d.sync.peer_acked_seq_nums, d.sync.my_seq_num, "Peer ACK");
    validateAck(d.sync.downloaded_seq_nums, d.sync.announced_seq_num, "Downloaded ACK");
    int64_t previous = 0;
    for (auto seq : d.sync.resynced_seq_nums)
    {
        require(seq > previous && seq <= d.sync.my_seq_num, "Invalid resynced sequence");
        previous = seq;
    }
}
inline int64_t resyncParent(const ChatMessage &m)
{
    static const std::regex expression(R"(^\[Resync of Seq:([0-9]+) )");
    std::smatch match;
    return std::regex_search(m.content.str(), match, expression) ? std::stoll(match[1]) : 0;
}
// Independent expected label. Production resolver is exercised separately by contracts.cpp.
inline std::string expectedStatus(const LobbyData &d, const ChatMessage &m)
{
    if (m.direction != MessageDirection::OUT)
        return "";
    bool peer = d.sync.peer_acked_seq_nums.acknowledges(m.seq_num), copied = false, copySent = false;
    for (const auto &candidate : d.history.materialize())
    {
        if (candidate.direction == MessageDirection::OUT && resyncParent(candidate) == m.seq_num)
        {
            copied = true;
            peer = peer || d.sync.peer_acked_seq_nums.acknowledges(candidate.seq_num);
            copySent = copySent || d.sync.dht_acked_seq_nums.acknowledges(candidate.seq_num);
        }
    }
    auto parent = resyncParent(m);
    if (parent)
        peer = peer || d.sync.peer_acked_seq_nums.acknowledges(parent);
    bool resynced = copied || std::binary_search(d.sync.resynced_seq_nums.begin(), d.sync.resynced_seq_nums.end(), m.seq_num);
    if (peer)
        return resynced ? "[Ack (via resync)]" : "[Ack]";
    if (resynced)
        return copySent ? "[Sent (via resync)]" : "[Sending resync...]";
    return d.sync.dht_acked_seq_nums.acknowledges(m.seq_num) ? "[Sent]" : "[Sending...]";
}
inline ChatMessage findMessage(const LobbyData &d, const std::string &token, MessageDirection direction)
{
    std::vector<ChatMessage> matches;
    for (const auto &m : d.history.materialize())
        if (m.content == token && m.direction == direction)
            matches.push_back(m);
    require(matches.size() == 1, "Missing/duplicate persisted message: " + token);
    return matches.front();
}
inline void checkScreen(const LobbyData &d, const std::string &screen, const std::string &token, MessageDirection direction)
{
    auto m = findMessage(d, token, direction);
    std::istringstream in(screen);
    std::string line;
    int count = 0;
    auto seq = "| Seq:" + std::to_string(m.seq_num) + "]";
    auto suffix = token + (direction == MessageDirection::OUT ? " " + expectedStatus(d, m) : "");
    while (std::getline(in, line))
    {
        if (line.find(seq) != std::string::npos && line.find(": " + token) != std::string::npos)
        {
            ++count;
            auto end = line.find_last_not_of(' ');
            if (end != std::string::npos)
                line.resize(end + 1);
            require(line.ends_with(suffix), "UI/JSON status mismatch for " + token + ": " + line);
        }
    }
    require(count == 1, "Missing/duplicate screen row for " + token + " " + seq);
}
// Logs are asynchronous diagnostics: callers must retry with fresh file contents
// after validating durable delivery, rather than treating one snapshot as complete.
inline void checkDeliveryLogs(const std::string& sender, const std::string& receiver,
                              const std::string& channel, std::int64_t sequence)
{
    auto hasLine = [](const std::string& text, const std::vector<std::string>& terms) {
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line))
            if (std::all_of(terms.begin(), terms.end(), [&](const auto& term) {
                return line.find(term) != std::string::npos;
            })) return true;
        return false;
    };
    const auto id = "lobby=" + LogEvents::lobbyId(channel);
    const auto seq = "seq=" + std::to_string(sequence) + " ";
    require(hasLine(sender, {"[MSG-OUT]", id, seq, "Message persisted"}) &&
            hasLine(receiver, {"[MSG-IN]", id, seq, "Message persisted"}), "Missing correlated transaction logs");
    bool acknowledged = false;
    std::istringstream lines(sender);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find("[ACK]") == std::string::npos || line.find(id) == std::string::npos) continue;
        const auto begin = line.find(" ack=");
        const auto end = line.find(" | Peer receipts persisted", begin);
        if (begin == std::string::npos || end == std::string::npos) continue;
        const auto ack = json::parse(line.substr(begin + 5, end - begin - 5)).get<AckState>();
        acknowledged = acknowledged || ack.acknowledges(sequence);
    }
    require(acknowledged, "Missing correlated peer ACK log");
}

inline json analyzeLogs(const std::string &raw)
{
    static const std::regex ansi("\x1b\\[[0-?]*[ -/]*[@-~]");
    auto clean = std::regex_replace(raw, ansi, "");
    json result = {{"errors", json::array()}, {"warnings", json::array()}, {"transient_dht_failures", json::array()}};
    std::istringstream input(clean);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.find("DHT put failed (0 nodes accepted it)") != std::string::npos ||
            ((line.find("DHT PUT completed") != std::string::npos || line.find("Publication completed:") != std::string::npos) && line.find("successful_nodes=0") != std::string::npos))
            result["transient_dht_failures"].push_back(line);
        else if (line.find("[error]") != std::string::npos || line.find("[critical]") != std::string::npos || line.find("AddressSanitizer") != std::string::npos || line.find("runtime error:") != std::string::npos || line.find("terminate called") != std::string::npos)
            result["errors"].push_back(line);
        else if (line.find("[warning]") != std::string::npos || line.find("[warn]") != std::string::npos)
            result["warnings"].push_back(line);
    }
    return result;
}
} // namespace regression
