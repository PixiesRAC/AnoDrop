#pragma once
#include "Bep44Signer.h"
#include "SigningKey.h"
#include <memory>

namespace network::detail {
inline void signAndPackMutableItem(const std::string &payload, std::int64_t seqNum, const std::array<char, 32> &publicKey, const std::shared_ptr<SigningKey> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, const std::string &dhtSalt, const std::string &logRole = "mutable_item")
{
    network::Bep44Signer::signAndPackMutableItem(payload, seqNum, publicKey, privateKey->bytes, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt, logRole);
}

inline void put_mutable_callback_handshake(const std::string &payload, std::int64_t target,
    const std::array<char, 32>& publicKey, const std::shared_ptr<SigningKey>& privateKey,
    lt::entry& value, std::array<char, 64>& signature, std::int64_t& sequence, const std::string& salt)
{
    // No application state or storage on the libtorrent thread.
    if (sequence >= target) {
        SPDLOG_INFO("[HANDSHAKE] Handshake PUT skipped target_sha1={} observed_seq={} requested_seq={} reason=sequence_not_newer",
                    logging::target(publicKey, salt), sequence, target);
        return;
    }
    network::Bep44Signer::signAndPackMutableItem(payload, target, publicKey, privateKey->bytes,
                                                value, signature, sequence, salt, "handshake");
}

inline void put_mutable_callback_anomaly(const std::string &finalEncryptedPayload, const std::array<char, 32> &publicKey, const std::shared_ptr<SigningKey> &privateKey, lt::entry &dhtMutableItem, std::array<char, 64> &dhtSignature, std::int64_t &dhtSequenceNumber, std::string const &dhtSalt)
{
    signAndPackMutableItem(finalEncryptedPayload, INT64_MAX, publicKey, privateKey, dhtMutableItem, dhtSignature, dhtSequenceNumber, dhtSalt, "anomaly"); // Force MAX for anomaly
}
} // namespace network::detail
