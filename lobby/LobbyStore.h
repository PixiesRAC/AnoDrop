#pragma once

#include "LobbyListener.h"
#include "lobby/LobbyData.h"
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <expected>
#include <mutex>
#include "StoreError.h"
#include "crypto/CryptoUtils.h"
#include "utils/WorkerThread.h"

class Lobby;

class LobbyStore
{
  public:
    LobbyStore(const std::string &masterPassword, const std::string &storeDir = "store");
    ~LobbyStore();

    std::expected<void, StoreError> saveLobbyData(const LobbyData &data);
    std::expected<void, StoreError> appendHistory(const std::string &lobbyName, const std::vector<ChatMessage> &newMessages);
    void asyncUpdateLobbyData(const std::string &lobbyName, const nlohmann::json &jsonPatch);
    void updateMasterPassword(const std::string &newPassword);

    std::shared_ptr<LobbyObserver> getLobby(const std::string &lobbyName);
    std::shared_ptr<LobbyObserver> getOrCreateLobby(const std::string &lobbyName);
    std::vector<std::shared_ptr<LobbyObserver>> getLobbies();

    void savePreferences(const nlohmann::json &prefs);
    nlohmann::json loadPreferences();

    void populateLobbies();
    void setLobbyListener(std::unique_ptr<LobbyListener> listener);
    LobbyListener *getLobbyListener() const;
    void removeLobby(const std::string &lobbyName);
    std::vector<std::string> getAvailableLobbies();

  private:
    void updateLobbyData(const std::string &lobbyName, const nlohmann::json &jsonPatch);
    std::expected<void, StoreError> saveLobbyDataToFile(const LobbyData &data, const std::filesystem::path &filePath);
    std::expected<LobbyData, StoreError> loadLobbyDataFromFile(const std::filesystem::path &filePath);

    std::string m_storeDir;
    std::vector<unsigned char> m_masterKey;
    std::unique_ptr<LobbyListener> m_lobbyListener;
    std::mutex m_saveMutex;

    WorkerThread m_ioWorker;
};
