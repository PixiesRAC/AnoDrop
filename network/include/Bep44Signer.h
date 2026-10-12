#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/kademlia/item.hpp>

namespace network {

class Bep44Signer
{
  public:
    static void signAndPackMutableItem(
        const std::string &payload,
        std::int64_t seqNum,
        const std::array<char, 32> &publicKey,
        const std::array<char, 64> &privateKey,
        lt::entry &dhtMutableItem,
        std::array<char, 64> &dhtSignature,
        std::int64_t &dhtSequenceNumber,
        const std::string &dhtSalt)
    {
        dhtMutableItem = payload;
        dhtSequenceNumber = seqNum;

        size_t expectedBencodeSize = std::to_string(payload.size()).length() + 1 + payload.size();
        std::vector<char> bencodedItem;
        bencodedItem.reserve(expectedBencodeSize);
        lt::bencode(std::back_inserter(bencodedItem), dhtMutableItem);

        lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(
            bencodedItem, dhtSalt, lt::dht::sequence_number(dhtSequenceNumber),
            lt::dht::public_key(publicKey.data()), lt::dht::secret_key(privateKey.data()));

        dhtSignature = generatedSignature.bytes;
    }
};

} // namespace network
