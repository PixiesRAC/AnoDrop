#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/kademlia/item.hpp>
#include <openssl/crypto.h>
#include <spdlog/spdlog.h>
#include "DhtLog.h"

namespace network {

class Bep44Signer
{
  public:
    [[nodiscard]] static constexpr std::size_t decimalDigits(std::size_t n) noexcept
    {
        std::size_t digits = 1;
        while (n >= 10)
        {
            n /= 10;
            ++digits;
        }
        return digits;
    }

    [[nodiscard]] static constexpr std::size_t bencodedStringSize(std::size_t payloadLen) noexcept
    {
        return decimalDigits(payloadLen) + 1 + payloadLen;
    }

    static void signAndPackMutableItem(
        const std::string &payload,
        std::int64_t seqNum,
        const std::array<char, 32> &publicKey,
        const std::array<char, 64> &privateKey,
        lt::entry &dhtMutableItem,
        std::array<char, 64> &dhtSignature,
        std::int64_t &dhtSequenceNumber,
        const std::string &dhtSalt, const std::string &logRole = "mutable_item")
    {
        dhtMutableItem = payload;
        dhtSequenceNumber = seqNum;

        const std::size_t expectedBencodeSize = bencodedStringSize(payload.size());
        std::vector<char> bencodedItem;
        bencodedItem.reserve(expectedBencodeSize);
        lt::bencode(std::back_inserter(bencodedItem), dhtMutableItem);

        lt::dht::secret_key secretKey(privateKey.data());
        lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(
            bencodedItem, dhtSalt, lt::dht::sequence_number(dhtSequenceNumber),
            lt::dht::public_key(publicKey.data()), secretKey);
        OPENSSL_cleanse(secretKey.bytes.data(), secretKey.bytes.size());

        dhtSignature = generatedSignature.bytes;
        SPDLOG_DEBUG("[DHT] Sign {} | target_sha1={} public_key_ed25519={} salt_hex={} dht_seq={} signature_hex={} value_bytes={}",
                    logRole, logging::target(publicKey, dhtSalt), Util::toHex(publicKey), Util::toHex(dhtSalt),
                    dhtSequenceNumber, Util::toHex(dhtSignature), payload.size());
    }
};

} // namespace network
