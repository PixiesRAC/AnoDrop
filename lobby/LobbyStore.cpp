#include "lobby/LobbyStore.h"
#include "Lobby.h"
#include "crypto/CryptoUtils.h"
#include "crypto/Hasher.h"
#include "utils/Util.h"
#include <openssl/crypto.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <ranges>
#include <spdlog/spdlog.h>
#include <stdexcept>

LobbyStore::LobbyStore(const std::string &masterPassword, const std::string &storeDir) : m_storeDir(storeDir)
{

    std::filesystem::create_directories(m_storeDir);
    std::filesystem::create_directories(std::filesystem::path(m_storeDir) / "lobbies");
    std::filesystem::create_directories(std::filesystem::path(m_storeDir) / "pref");

    m_masterKey = Hasher::getInstance().getHashMsg256(masterPassword);
    m_lobbyListener = std::make_unique<LobbyListener>();
    populateLobbies();
}

LobbyStore::~LobbyStore()
{
    if (!m_masterKey.empty()) {
        OPENSSL_cleanse(m_masterKey.data(), m_masterKey.capacity());
    }
}

std::expected<void, StoreError> LobbyStore::saveLobbyData(const LobbyData &data)
{
    std::lock_guard<std::mutex> lock(m_saveMutex);
    std::string filename = Util::toHex(Hasher::getInstance().getHashMsg256(data.lobby_name)) + ".enc";
    std::filesystem::path filePath = std::filesystem::path(m_storeDir) / "lobbies" / filename;
    std::filesystem::path tmpPath = filePath.string() + ".tmp";

    nlohmann::json dataToSave = data;
    dataToSave.erase("history"); // Exclude history from the main file
    std::string jsonStr = dataToSave.dump();
    std::vector<unsigned char> plaintext(jsonStr.begin(), jsonStr.end());

    try
    {
        auto ciphertextRes = CryptoUtils::encryptAES256GCM(plaintext, m_masterKey);
        if (!ciphertextRes)
        {
            return std::unexpected(StoreError::EncryptionFailed);
        }
        std::vector<unsigned char> ciphertext = *ciphertextRes;

        std::ofstream outFile(tmpPath, std::ios::binary);
        if (!outFile)
        {
            return std::unexpected(StoreError::FileOpenFailed);
        }
        if (!outFile.write(reinterpret_cast<const char *>(ciphertext.data()), ciphertext.size()))
        {
            return std::unexpected(StoreError::WriteFailed);
        }
        outFile.close(); // Important to close before renaming

        // Atomic rename by the OS
        std::filesystem::rename(tmpPath, filePath);

        SPDLOG_INFO("Lobby data saved successfully for {}", data.lobby_name);
        return {};
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Error saving lobby data: {}", e.what());
        if (std::filesystem::exists(tmpPath))
        {
            std::filesystem::remove(tmpPath); // Cleanup in case of error
        }
        return std::unexpected(StoreError::Unknown);
    }

    if (m_lobbyListener->hasAlreadyObserver(data.lobby_name))
        return {};
    std::shared_ptr<Lobby> lobby = std::make_shared<Lobby>(data);
    m_lobbyListener->addObserver(std::move(lobby));
    return {};
}

std::expected<void, StoreError> LobbyStore::appendHistory(const std::string &lobbyName, const std::vector<ChatMessage> &newMessages)
{
    if (newMessages.empty())
        return {};

    std::lock_guard<std::mutex> lock(m_saveMutex);
    std::string filename = Util::toHex(Hasher::getInstance().getHashMsg256(lobbyName)) + ".history.enc";
    std::filesystem::path filePath = std::filesystem::path(m_storeDir) / "lobbies" / filename;

    std::ofstream outFile(filePath, std::ios::binary | std::ios::app);
    if (!outFile)
    {
        return std::unexpected(StoreError::FileOpenFailed);
    }

    try
    {
        for (const auto &msg : newMessages)
        {
            nlohmann::json j = msg;
            std::string jsonStr = j.dump();

            // Obfuscate message length: pad with spaces to a multiple of 256 bytes
            size_t blockSize = 256;
            size_t padding = blockSize - (jsonStr.size() % blockSize);
            if (padding != blockSize)
            {
                jsonStr.append(padding, ' ');
            }

            std::vector<unsigned char> plaintext(jsonStr.begin(), jsonStr.end());

            auto ciphertextRes = CryptoUtils::encryptAES256GCM(plaintext, m_masterKey);
            if (!ciphertextRes)
            {
                return std::unexpected(StoreError::EncryptionFailed);
            }
            const auto &ciphertext = *ciphertextRes;

            uint32_t size = static_cast<uint32_t>(ciphertext.size());
            if (!outFile.write(reinterpret_cast<const char *>(&size), sizeof(size)) || !outFile.write(reinterpret_cast<const char *>(ciphertext.data()), ciphertext.size()))
            {
                return std::unexpected(StoreError::WriteFailed);
            }
        }
        return {};
    }
    catch (const std::exception &e)
    {
        return std::unexpected(StoreError::Unknown);
    }
}

