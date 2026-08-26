#pragma once

#include "LobbyListener.h"
#include "lobby/LobbyData.h"
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <expected>

class Lobby;

class LobbyStore
{
  public:
    LobbyStore(const std::string &masterPassword, const std::string &storeDir = "store");

    std::expected<void, std::string> saveLobbyData(const LobbyData &data);
    void updateLobbyData(const std::string &lobbyName, const nlohmann::json &jsonPatch);
    void updateMasterPassword(const std::string &newPassword);

    std::shared_ptr<LobbyObserver> getLobby(const std::string &lobbyName);
    std::shared_ptr<LobbyObserver> getOrCreateLobby(const std::string &lobbyName);
    std::vector<std::shared_ptr<LobbyObserver>> getLobbies();

    void savePreferences(const nlohmann::json &prefs);
    nlohmann::json loadPreferences();

  private:
    void populateLobbies();
    std::expected<LobbyData, std::string> loadLobbyDataFromFile(const std::filesystem::path &filePath);

    std::string m_storeDir;
    std::vector<unsigned char> m_masterKey;
    std::unique_ptr<LobbyListener> m_lobbyListener;
};
