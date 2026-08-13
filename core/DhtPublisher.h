#pragma once

#include "network/DhtManager.h"

#include <memory>
#include <string>

class DhtPublisher {
public:
  DhtPublisher() = default;
  ~DhtPublisher() = default;

  void setDhtManager(std::unique_ptr<DhtManager> dhtManager);
  
  struct RendezvousInfo {
    std::string pubKeyHex;
    std::string targetIDHex;
  };

  // Setup cryptographic keys and return info for UI
  RendezvousInfo initializeRendezvous(const std::string& passphrase);
  
  // Dispatch a message to the DHT network
  void dispatchMessage(const std::string& message);
  
  // Wait for the DHT network to confirm the put operation
  void waitForPutConfirmed();

private:
  std::unique_ptr<DhtManager> m_dhtManager;
  // State from initialization needed for dispatching messages
  std::array<char, 32> m_pubKey;
  std::array<char, 64> m_privKey;
};
