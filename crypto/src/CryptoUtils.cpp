#include "CryptoUtils.h"

#include <array>
#include <memory>
#include <span>

#include <argon2.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>

#include "SecureBuffer.h"
#include "Util.h"

namespace CryptoUtils
{

using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

std::expected<std::vector<unsigned char>, std::string> encryptAES256GCM(std::span<const unsigned char> plaintext, std::span<const unsigned char> key, std::span<const unsigned char> aad)
{
    if (key.size() != 32)
    {
        return std::unexpected("AES-256 key must be exactly 32 bytes");
    }

    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx)
        return std::unexpected("Failed to create EVP_CIPHER_CTX");

    constexpr std::size_t ivLen = 12;
    constexpr std::size_t tagLen = 16;
    std::vector<unsigned char> result(ivLen + plaintext.size() + tagLen);

    if (RAND_bytes(result.data(), static_cast<int>(ivLen)) != 1)
    {
        return std::unexpected("Failed to generate random IV");
    }

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    {
        return std::unexpected("Failed to init encryption");
    }

    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), result.data()) != 1)
    {
        return std::unexpected("Failed to set key and IV");
    }

    if (!aad.empty())
    {
        int len = 0;
        if (EVP_EncryptUpdate(ctx.get(), nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1)
        {
            return std::unexpected("Failed to set AAD");
        }
    }

    unsigned char *ciphertextPtr = result.data() + ivLen;
    int len = 0;
    if (EVP_EncryptUpdate(ctx.get(), ciphertextPtr, &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1)
    {
        return std::unexpected("Failed to encrypt data");
    }
    int ciphertextLen = len;

    if (EVP_EncryptFinal_ex(ctx.get(), ciphertextPtr + ciphertextLen, &len) != 1)
    {
        return std::unexpected("Failed to finalize encryption");
    }
    ciphertextLen += len;

    unsigned char *tagPtr = ciphertextPtr + ciphertextLen;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tagLen), tagPtr) != 1)
    {
        return std::unexpected("Failed to get GCM tag");
    }

    result.resize(ivLen + static_cast<std::size_t>(ciphertextLen) + tagLen);
    return result;
}

std::expected<std::vector<unsigned char>, std::string> decryptAES256GCM(std::span<const unsigned char> ciphertext_blob, std::span<const unsigned char> key, std::span<const unsigned char> aad)
{
    if (key.size() != 32)
    {
        return std::unexpected("AES-256 key must be exactly 32 bytes");
    }
    if (ciphertext_blob.size() < 12 + 16)
    {
        return std::unexpected("Ciphertext blob is too small");
    }

    const auto iv = ciphertext_blob.first(12);
    std::array<unsigned char, 16> tag{};
    std::copy(ciphertext_blob.end() - 16, ciphertext_blob.end(), tag.begin());
    const auto ciphertext = ciphertext_blob.subspan(12, ciphertext_blob.size() - 28);

    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx)
        return std::unexpected("Failed to create EVP_CIPHER_CTX");

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    {
        return std::unexpected("Failed to init decryption");
    }

    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1)
    {
        return std::unexpected("Failed to set key and IV");
    }

    if (!aad.empty())
    {
        int len = 0;
        if (EVP_DecryptUpdate(ctx.get(), nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1)
        {
            return std::unexpected("Failed to set AAD");
        }
    }

    std::vector<unsigned char> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int plaintext_len = 0;

    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1)
    {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        return std::unexpected("Failed to decrypt data");
    }
    plaintext_len = len;

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, tag.data()) != 1)
    {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        return std::unexpected("Failed to set GCM tag");
    }

    int ret = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + len, &len);

    if (ret > 0)
    {
        plaintext_len += len;
        plaintext.resize(static_cast<std::size_t>(plaintext_len));
        return plaintext;
    }
    else
    {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        return std::unexpected("Decryption failed (auth tag mismatch)");
    }
}

std::vector<unsigned char> deriveStoreKey(std::string_view password, std::span<const unsigned char> salt)
{
    constexpr std::uint32_t passes = 3;
    constexpr std::uint32_t memoryKiB = 64 * 1024;
    std::vector<unsigned char> output(32);

    int rc = argon2id_hash_raw(
        passes, memoryKiB, 1,
        password.data(), password.size(),
        salt.data(), salt.size(),
        output.data(), output.size());

    if (rc != ARGON2_OK)
    {
        OPENSSL_cleanse(output.data(), output.size());
        throw std::runtime_error(std::string("Argon2id failed: ") + argon2_error_message(rc));
    }
    return output;
}

