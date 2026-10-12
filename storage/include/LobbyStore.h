#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "IoWorker.h"
#include "LobbyData.h"
#include "LobbyListener.h"
#include "StoreError.h"

class Lobby;

class LobbyStore
{
  public:
    static constexpr const char* DEFAULT_STORE_DIR = "store";

    LobbyStore(const std::string &masterPassword, const std::string &storeDir = DEFAULT_STORE_DIR);
    ~LobbyStore();

    void asyncUpdateLobbyData(const std::string &lobbyName, const nlohmann::json &jsonPatch);
    void updateMasterPassword(const std::string &newPassword);

    [[nodiscard]] std::shared_ptr<LobbyObserver> getLobby(const std::string &lobbyName);
    [[nodiscard]] std::shared_ptr<LobbyObserver> getOrCreateLobby(const std::string &lobbyName);
    [[nodiscard]] std::vector<std::shared_ptr<LobbyObserver>> getLobbies();

    void populateLobbies();

  private:
    [[nodiscard]] std::expected<void, StoreError> saveLobbyData(const LobbyData &data);
    [[nodiscard]] std::expected<void, StoreError> appendHistory(const std::string &lobbyName, const std::vector<ChatMessage> &newMessages);
    void updateLobbyData(const std::string &lobbyName, std::shared_ptr<LobbyObserver> observer, const nlohmann::json &jsonPatch);
    [[nodiscard]] std::expected<LobbyData, StoreError> loadLobbyDataFromFile(const std::filesystem::path &filePath);

    std::string m_storeDir;
    std::vector<unsigned char> m_masterKey;
    std::unique_ptr<LobbyListener> m_lobbyListener;

    IoWorker m_ioWorker;
};
