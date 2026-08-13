#include "core/DhtPublisher.h"
#include "crypto/HandShakeChannel.h"
#include "crypto/Hasher.h"
#include "network/Dropper.h"
#include "utils/Util.h"

#include <libtorrent/hasher.hpp>
#include <spdlog/spdlog.h>

void DhtPublisher::setDhtManager(std::unique_ptr<DhtManager> dhtManager) {
  m_dhtManager = std::move(dhtManager);
}

DhtPublisher::RendezvousInfo DhtPublisher::initializeRendezvous(const std::string& passphrase) {
  std::vector<unsigned char> seed = Hasher::getInstance().getHashedMsg(passphrase);
  
  HandShakeChannel channel(seed);
  m_pubKey = channel.getPublicKey();
  m_privKey = channel.getPrivateKey();

  lt::hasher h(m_pubKey.data(), m_pubKey.size());
  lt::sha1_hash targetID = h.final();

  DhtPublisher::RendezvousInfo info;
  info.pubKeyHex = Util::toHex(m_pubKey);
  info.targetIDHex = Util::toHex(targetID.to_string());

  spdlog::info("Public Key (Hex): {}", info.pubKeyHex);
  spdlog::info("Target ID (SHA-1): {}", info.targetIDHex);

  return info;
}

void DhtPublisher::dispatchMessage(const std::string& message) {
  if (!m_dhtManager) {
    throw std::runtime_error("DHT Manager is not initialized");
  }

  Dropper dropper(m_dhtManager->getSession(), m_pubKey, m_privKey);
  dropper.dropMessage(message);
}

void DhtPublisher::waitForPutConfirmed() {
  if (m_dhtManager) {
    m_dhtManager->waitForPutConfirmed();
  }
}