void LobbyStore::asyncUpdateLobbyData(const std::string &lobbyName, const nlohmann::json &jsonPatch)
{
    m_ioWorker.enqueue([this, lobbyName, jsonPatch]() {
        this->updateLobbyData(lobbyName, jsonPatch);
    });
}

void LobbyStore::updateLobbyData(const std::string &lobbyName, const nlohmann::json &jsonPatch)
{
    nlohmann::json logPatch = jsonPatch;
    if (logPatch.contains("history"))
    {
        logPatch["history"] = "[history updated]";
    }
    SPDLOG_DEBUG("updateLobbyData for lobby {}: {}", lobbyName, logPatch.dump());

    auto observer = getOrCreateLobby(lobbyName);

    bool metadataChanged = false;
    for (auto it = jsonPatch.begin(); it != jsonPatch.end(); ++it)
    {
        if (it.key() == "new_message")
        {
            auto msg = it.value().get<ChatMessage>();
            if (msg.direction == MessageDirection::IN) {
                SPDLOG_DEBUG("📥 [RECEIVED] I/O Worker: Enqueueing disk write for incoming message: '{}'", msg.content);
            } else {
                SPDLOG_DEBUG("📤 [SENT] I/O Worker: Enqueueing disk write for outgoing message: '{}'", msg.content);
            }

            m_ioWorker.enqueue([this, lobbyName, msg]() {
                auto res = appendHistory(lobbyName, {msg});
                if (!res)
                    SPDLOG_ERROR("Failed to append history: {}", storeErrorToString(res.error()));
            });
        }
        else if (it.key() == "message_sent_confirm" || it.key() == "message_resynced")
        {
            metadataChanged = true; // acked_seq_nums and resynced_seq_nums are in the main lobby file
        }
        else
        {
            metadataChanged = true;
        }
    }

    observer->onUpdate(jsonPatch);

    if (metadataChanged)
    {
        auto data = observer->getLobbyData();
        m_ioWorker.enqueue([this, data]() {
            auto res = saveLobbyData(data);
            if (!res)
                SPDLOG_ERROR("Failed to save lobby metadata: {}", storeErrorToString(res.error()));
        });
    }
}