std::expected<X25519KeyPair, std::string> generateX25519KeyPair()
{
    PkeyCtxPtr pctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), EVP_PKEY_CTX_free);
    if (!pctx)
        return std::unexpected("Failed to create X25519 context");

    if (EVP_PKEY_keygen_init(pctx.get()) <= 0)
    {
        return std::unexpected("Failed to init X25519 keygen");
    }

    EVP_PKEY *rawPkey = nullptr;
    if (EVP_PKEY_keygen(pctx.get(), &rawPkey) <= 0)
    {
        return std::unexpected("Failed to generate X25519 key");
    }
    PkeyPtr pkey(rawPkey, EVP_PKEY_free);

    size_t pub_len = 32, priv_len = 32;
    X25519KeyPair keyPair{std::vector<unsigned char>(pub_len), std::vector<unsigned char>(priv_len)};

    if (EVP_PKEY_get_raw_public_key(pkey.get(), keyPair.publicKey.data(), &pub_len) <= 0 ||
        EVP_PKEY_get_raw_private_key(pkey.get(), keyPair.privateKey.data(), &priv_len) <= 0)
    {
        OPENSSL_cleanse(keyPair.privateKey.data(), keyPair.privateKey.size());
        return std::unexpected("Failed to extract raw X25519 keys");
    }

    SPDLOG_INFO("[HANDSHAKE] generateX25519KeyPair: Raw generated PubKey: {}", Util::toHex(keyPair.publicKey));
    return keyPair;
}

std::expected<std::vector<unsigned char>, std::string> deriveX25519SharedSecret(std::span<const unsigned char> myPrivateKey, std::span<const unsigned char> peerPublicKey)
{
    if (myPrivateKey.size() != 32 || peerPublicKey.size() != 32)
    {
        return std::unexpected("X25519 keys must be exactly 32 bytes");
    }

    PkeyPtr myPrivKey(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, myPrivateKey.data(), myPrivateKey.size()), EVP_PKEY_free);
    if (!myPrivKey)
        return std::unexpected("Failed to load X25519 private key");

    PkeyPtr peerPubKey(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peerPublicKey.data(), peerPublicKey.size()), EVP_PKEY_free);
    if (!peerPubKey)
    {
        return std::unexpected("Failed to load X25519 peer public key");
    }

    PkeyCtxPtr ctx(EVP_PKEY_CTX_new(myPrivKey.get(), nullptr), EVP_PKEY_CTX_free);
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
    if (EVP_PKEY_derive(ctx.get(), nullptr, &secretLen) <= 0)
    {
        return std::unexpected("Failed to determine shared secret length");
    }

    std::vector<unsigned char> sharedSecret(secretLen);
    if (EVP_PKEY_derive(ctx.get(), sharedSecret.data(), &secretLen) <= 0)
    {
        OPENSSL_cleanse(sharedSecret.data(), sharedSecret.size());
        return std::unexpected("Failed to derive shared secret");
    }
    sharedSecret.resize(secretLen);

    return sharedSecret;
}

std::expected<std::vector<unsigned char>, std::string> deriveHKDF(std::span<const unsigned char> ikm, std::span<const unsigned char> salt, std::string_view info, size_t outLen)
{
    PkeyCtxPtr pctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr), EVP_PKEY_CTX_free);
    if (!pctx) return std::unexpected("Failed to create HKDF context");

    if (EVP_PKEY_derive_init(pctx.get()) <= 0)
        return std::unexpected("Failed to init HKDF");

    if (EVP_PKEY_CTX_set_hkdf_mode(pctx.get(), EVP_PKEY_HKDEF_MODE_EXTRACT_AND_EXPAND) <= 0)
        return std::unexpected("Failed to set HKDF mode");

    if (EVP_PKEY_CTX_set_hkdf_md(pctx.get(), EVP_sha256()) <= 0)
        return std::unexpected("Failed to set HKDF digest");

    if (EVP_PKEY_CTX_set1_hkdf_key(pctx.get(), ikm.data(), static_cast<int>(ikm.size())) <= 0)
        return std::unexpected("Failed to set HKDF key");

    if (!salt.empty())
    {
        if (EVP_PKEY_CTX_set1_hkdf_salt(pctx.get(), salt.data(), static_cast<int>(salt.size())) <= 0)
            return std::unexpected("Failed to set HKDF salt");
    }

    if (!info.empty())
    {
        if (EVP_PKEY_CTX_add1_hkdf_info(pctx.get(), reinterpret_cast<const unsigned char*>(info.data()), static_cast<int>(info.size())) <= 0)
            return std::unexpected("Failed to set HKDF info");
    }

    std::vector<unsigned char> out(outLen);
    size_t len = outLen;
    if (EVP_PKEY_derive(pctx.get(), out.data(), &len) <= 0)
    {
        OPENSSL_cleanse(out.data(), out.size());
        return std::unexpected("Failed to derive HKDF key");
    }

    out.resize(len);
    return out;
}

} // namespace CryptoUtils
