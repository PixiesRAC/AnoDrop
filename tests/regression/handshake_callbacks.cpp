#include <filesystem>
#include <sstream>
#include <spdlog/sinks/ostream_sink.h>
#include "HandshakeKeyService.h"
#include "HandshakePreparation.h"
#include "LobbyStoreTestAccess.h"

#include "MutableItemCallbacks.h"
#include "HandshakePayloadCodec.h"
#include "LobbyDataSerializer.h"
using network::detail::signAndPackMutableItem;
using network::detail::put_mutable_callback_handshake;
using network::detail::put_mutable_callback_anomaly;
void check(bool condition, const std::string &message);

namespace {
struct CapturedLogs {
    std::ostringstream output;
    std::shared_ptr<spdlog::logger> previous = spdlog::default_logger();
    CapturedLogs() {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
        auto logger = std::make_shared<spdlog::logger>("handshake-log-test", sink);
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger);
    }
    ~CapturedLogs() { spdlog::set_default_logger(previous); }
};

std::string prepare(const std::shared_ptr<LobbyStoreTestAccess>& store, const std::string& channel, const std::string& name)
{
    auto payload = HandshakePreparation::prepareLobbyPayload(*store, channel, name);
    check(payload.has_value(), "Cannot prepare handshake");
    return {payload->begin(), payload->end()};
}
}

