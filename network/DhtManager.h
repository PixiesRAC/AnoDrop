#pragma once

#include "network/AlertListener.h"
#include "network/AlertObserver.h"
#include "utils/Util.h"

#include <atomic>
#include <libtorrent/session.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

class DhtManager : public AlertObserver {
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

  bool wasPutConfirmed() const;
  void resetPutConfirmed();
  void waitForPutConfirmed();

private:
  std::shared_ptr<lt::session> m_session;
  std::optional<lt::sha1_hash> m_nodeIdIpv4;
  std::optional<lt::sha1_hash> m_nodeIdIpv6;
  std::atomic_bool m_isDhtReady{false};
  std::unique_ptr<AlertListener> m_alertListener;
  std::jthread m_alertThread;
  std::atomic_bool m_lastPutConfirmed{false};
  mutable std::mutex m_nodeIdMutex;
};