std::expected<LobbyData, StoreError> LobbyStore::loadLobbyDataFromFile(const std::filesystem::path &filePath)
{
    if (!std::filesystem::exists(filePath))
    {
        return std::unexpected(StoreError::FileNotFound);
    }

    try
    {
        std::ifstream inFile(filePath, std::ios::binary | std::ios::ate);
        if (!inFile)
        {
            return std::unexpected(StoreError::FileOpenFailed);
        }
        std::streamsize size = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        std::vector<unsigned char> ciphertextBlob(size);
        if (!inFile.read(reinterpret_cast<char *>(ciphertextBlob.data()), size))
        {
            return std::unexpected(StoreError::ReadFailed);
        }

        std::vector<unsigned char> currentKey;
        {
            std::lock_guard<std::mutex> lock(m_saveMutex);
            currentKey = m_masterKey;
        }

        auto plaintextRes = CryptoUtils::decryptAES256GCM(ciphertextBlob, currentKey);
        if (!plaintextRes)
        {
            return std::unexpected(StoreError::DecryptionFailed);
        }
        std::vector<unsigned char> plaintext = *plaintextRes;

        std::string jsonStr(plaintext.begin(), plaintext.end());
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        LobbyData lobbyData = j.get<LobbyData>();

        bool legacyMigrationNeeded = false;
        if (j.contains("history") && !j["history"]["messages"].empty())
        {
            legacyMigrationNeeded = true;
        }

        std::string historyFilename = Util::toHex(Hasher::getInstance().getHashMsg256(lobbyData.lobby_name)) + ".history.enc";
        std::filesystem::path historyFilePath = std::filesystem::path(m_storeDir) / "lobbies" / historyFilename;

        if (legacyMigrationNeeded)
        {
            SPDLOG_INFO("Migrating legacy history for lobby: {}", lobbyData.lobby_name);
            auto res = appendHistory(lobbyData.lobby_name, lobbyData.history.messages);
            if (res)
            {
                // Re-save without history to complete migration
                if (auto saveRes = saveLobbyData(lobbyData); !saveRes)
                    SPDLOG_ERROR("Failed to save lobby data during migration: {}", storeErrorToString(saveRes.error()));
            }
        }
        else if (std::filesystem::exists(historyFilePath))
        {
            std::ifstream histFile(historyFilePath, std::ios::binary);
            if (histFile)
            {
                while (histFile.peek() != EOF)
                {
                    uint32_t msgSize = 0;
                    if (!histFile.read(reinterpret_cast<char *>(&msgSize), sizeof(msgSize)))
                        break;

                    std::vector<unsigned char> msgCiphertext(msgSize);
                    if (!histFile.read(reinterpret_cast<char *>(msgCiphertext.data()), msgSize))
                        break;

                    auto msgPlaintextRes = CryptoUtils::decryptAES256GCM(msgCiphertext, currentKey);
                    if (msgPlaintextRes)
                    {
                        std::string msgJsonStr(msgPlaintextRes->begin(), msgPlaintextRes->end());
                        try
                        {
                            nlohmann::json msgJson = nlohmann::json::parse(msgJsonStr);
                            lobbyData.history.messages.push_back(msgJson.get<ChatMessage>());
                        }
                        catch (const std::exception &e)
                        {
                            SPDLOG_ERROR("Failed to parse history message: {}", e.what());
                        }
                    }
                }
            }
        }

        return lobbyData;
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Error loading lobby data from {}: {} (Wrong Master Password "
                     "or corrupted data?)",
                     filePath.string(), e.what());
        return std::unexpected(StoreError::Unknown);
    }
}

void LobbyStore::populateLobbies()
{
    std::filesystem::path lobbiesDir = std::filesystem::path(m_storeDir) / "lobbies";
    if (!std::filesystem::exists(lobbiesDir))
        return;

    auto isEncFile = [](const auto &entry) { return entry.path().string().ends_with(".enc"); };

    for (const auto &entry : std::filesystem::directory_iterator(lobbiesDir) | std::views::filter(isEncFile))
    {
        auto dataRes = loadLobbyDataFromFile(entry.path());
        if (dataRes && !dataRes->isEmpty())
        {
            auto lobby = std::make_shared<Lobby>(*dataRes);
            m_lobbyListener->addObserver(lobby);
        }
    }
}

std::shared_ptr<LobbyObserver> LobbyStore::getLobby(const std::string &lobbyName)
{
    return m_lobbyListener->getObserver(lobbyName);
}

std::shared_ptr<LobbyObserver> LobbyStore::getOrCreateLobby(const std::string &lobbyName)
{
    auto observer = getLobby(lobbyName);
    if (observer)
        return observer;

    SPDLOG_INFO("Creating new lobby: {}", lobbyName);

    LobbyData lobbyData;
    lobbyData.lobby_name = lobbyName;
    auto lobby = std::make_shared<Lobby>(lobbyData);
    m_lobbyListener->addObserver(lobby);
    return lobby;
}

std::vector<std::shared_ptr<LobbyObserver>> LobbyStore::getLobbies()
{
    return m_lobbyListener->getObservers();
}