void handshakeSequenceContract(const std::filesystem::path &root, bool locallyReady, bool finalized)
{
    CapturedLogs logs;
    const std::string channel = "handshake-sequence-contract", salt = "regression-salt";
    auto alice = std::make_shared<LobbyStoreTestAccess>("pw", (root / "Alice").string());
    auto bob = std::make_shared<LobbyStoreTestAccess>("pw", (root / "Bob").string());
    const auto ownA = prepare(alice, channel, "Alice"), ownB = prepare(bob, channel, "Bob");
    auto initialA = alice->getLobby(channel)->getLobbyData();
    auto initialB = bob->getLobby(channel)->getLobbyData();
    auto first = HandshakeStateEngine::processHandshakeTransition(ownA, initialA, 1, ownB, true).value();
    check(first.shouldWrite && !first.established && first.update.status == LobbyStatus::CONFIRMATION_PENDING,
          "Handshake completed without reciprocal confirmation");
    alice->commit(channel, first.update);
    auto second = HandshakeStateEngine::processHandshakeTransition(ownB, initialB,
        first.targetSequenceNumber, first.payloadToWrite, true).value();
    check(second.shouldWrite && second.established && second.targetSequenceNumber == INT64_MAX - 1,
          "Reciprocal confirmation did not finalize transcript");
    bob->commit(channel, second.update);
    auto local = locallyReady ? alice->getLobby(channel)->getLobbyData() : initialA;
    if (locallyReady) local.metadata.status = LobbyStatus::HANDSHAKE_DONE;
    auto completed = HandshakeStateEngine::processHandshakeTransition(ownA, local,
        second.targetSequenceNumber, second.payloadToWrite, true).value();
    check(completed.established && !completed.shouldWrite && completed.update.status == LobbyStatus::HANDSHAKE_DONE,
          "Late finalized transcript was not adopted");
    alice->commit(channel, completed.update);
    auto a = alice->getLobby(channel)->getLobbyData(), b = bob->getLobby(channel)->getLobbyData();
    check(a.metadata.other_pseudo == "Bob" && a.crypto->private_node_discussion == b.crypto->private_node_discussion,
          "Peer identity or shared secret mismatch");
    check(a.metadata.fake_timezone == b.metadata.fake_timezone, "Timezone agreement failed");

    HandshakeKeyService::ChannelKeys keys(std::vector<unsigned char>(32, 1));
    auto signingKey = std::make_shared<SigningKey>(keys.privateKey);
    lt::entry item;
    std::array<char, 64> signature{};
    std::int64_t sequence = 0;
    signAndPackMutableItem(first.payloadToWrite, finalized ? INT64_MAX - 1 : 7,
                          keys.publicKey, signingKey, item, signature, sequence, salt);
    auto previousPayload = item.string();
    auto previousSignature = signature;
    put_mutable_callback_handshake(second.payloadToWrite, INT64_MAX - 1, keys.publicKey,
                                  signingKey, item, signature, sequence, salt);
    check(sequence == INT64_MAX - 1, "Handshake target sequence lost");
    if (finalized)
        check(item.string() == previousPayload && signature == previousSignature, "Late callback overwrote finalized item");
    else
        check(item.string() == second.payloadToWrite, "Callback did not publish prepared transcript");
    previousPayload = item.string();
    previousSignature = signature;
    put_mutable_callback_handshake(ownA, INT64_MAX - 1, keys.publicKey, signingKey, item, signature, sequence, salt);
    check(item.string() == previousPayload && signature == previousSignature, "Repeated callback modified final item");
    put_mutable_callback_anomaly("anomaly", keys.publicKey, signingKey, item, signature, sequence, salt);
    previousPayload = item.string();
    previousSignature = signature;
    put_mutable_callback_handshake(ownA, INT64_MAX - 1, keys.publicKey, signingKey, item, signature, sequence, salt);
    check(sequence == INT64_MAX && item.string() == previousPayload && signature == previousSignature,
          "Late handshake overwrote anomaly");
    const std::string privateMessage = "PRIVATE-MESSAGE-MUST-NOT-BE-LOGGED";
    LobbyUpdate messageUpdate;
    messageUpdate.new_message = ChatMessage{MessageDirection::OUT, privateMessage, 123, 1};
    messageUpdate.my_seq_num = 1;
    alice->commit(channel, messageUpdate);
    const auto captured = logs.output.str();
    check(captured.find(privateMessage) == std::string::npos, "Message content leaked into logs");
    check(captured.find("lobby=\"" + channel + "\"") != std::string::npos, "Missing readable lobby name in message log");
    for (const auto &secret : {std::string(a.crypto->my_x25519_priv), std::string(b.crypto->my_x25519_priv),
                              std::string(a.crypto->private_node_discussion), Util::toHex(keys.privateKey)}) {
        check(!secret.empty() && captured.find(secret) == std::string::npos, "Secret leaked into handshake logs");
    }
    check(captured.find("target_sha1=" + network::logging::target(keys.publicKey, salt)) != std::string::npos,
          "Missing wire target in handshake logs");
    check(captured.find("public_key_ed25519=" + keys.pubKeyHex) != std::string::npos,
          "Missing signing public key in handshake logs");
    check(network::logging::sha1("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "Invalid logged SHA-1");

}

void handshakeRejectionContract(const std::filesystem::path& root)
{
    const std::string channel = "channel";
    auto store = std::make_shared<LobbyStoreTestAccess>("pw", root.string());
    auto own = prepare(store, channel, "Alice");
    auto local = store->getLobby(channel)->getLobbyData();
    auto self = HandshakeStateEngine::processHandshakeTransition(own, local, 1, own, true).value();
    check(!self.established && !self.update.private_node_discussion, "Own key accepted as peer");
    auto reject = [&](const nlohmann::json& value) {
        auto payload = HandshakePayloadCodec::encryptDhtHandshakePayload(value, channel).value();
        const auto result = HandshakeStateEngine::processHandshakeTransition(own, local, 1,
            std::string(payload.begin(), payload.end()), true);
        check(!result && !result.error().empty(), "Invalid handshake did not return a diagnostic");
    };
    auto peer = HandshakePayloadCodec::decryptDhtHandShakePayload(own, channel).value().at(0);
    auto malformed = peer;
    malformed.erase("protocol_version");
    reject(nlohmann::json::array({malformed}));
    malformed = peer;
    malformed["protocol_version"] = "not an integer";
    reject(nlohmann::json::array({malformed}));
    peer["x25519_pub"] = "not-a-valid-key";
    reject(nlohmann::json::array({peer}));
    reject(nlohmann::json::object());
    check(!HandshakePayloadCodec::decryptDhtHandShakePayload(own, "wrong-channel"), "Wrong channel accepted");
    own.back() ^= 1;
    check(!HandshakePayloadCodec::decryptDhtHandShakePayload(own, channel), "Tampered handshake accepted");
}
