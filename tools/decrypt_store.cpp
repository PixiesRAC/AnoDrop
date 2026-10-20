#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <openssl/crypto.h>

#include "CryptoUtils.h"

namespace
{

std::string formatHex(const unsigned char *data, size_t length, bool space = true)
{
    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        if (space && i + 1 < length)
            oss << ' ';
    }
    return oss.str();
}

void printHexDump(const unsigned char *data, size_t length, size_t maxDisplay = 128)
{
    const size_t displayLen = std::min(length, maxDisplay);
    for (size_t i = 0; i < displayLen; i += 16)
    {
        std::cout << "       " << std::hex << std::setw(4) << std::setfill('0') << i << " : ";
        for (size_t j = 0; j < 16; ++j)
        {
            if (i + j < displayLen)
            {
                std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i + j]) << ' ';
            }
            else
            {
                std::cout << "   ";
            }
            if (j == 7)
                std::cout << ' ';
        }
        std::cout << " | ";
        for (size_t j = 0; j < 16 && (i + j) < displayLen; ++j)
        {
            unsigned char c = data[i + j];
            std::cout << (c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
        }
        std::cout << '\n';
    }
    if (length > maxDisplay)
    {
        std::cout << "       ... [" << std::dec << (length - maxDisplay) << " additional bytes omitted]\n";
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: decrypt_store <master_password> [store_directory=store]\n";
        return 1;
    }

    const std::string masterPwd = argv[1];
    const std::filesystem::path storeDir = (argc == 3) ? argv[2] : "store";

    const auto vaultPath = storeDir / "vault.enc";
    const auto journalPath = storeDir / "transactions.v1.enc";

    if (!std::filesystem::exists(vaultPath))
    {
        std::cerr << "Error: File not found: " << vaultPath << "\n";
        return 1;
    }

    std::cout << "================================================================================\n";
    std::cout << "                  ANODROP - ENCRYPTED JOURNAL INSPECTOR                         \n";
    std::cout << "================================================================================\n";
    std::cout << "Store directory: " << storeDir << "\n\n";

    // 1. Déverrouillage de vault.enc
    std::ifstream vaultFile(vaultPath, std::ios::binary);
    if (!vaultFile)
    {
        std::cerr << "Error: Cannot open " << vaultPath << "\n";
        return 1;
    }

    std::vector<unsigned char> vaultData(
        (std::istreambuf_iterator<char>(vaultFile)),
        std::istreambuf_iterator<char>());

    std::cout << "--- [1] VAULT INSPECTION (" << vaultPath.filename().string() << ") ---\n";
    std::cout << "Vault size: " << std::dec << vaultData.size() << " bytes (expected: 76 bytes)\n";

    if (vaultData.size() != 76)
    {
        std::cerr << "Error: Invalid vault.enc size (must be exactly 76 bytes)\n";
        return 1;
    }

    const auto salt = std::vector<unsigned char>(vaultData.begin(), vaultData.begin() + 16);
    const auto encryptedDek = std::vector<unsigned char>(vaultData.begin() + 16, vaultData.end());

    std::cout << "🔒 ENCRYPTED VAULT VIEW (HEX):\n";
    std::cout << "   • Argon2id Salt (16 bytes): " << formatHex(salt.data(), salt.size()) << "\n";
    std::cout << "   • Vault IV (12 bytes)     : " << formatHex(encryptedDek.data(), 12) << "\n";
    std::cout << "   • Encrypted DEK (32 bytes): " << formatHex(encryptedDek.data() + 12, 32) << "\n";
    std::cout << "   • GCM Tag (16 bytes)      : " << formatHex(encryptedDek.data() + 44, 16) << "\n";

    std::vector<unsigned char> masterKey;
    try
    {
        auto kek = CryptoUtils::deriveStoreKey(masterPwd, salt);
        std::cout << "\n🔑 DERIVED KEK (Argon2id from password + salt):\n";
        std::cout << "   • KEK (32 bytes / 256 bits): " << formatHex(kek.data(), kek.size()) << "\n";

        auto dekRes = CryptoUtils::decryptAES256GCM(encryptedDek, kek);
        OPENSSL_cleanse(kek.data(), kek.size());

        if (!dekRes)
        {
            std::cerr << "\n❌ FAILURE: Incorrect master password or corrupted vault.\n";
            return 1;
        }
        masterKey = std::move(*dekRes);
        std::cout << "\n🔓 DECRYPTED VAULT VIEW (DEK / MASTER KEY):\n";
        std::cout << "   ✓ Master password valid! Successful DEK decryption via KEK.\n";
        std::cout << "   • DEK / MasterKey (HEX)   : " << formatHex(masterKey.data(), masterKey.size()) << "\n\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Key derivation error: " << e.what() << "\n";
        return 1;
    }

    // 2. Inspection et déchiffrement de transactions.v1.enc
    if (!std::filesystem::exists(journalPath))
    {
        std::cout << "No transactions.v1.enc journal found.\n";
        OPENSSL_cleanse(masterKey.data(), masterKey.size());
        return 0;
    }

    std::ifstream journalFile(journalPath, std::ios::binary);
    if (!journalFile)
    {
        std::cerr << "Error: Cannot open " << journalPath << "\n";
        OPENSSL_cleanse(masterKey.data(), masterKey.size());
        return 1;
    }

    std::cout << "--- [2] JOURNAL RECORD PARSING (" << journalPath.filename().string() << ") ---\n";

    size_t recordIndex = 0;
    size_t fileOffset = 0;

    while (journalFile.peek() != EOF)
    {
        unsigned char sizeBuf[4];
        if (!journalFile.read(reinterpret_cast<char *>(sizeBuf), 4))
            break;

        const uint32_t recordSize = static_cast<uint32_t>(sizeBuf[0]) |
                                    (static_cast<uint32_t>(sizeBuf[1]) << 8) |
                                    (static_cast<uint32_t>(sizeBuf[2]) << 16) |
                                    (static_cast<uint32_t>(sizeBuf[3]) << 24);

        std::vector<unsigned char> recordData(recordSize);
        if (!journalFile.read(reinterpret_cast<char *>(recordData.data()), recordSize))
        {
            std::cerr << "⚠️ Unexpected EOF (truncation detected) at offset " << fileOffset << "\n";
            break;
        }

        std::cout << "================================================================================\n";
        std::cout << "📦 TRANSACTION #" << std::dec << recordIndex
                  << "  |  File Offset: " << fileOffset << " (0x" << std::hex << fileOffset << std::dec << ")"
                  << "  |  Size: " << recordSize << " bytes\n";
        std::cout << "================================================================================\n";

        if (recordSize < 28) // 12 IV + 16 Tag
        {
            std::cerr << "❌ Record too short (minimum 28 bytes required).\n";
            fileOffset += 4 + recordSize;
            ++recordIndex;
            continue;
        }

        const unsigned char *iv = recordData.data();
        const unsigned char *tag = recordData.data() + recordSize - 16;
        const unsigned char *ciphertext = recordData.data() + 12;
        const size_t ciphertextLen = recordSize - 28;
        const std::string aad = "AnoDropJournal/v1/" + std::to_string(recordIndex);

        // A. Affichage Chiffré en Hex / Décimal
        std::cout << "🔒 1. ENCRYPTED DISK VIEW (HEX & DECIMAL):\n";
        std::cout << "   • Length Prefix (4B LE)  : " << recordSize << " bytes [ " << formatHex(sizeBuf, 4) << "]\n";
        std::cout << "   • Random IV (12B)        : [ " << formatHex(iv, 12) << "]\n";
        std::cout << "   • GCM Tag Seal (16B)     : [ " << formatHex(tag, 16) << "]\n";
        std::cout << "   • Positional AAD         : \"" << aad << "\"\n";
        std::cout << "   • Ciphertext (" << std::dec << ciphertextLen << " bytes):\n";
        printHexDump(ciphertext, ciphertextLen, 64);

        // B. Déchiffrement AES-256-GCM
        std::vector<unsigned char> aadBytes(aad.begin(), aad.end());
        auto decrypted = CryptoUtils::decryptAES256GCM(recordData, masterKey, aadBytes);

        std::cout << "\n🔓 2. DECRYPTED VIEW (PLAINTEXT PAYLOAD):\n";
        if (decrypted)
        {
            std::string jsonStr(decrypted->begin(), decrypted->end());
            try
            {
                auto j = nlohmann::json::parse(jsonStr);
                std::cout << "   ✓ Authenticity verified by AES-GCM tag!\n";
                std::cout << j.dump(2) << "\n\n";
            }
            catch (const std::exception &)
            {
                std::cout << "   ✓ Decrypted text (non-JSON): " << jsonStr << "\n\n";
            }
        }
        else
        {
            std::cout << "   ❌ AUTHENTICATION FAILED (Tag mismatch or positional alteration)!\n\n";
        }

        fileOffset += 4 + recordSize;
        ++recordIndex;
    }

    std::cout << "================================================================================\n";
    std::cout << "Total transactions parsed: " << std::dec << recordIndex << "\n";
    std::cout << "Total journal size       : " << fileOffset << " bytes\n";
    std::cout << "================================================================================\n";

    OPENSSL_cleanse(masterKey.data(), masterKey.size());
    return 0;
}

