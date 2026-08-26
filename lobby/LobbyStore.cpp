#include "lobby/LobbyStore.h"
#include "Lobby.h"
#include "crypto/CryptoUtils.h"
#include "crypto/Hasher.h"
#include "utils/Util.h"

#include <filesystem>
#include <fstream>
#include <ranges>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
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

std::expected<void, std::string> LobbyStore::saveLobbyData(const LobbyData &data)
{
    std::string filename = Util::toHex(Hasher::getInstance().getHashMsg256(data.lobby_name)) + ".enc";
    std::filesystem::path filePath = std::filesystem::path(m_storeDir) / "lobbies" / filename;
    std::filesystem::path tmpPath = filePath.string() + ".tmp";

    nlohmann::json dataToSave = data;
    std::string jsonStr = dataToSave.dump();
    std::vector<unsigned char> plaintext(jsonStr.begin(), jsonStr.end());

    try
    {
        auto ciphertextRes = CryptoUtils::encryptAES256GCM(plaintext, m_masterKey);
        if (!ciphertextRes)
        {
            return std::unexpected("Failed to encrypt data: " + ciphertextRes.error());
        }
        std::vector<unsigned char> ciphertext = *ciphertextRes;

        std::ofstream outFile(tmpPath, std::ios::binary);
        if (!outFile)
        {
            return std::unexpected("Failed to open temp file for writing: " + tmpPath.string());
        }
        outFile.write(reinterpret_cast<const char *>(ciphertext.data()), ciphertext.size());
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
        return std::unexpected(e.what());
    }

    if (m_lobbyListener->hasAlreadyObserver(data.lobby_name))
        return {};
    std::shared_ptr<Lobby> lobby = std::make_shared<Lobby>(data);
    m_lobbyListener->addObserver(std::move(lobby));
    return {};
}

void LobbyStore::updateLobbyData(const std::string &lobbyName, const nlohmann::json &jsonPatch)
{
    SPDLOG_INFO("updateLobbyData for lobby {}: {}", lobbyName, jsonPatch.dump());

    auto observer = getOrCreateLobby(lobbyName);

    observer->onUpdate(jsonPatch);
    saveLobbyData(observer->getLobbyData());
}

std::expected<LobbyData, std::string> LobbyStore::loadLobbyDataFromFile(const std::filesystem::path &filePath)
{
    if (!std::filesystem::exists(filePath))
    {
        return std::unexpected("File does not exist");
    }

    try
    {
        std::ifstream inFile(filePath, std::ios::binary | std::ios::ate);
        if (!inFile)
        {
            return std::unexpected("Failed to open file for reading: " + filePath.string());
        }
        std::streamsize size = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        std::vector<unsigned char> ciphertextBlob(size);
        if (!inFile.read(reinterpret_cast<char *>(ciphertextBlob.data()), size))
        {
            return std::unexpected("Failed to read data from file: " + filePath.string());
        }

        auto plaintextRes = CryptoUtils::decryptAES256GCM(ciphertextBlob, m_masterKey);
        if (!plaintextRes)
        {
            return std::unexpected("Failed to decrypt data: " + plaintextRes.error());
        }
        std::vector<unsigned char> plaintext = *plaintextRes;

        std::string jsonStr(plaintext.begin(), plaintext.end());
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        return j.get<LobbyData>();
    }
    catch (const std::exception &e)
    {
        SPDLOG_ERROR("Error loading lobby data from {}: {} (Wrong Master Password "
                     "or corrupted data?)",
                     filePath.string(), e.what());
        return std::unexpected(e.what());
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
    std::string filename = "preferences.enc";
    std::filesystem::path filePath = std::filesystem::path(m_storeDir) / "pref" / filename;
    std::filesystem::path tmpPath = filePath.string() + ".tmp";

    std::string jsonStr = prefs.dump();
    std::vector<unsigned char> plaintext(jsonStr.begin(), jsonStr.end());

    try
    {
        auto ciphertextRes = CryptoUtils::encryptAES256GCM(plaintext, m_masterKey);
        if (!ciphertextRes) {
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

        auto plaintextRes = CryptoUtils::decryptAES256GCM(ciphertextBlob, m_masterKey);
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
    // 1. Read all data with the old key
    auto lobbies = getLobbies();
    auto prefs = loadPreferences();

    // 2. Update the master key in memory
    m_masterKey = Hasher::getInstance().getHashMsg256(newPassword);

    // 3. Rewrite all data with the new key
    for (const auto &lobby : lobbies)
    {
        saveLobbyData(lobby->getLobbyData()); // Uses atomic rename, it is clean
    }
    savePreferences(prefs);

    SPDLOG_INFO("Master password updated and all files re-encrypted.");
}
