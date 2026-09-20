#include "crypto/CryptoUtils.h"
#include <cstring>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
#include "lobby/LobbyData.h"
#include "lobby/LobbyStore.h"
#include "crypto/Hasher.h"
#include "utils/Util.h"
#include <spdlog/spdlog.h>

namespace CryptoUtils
{

using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

std::expected<std::vector<unsigned char>, std::string> encryptAES256GCM(const std::vector<unsigned char> &plaintext, const std::vector<unsigned char> &key)
{
    if (key.size() != 32)
    {
        return std::unexpected("AES-256 key must be exactly 32 bytes");
    }

    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx)
        return std::unexpected("Failed to create EVP_CIPHER_CTX");

    std::vector<unsigned char> iv(12);
    if (RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1)
    {
        return std::unexpected("Failed to generate random IV");
    }

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
    {
        return std::unexpected("Failed to init encryption");
    }

    if (EVP_EncryptInit_ex(ctx.get(), NULL, NULL, key.data(), iv.data()) != 1)
    {
        return std::unexpected("Failed to set key and IV");
    }

    std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int ciphertext_len = 0;

    if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1)
    {
        return std::unexpected("Failed to encrypt data");
    }
    ciphertext_len = len;

    if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + len, &len) != 1)
    {
        return std::unexpected("Failed to finalize encryption");
    }
    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);

    std::vector<unsigned char> tag(16);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1)
    {
        return std::unexpected("Failed to get GCM tag");
    }

    std::vector<unsigned char> result;
    result.reserve(iv.size() + ciphertext.size() + tag.size());
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());
    result.insert(result.end(), tag.begin(), tag.end());

    return result;
}

std::expected<std::vector<unsigned char>, std::string> decryptAES256GCM(const std::vector<unsigned char> &ciphertext_blob, const std::vector<unsigned char> &key)
{
    if (key.size() != 32)
    {
        return std::unexpected("AES-256 key must be exactly 32 bytes");
    }
    if (ciphertext_blob.size() < 12 + 16)
    {
        return std::unexpected("Ciphertext blob is too small");
    }

    std::vector<unsigned char> iv(ciphertext_blob.begin(), ciphertext_blob.begin() + 12);
    std::vector<unsigned char> tag(ciphertext_blob.end() - 16, ciphertext_blob.end());
    std::vector<unsigned char> ciphertext(ciphertext_blob.begin() + 12, ciphertext_blob.end() - 16);

    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx)
        return std::unexpected("Failed to create EVP_CIPHER_CTX");

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
    {
        return std::unexpected("Failed to init decryption");
    }

    if (EVP_DecryptInit_ex(ctx.get(), NULL, NULL, key.data(), iv.data()) != 1)
    {
        return std::unexpected("Failed to set key and IV");
    }

    std::vector<unsigned char> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintext_len = 0;

    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1)
    {
        return std::unexpected("Failed to decrypt data");
    }
    plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, tag.data()) != 1)
    {
        return std::unexpected("Failed to set GCM tag");
    }

    int ret = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len);

    if (ret > 0)
    {
        plaintext_len += len;
        plaintext.resize(plaintext_len);
        return plaintext;
    }
    else
    {
        return std::unexpected("Decryption failed (auth tag mismatch)");
    }
}

std::expected<X25519KeyPair, std::string> generateX25519KeyPair()
{
    PkeyCtxPtr pctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL), EVP_PKEY_CTX_free);
    if (!pctx)
        return std::unexpected("Failed to create X25519 context");

    if (EVP_PKEY_keygen_init(pctx.get()) <= 0)
    {
        return std::unexpected("Failed to init X25519 keygen");
    }

    EVP_PKEY *rawPkey = NULL;
    if (EVP_PKEY_keygen(pctx.get(), &rawPkey) <= 0)
    {
        return std::unexpected("Failed to generate X25519 key");
    }
    PkeyPtr pkey(rawPkey, EVP_PKEY_free);

    size_t pub_len = 32, priv_len = 32;
    std::vector<unsigned char> pub_key(pub_len);
    std::vector<unsigned char> priv_key(priv_len);

    if (EVP_PKEY_get_raw_public_key(pkey.get(), pub_key.data(), &pub_len) <= 0 || EVP_PKEY_get_raw_private_key(pkey.get(), priv_key.data(), &priv_len) <= 0)
    {
        return std::unexpected("Failed to extract raw X25519 keys");
    }

    SPDLOG_INFO("generateX25519KeyPair: Raw generated PubKey: {}", Util::toHex(pub_key));
    return X25519KeyPair{pub_key, priv_key};
}
std::expected<std::vector<unsigned char>, std::string> deriveX25519SharedSecret(const std::vector<unsigned char> &myPrivateKey, const std::vector<unsigned char> &peerPublicKey)
{
    if (myPrivateKey.size() != 32 || peerPublicKey.size() != 32)
    {
        return std::unexpected("X25519 keys must be exactly 32 bytes");
    }

    PkeyPtr myPrivKey(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, myPrivateKey.data(), myPrivateKey.size()), EVP_PKEY_free);
    if (!myPrivKey)
        return std::unexpected("Failed to load X25519 private key");

    PkeyPtr peerPubKey(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peerPublicKey.data(), peerPublicKey.size()), EVP_PKEY_free);
    if (!peerPubKey)
    {
        return std::unexpected("Failed to load X25519 peer public key");
    }

    PkeyCtxPtr ctx(EVP_PKEY_CTX_new(myPrivKey.get(), NULL), EVP_PKEY_CTX_free);
    if (!ctx)
    {
        return std::unexpected("Failed to create derivation context");
    }

    if (EVP_PKEY_derive_init(ctx.get()) <= 0)
    {
        return std::unexpected("Failed to init derivation");
    }

    if (EVP_PKEY_derive_set_peer(ctx.get(), peerPubKey.get()) <= 0)
    {
        return std::unexpected("Failed to set peer key for derivation");
    }

    size_t secretLen = 0;
    if (EVP_PKEY_derive(ctx.get(), NULL, &secretLen) <= 0)
    {
        return std::unexpected("Failed to determine shared secret length");
    }

    std::vector<unsigned char> sharedSecret(secretLen);
    if (EVP_PKEY_derive(ctx.get(), sharedSecret.data(), &secretLen) <= 0)
    {
        return std::unexpected("Failed to derive shared secret");
    }
    sharedSecret.resize(secretLen);

    return sharedSecret;
}

} // namespace CryptoUtils
