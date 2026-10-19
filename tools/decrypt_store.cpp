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
        std::cout << "       ... [" << std::dec << (length - maxDisplay) << " octets supplémentaires masqués]\n";
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
        std::cerr << "Erreur: Fichier introuvable: " << vaultPath << "\n";
        return 1;
    }

    std::cout << "================================================================================\n";
    std::cout << "                  ANODROP - INSPECTEUR DU JOURNAL CHIFFRÉ                       \n";
    std::cout << "================================================================================\n";
    std::cout << "Répertoire du store : " << storeDir << "\n\n";

    // 1. Déverrouillage de vault.enc
    std::ifstream vaultFile(vaultPath, std::ios::binary);
    if (!vaultFile)
    {
        std::cerr << "Erreur: Impossible d'ouvrir " << vaultPath << "\n";
        return 1;
    }

    std::vector<unsigned char> vaultData(
        (std::istreambuf_iterator<char>(vaultFile)),
        std::istreambuf_iterator<char>());

    std::cout << "--- [1] INSPECTION DU COFFRE (" << vaultPath.filename().string() << ") ---\n";
    std::cout << "Taille du coffre : " << std::dec << vaultData.size() << " octets (attendu: 76 octets)\n";

    if (vaultData.size() != 76)
    {
        std::cerr << "Erreur: Taille de vault.enc invalide (doit être 76 octets)\n";
        return 1;
    }

    const auto salt = std::vector<unsigned char>(vaultData.begin(), vaultData.begin() + 16);
    const auto encryptedDek = std::vector<unsigned char>(vaultData.begin() + 16, vaultData.end());

    std::cout << "🔒 VUE CHIFFRÉE DU COFFRE (HEX) :\n";
    std::cout << "   • Salt Argon2id (16 octets) : " << formatHex(salt.data(), salt.size()) << "\n";
    std::cout << "   • IV Coffre (12 octets)     : " << formatHex(encryptedDek.data(), 12) << "\n";
    std::cout << "   • DEK Chiffré (32 octets)   : " << formatHex(encryptedDek.data() + 12, 32) << "\n";
    std::cout << "   • Tag GCM (16 octets)       : " << formatHex(encryptedDek.data() + 44, 16) << "\n";

    std::vector<unsigned char> masterKey;
    try
    {
        auto kek = CryptoUtils::deriveStoreKey(masterPwd, salt);
        std::cout << "\n🔑 CLÉ KEK DÉRIVÉE (Argon2id du mot de passe + salt) :\n";
        std::cout << "   • KEK (32 octets / 256 bits) : " << formatHex(kek.data(), kek.size()) << "\n";

        auto dekRes = CryptoUtils::decryptAES256GCM(encryptedDek, kek);
        OPENSSL_cleanse(kek.data(), kek.size());

        if (!dekRes)
        {
            std::cerr << "\n❌ ÉCHEC : Mot de passe maître erroné ou vault corrompu.\n";
            return 1;
        }
        masterKey = std::move(*dekRes);
        std::cout << "\n🔓 VUE DÉCHIFFRÉE DU COFFRE (DEK / MASTERKEY) :\n";
        std::cout << "   ✓ Mot de passe valide ! Déchiffrement réussi du DEK via KEK.\n";
        std::cout << "   • DEK / MasterKey (HEX) : " << formatHex(masterKey.data(), masterKey.size()) << "\n\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Erreur dérivation clé : " << e.what() << "\n";
        return 1;
    }

    // 2. Inspection et déchiffrement de transactions.v1.enc
    if (!std::filesystem::exists(journalPath))
    {
        std::cout << "Aucun journal transactions.v1.enc trouvé.\n";
        OPENSSL_cleanse(masterKey.data(), masterKey.size());
        return 0;
    }

    std::ifstream journalFile(journalPath, std::ios::binary);
    if (!journalFile)
    {
        std::cerr << "Erreur: Impossible d'ouvrir " << journalPath << "\n";
        OPENSSL_cleanse(masterKey.data(), masterKey.size());
        return 1;
    }

    std::cout << "--- [2] DÉCORTICAGE DES ENREGISTREMENTS (" << journalPath.filename().string() << ") ---\n";

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
            std::cerr << "⚠️ Fin de fichier inattendue (troncature détectée) à l'offset " << fileOffset << "\n";
            break;
        }

        std::cout << "================================================================================\n";
        std::cout << "📦 TRANSACTION #" << std::dec << recordIndex
                  << "  |  Offset Fichier: " << fileOffset << " (0x" << std::hex << fileOffset << std::dec << ")"
                  << "  |  Taille: " << recordSize << " octets\n";
        std::cout << "================================================================================\n";

        if (recordSize < 28) // 12 IV + 16 Tag
        {
            std::cerr << "❌ Enregistrement trop court (inférieur à 28 octets minimum).\n";
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
        std::cout << "🔒 1. VUE CHIFFRÉE SUR DISQUE (HEX & DÉCIMAL) :\n";
        std::cout << "   • Préfixe Taille (4B LE) : " << recordSize << " octets [ " << formatHex(sizeBuf, 4) << "]\n";
        std::cout << "   • IV Aléatoire (12B)     : [ " << formatHex(iv, 12) << "]\n";
        std::cout << "   • Tag GCM Sceau (16B)    : [ " << formatHex(tag, 16) << "]\n";
        std::cout << "   • AAD de Position        : \"" << aad << "\"\n";
        std::cout << "   • Ciphertext (" << std::dec << ciphertextLen << " octets) :\n";
        printHexDump(ciphertext, ciphertextLen, 64);

        // B. Déchiffrement AES-256-GCM
        std::vector<unsigned char> aadBytes(aad.begin(), aad.end());
        auto decrypted = CryptoUtils::decryptAES256GCM(recordData, masterKey, aadBytes);

        std::cout << "\n🔓 2. VUE DÉCHIFFRÉE (CONTENU EN CLAIR) :\n";
        if (decrypted)
        {
            std::string jsonStr(decrypted->begin(), decrypted->end());
            try
            {
                auto j = nlohmann::json::parse(jsonStr);
                std::cout << "   ✓ Authenticité validée par Tag AES-GCM !\n";
                std::cout << j.dump(2) << "\n\n";
            }
            catch (const std::exception &)
            {
                std::cout << "   ✓ Texte déchiffré (non-JSON) : " << jsonStr << "\n\n";
            }
        }
        else
        {
            std::cout << "   ❌ ÉCHEC D'AUTHENTIFICATION (Tag mismatch ou altération positionnelle) !\n\n";
        }

        fileOffset += 4 + recordSize;
        ++recordIndex;
    }

    std::cout << "================================================================================\n";
    std::cout << "Total transactions parcourues : " << std::dec << recordIndex << "\n";
    std::cout << "Taille totale du journal      : " << fileOffset << " octets\n";
    std::cout << "================================================================================\n";

    OPENSSL_cleanse(masterKey.data(), masterKey.size());
    return 0;
}