void LobbyStore::savePreferences(const nlohmann::json &prefs)
{
    std::lock_guard<std::mutex> lock(m_saveMutex);
    std::string filename = "preferences.enc";
    std::filesystem::path filePath = std::filesystem::path(m_storeDir) / "pref" / filename;
    std::filesystem::path tmpPath = filePath.string() + ".tmp";

    std::string jsonStr = prefs.dump();
    std::vector<unsigned char> plaintext(jsonStr.begin(), jsonStr.end());

    try
    {
        auto ciphertextRes = CryptoUtils::encryptAES256GCM(plaintext, m_masterKey);
        if (!ciphertextRes)
        {
            SPDLOG_ERROR("Failed to encrypt preferences: {}", ciphertextRes.error());
            return;
        }
        std::vector<unsigned char> ciphertext = *ciphertextRes;

        std::ofstream outFile(tmpPath, std::ios::binary);
        if (!outFile)
            return;
        outFile.write(reinterpret_cast<const char *>(ciphertext.data()), ciphertext.size());
        outFile.close();

        std::filesystem::rename(tmpPath, filePath);
    }
    catch (...)
    {
        // Ignore errors for preferences
        if (std::filesystem::exists(tmpPath))
        {
            std::filesystem::remove(tmpPath);
        }
    }
}

nlohmann::json LobbyStore::loadPreferences()
{
    std::string filename = "preferences.enc";
    std::filesystem::path filePath = std::filesystem::path(m_storeDir) / "pref" / filename;

    if (!std::filesystem::exists(filePath))
    {
        return nlohmann::json::object();
    }

    try
    {
        std::ifstream inFile(filePath, std::ios::binary | std::ios::ate);
        if (!inFile)
            return nlohmann::json::object();

        std::streamsize size = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        std::vector<unsigned char> ciphertextBlob(size);
        if (!inFile.read(reinterpret_cast<char *>(ciphertextBlob.data()), size))
        {
            return nlohmann::json::object();
        }

        std::vector<unsigned char> currentKey;
        {
            std::lock_guard<std::mutex> lock(m_saveMutex);
            currentKey = m_masterKey;
        }

        auto plaintextRes = CryptoUtils::decryptAES256GCM(ciphertextBlob, currentKey);
        if (!plaintextRes)
        {
            SPDLOG_ERROR("Failed to decrypt preferences: {}", plaintextRes.error());
            return nlohmann::json::object();
        }
        std::vector<unsigned char> plaintext = *plaintextRes;
        std::string jsonStr(plaintext.begin(), plaintext.end());
        return nlohmann::json::parse(jsonStr);
    }
    catch (...)
    {
        return nlohmann::json::object();
    }
}

void LobbyStore::updateMasterPassword(const std::string &newPassword)
{
    std::promise<void> promise;
    std::future<void> future = promise.get_future();

    m_ioWorker.enqueue([this, newPassword, &promise]() {
        try {
            // 1. Read all data with the old key
            auto lobbies = getLobbies();
            auto prefs = loadPreferences();

            // 2. Update the master key in memory
            std::vector<unsigned char> newKey = Hasher::getInstance().getHashMsg256(newPassword);
            {
                std::lock_guard<std::mutex> lock(m_saveMutex);
                m_masterKey = newKey;
            }

            // 3. Rewrite all data with the new key
            for (const auto &lobby : lobbies)
            {
                auto lobbyData = lobby->getLobbyData();
                if (auto saveRes = saveLobbyData(lobbyData); !saveRes)
                    SPDLOG_ERROR("Failed to save lobby data during master password update: {}", storeErrorToString(saveRes.error()));
                
                if (!lobbyData.history.messages.empty())
                {
                    std::string historyFilename = Util::toHex(Hasher::getInstance().getHashMsg256(lobbyData.lobby_name)) + ".history.enc";
                    std::filesystem::path historyFilePath = std::filesystem::path(m_storeDir) / "lobbies" / historyFilename;
                    std::filesystem::remove(historyFilePath);
                    appendHistory(lobbyData.lobby_name, lobbyData.history.messages);
                }
            }
            savePreferences(prefs);

            SPDLOG_INFO("Master password updated and all files re-encrypted.");
            promise.set_value();
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });

    try {
        future.get();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to update master password: {}", e.what());
    }
}
