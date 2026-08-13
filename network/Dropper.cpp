#include "network/Dropper.h"
#include "utils/Util.h"

#include <functional>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/kademlia/item.hpp>
#include <spdlog/spdlog.h>

Dropper::Dropper(lt::session *session, const std::array<char, 32> &publicKey,
                 const std::array<char, 64> &privateKey)
    : m_session(session), m_publicKey(publicKey), m_privateKey(privateKey) {}

Dropper::~Dropper() {}

static void put_mutable_callback(const std::string &message,
                                 const std::array<char, 32> &publicKey,
                                 const std::array<char, 64> &privateKey,
                                 lt::entry &itemEntry,
                                 std::array<char, 64> &itemSignature,
                                 std::int64_t &sequenceNumber,
                                 std::string const &itemSalt) {
  if (message.size() > 1000) {
    spdlog::error("Error: Message ({} bytes) exceeds DHT limit (1000 bytes). "
                  "Aborting PUT.",
                  message.size());
    return;
  }

  // This callback is called locally by libtorrent (sometimes multiple times)
  // to prepare the signature before actually sending the PUT request to the
  // nodes.
  spdlog::info("Local DHT callback preparing to sign mutable item. (Network "
               "seq: {} -> New seq: {})",
               sequenceNumber, sequenceNumber + 1);

  itemEntry = message;
  sequenceNumber++;

  size_t expectedBencodeSize =
      std::to_string(message.size()).length() + 1 + message.size();

  std::vector<char> bencodedItem;
  bencodedItem.reserve(expectedBencodeSize);
  lt::bencode(std::back_inserter(bencodedItem), itemEntry);

  lt::dht::signature generatedSignature = lt::dht::sign_mutable_item(
      bencodedItem, itemSalt, lt::dht::sequence_number(sequenceNumber),
      lt::dht::public_key(publicKey.data()),
      lt::dht::secret_key(privateKey.data()));

  itemSignature = generatedSignature.bytes;
}

bool Dropper::dropMessage(const std::string &message) {
  if (!m_session) {
    spdlog::error("Dropper::dropMessage failed: session is null");
    return false;
  }

  spdlog::info(
      "Dropper is putting mutable item (message) into DHT raw {}, Hexa {}",
      message, Util::toHex(message));
  m_session->dht_put_item(
      m_publicKey,
      std::bind(&put_mutable_callback, message, m_publicKey, m_privateKey,
                std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3, std::placeholders::_4),
      "" // No salt
  );

  return true;
}
