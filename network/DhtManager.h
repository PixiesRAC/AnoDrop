#pragma once

#include "network/AlertListener.h"
#include "network/AlertObserver.h"
#include "utils/Util.h"

#include "lobby/LobbyStore.h"
#include <atomic>
#include <libtorrent/session.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <semaphore>

class DhtManager : public AlertObserver
{
  public:
    DhtManager();
    ~DhtManager();

    lt::session *getSession() const;
    std::shared_ptr<lt::session> getSharedSession() const;

    bool isSessionReady() const;

    void initializeDht();

    std::optional<lt::sha1_hash> getLocalNodeId(IpProtocol protocol = IpProtocol::IPv4) const;

    void onAlert(lt::alert *alert) override;
    void startAlertThread();
    void waitUntilDhtIsReady();

    void resetPutConfirmed();
    void waitForPutConfirmed(const std::string &context = "");

    /* HANDHSAKE */

    static std::optional<nlohmann::json> decryptDhtHandShakePayload(const std::string &ciphertextStr, const std::string &lobbyName);
    static std::string encryptDhtHandshakePayload(const nlohmann::json &payload, const std::string &lobbyName);
    static std::optional<std::vector<unsigned char>> prepareLobbyPayload(std::shared_ptr<LobbyStore> lobbyStore, const std::string &lobbyName, const std::string &pseudo);

    /* END HANDSHAKE*/

  private:
    std::shared_ptr<lt::session> m_session;
    std::optional<lt::sha1_hash> m_nodeIdIpv4;
    std::optional<lt::sha1_hash> m_nodeIdIpv6;
    std::atomic_bool m_isDhtReady{false};
    std::binary_semaphore m_putConfirmedSem{0};
    mutable std::mutex m_nodeIdMutex;

    std::unique_ptr<AlertListener> m_alertListener;
    std::jthread m_alertThread;
};
