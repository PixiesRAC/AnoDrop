#include "network/DhtManager.h"

#include <chrono>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <spdlog/spdlog.h>
#include <thread>

DhtManager::DhtManager() : AlertObserver()
{
  spdlog::info("Initializing DhtManager session");
  lt::settings_pack p;
  p.set_bool(lt::settings_pack::enable_dht, true);
  p.set_bool(lt::settings_pack::enable_upnp, false);
  p.set_bool(lt::settings_pack::enable_natpmp, false);
  p.set_bool(lt::settings_pack::enable_lsd, false);
  p.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);

  m_session = std::make_shared<lt::session>(p);
}

DhtManager::~DhtManager() {}

void DhtManager::startAlertThread()
{
  m_alertListener = std::make_unique<AlertListener>(m_session);

  m_alertListener->addObserver(this);
  m_alertThread = std::jthread(&AlertListener::checkForAlerts, m_alertListener.get());
}

std::shared_ptr<lt::session> DhtManager::getSharedSession() const
{
  return m_session;
}

lt::session *DhtManager::getSession() const
{
  return m_session.get();
}


void DhtManager::onAlert(lt::alert *alert)
{
  if (auto *stats = lt::alert_cast<lt::dht_stats_alert>(alert))
  {
    bool is_v6 = stats->local_endpoint.address().is_v6();
    std::stringstream ss;
    ss << stats->nid;

    {
      std::lock_guard<std::mutex> lock(m_nodeIdMutex);
      if (is_v6)
      {
        m_nodeIdIpv6 = stats->nid;
        spdlog::info("Local Node ID (IPv6): {}", ss.str());
      }
      else
      {
        m_nodeIdIpv4 = stats->nid;
        spdlog::info("Local Node ID (IPv4): {}", ss.str());
      }
    }

    int total_nodes = 0;
    for (auto const &bucket : stats->routing_table)
    {
      total_nodes += bucket.num_nodes;
    }
    spdlog::info("DHT stats received ({}). Known nodes: {}",
                 is_v6 ? "IPv6" : "IPv4", total_nodes);
    m_isDhtReady.store(true, std::memory_order_release);
  }
  else if (auto *put_alert = lt::alert_cast<lt::dht_put_alert>(alert))
  {
    std::stringstream ss;
    ss << put_alert->target;
    spdlog::info("DHT put operation finished! Target: {}", ss.str());
    m_lastPutConfirmed.store(true, std::memory_order_release);
  }
  else if (auto *get_mut = lt::alert_cast<lt::dht_mutable_item_alert>(alert))
  {
    spdlog::info("Mutable item found! Sequence: {}", get_mut->seq);
  }
  else if (auto *bootstrap = lt::alert_cast<lt::dht_bootstrap_alert>(alert))
  {
    spdlog::info("DHT bootstrap complete! The engine is connected to the network.");
    m_session->post_dht_stats();
  }
  else if (auto *listen_ok = lt::alert_cast<lt::listen_succeeded_alert>(alert))
  {
    spdlog::info("Network listening started successfully on port: {}", listen_ok->port);
  }
  else if (auto *listen_err = lt::alert_cast<lt::listen_failed_alert>(alert))
  {
    spdlog::error("Failed to open network port: {}", listen_err->error.message());
  }
  else if (auto *dht_err = lt::alert_cast<lt::dht_error_alert>(alert))
  {
    spdlog::warn("Internal DHT error: {}", dht_err->error.message());
  }
}

void DhtManager::waitUntilDhtIsReady()
{
  while (!m_isDhtReady.load(std::memory_order_acquire))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spdlog::info("waiting for DHT to be ready");
  }
}

bool DhtManager::wasPutConfirmed() const
{
  return m_lastPutConfirmed.load(std::memory_order_acquire);
}

void DhtManager::resetPutConfirmed()
{
  m_lastPutConfirmed.store(false, std::memory_order_release);
}

void DhtManager::waitForPutConfirmed()
{
  resetPutConfirmed();
  while (!m_lastPutConfirmed.load(std::memory_order_acquire))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spdlog::info("waiting for put confirmed");
  }
}

bool DhtManager::isSessionReady() const
{
  if (!m_session)
    return false;
  return true;
}

std::optional<lt::sha1_hash> DhtManager::getLocalNodeId(IpProtocol protocol) const
{
  std::lock_guard<std::mutex> lock(m_nodeIdMutex);
  std::optional<lt::sha1_hash> result = (protocol == IpProtocol::IPv4) ? m_nodeIdIpv4 : m_nodeIdIpv6;
  if (result && result->is_all_zeros())
  {
    return std::nullopt;
  }
  return result;
}