#pragma once

#include <mutex>
#include <openssl/evp.h>
#include <stdexcept>
#include <string>
#include <vector>

class Hasher {
private:
  Hasher() {
    m_context = EVP_MD_CTX_new();
    if (m_context == nullptr) {
      throw std::runtime_error("Failed to create EVP_MD_CTX");
    }
  }

  ~Hasher() {
    if (m_context != nullptr) {
      EVP_MD_CTX_free(m_context);
    }
  }

  EVP_MD_CTX *m_context;
  std::mutex m_mutex;

public:
  static Hasher &getInstance() {
    static Hasher instance;
    return instance;
  }

  Hasher(const Hasher &) = delete;
  Hasher &operator=(const Hasher &) = delete;

  // Computes the SHA-256 hash of the input message
  // Returns a 32-byte vector representing the hash
  inline std::vector<unsigned char> getHashedMsg(const std::string &rawMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (EVP_DigestInit_ex(m_context, EVP_sha256(), nullptr) != 1) {
      throw std::runtime_error("Failed to initialize SHA-256 digest");
    }

    if (EVP_DigestUpdate(m_context, rawMsg.c_str(), rawMsg.length()) != 1) {
      throw std::runtime_error("Failed to update SHA-256 digest");
    }

    std::vector<unsigned char> hash(EVP_MD_size(EVP_sha256()));
    unsigned int lengthOfHash = 0;

    if (EVP_DigestFinal_ex(m_context, hash.data(), &lengthOfHash) != 1) {
      throw std::runtime_error("Failed to finalize SHA-256 digest");
    }

    hash.resize(lengthOfHash);
    return hash;
  }
};
