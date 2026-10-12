#pragma once

#include <libtorrent/session.hpp>

#include "AlertObserver.h"

#include <shared_mutex>

class AlertListener
{
  public:
    AlertListener(std::shared_ptr<lt::session> session);
    ~AlertListener();

    void checkForAlerts(std::stop_token st);
    void addObserver(AlertObserver *observer);
    void removeObserver(AlertObserver *observer);

  private:
    mutable std::shared_mutex m_observersMutex;
    std::vector<AlertObserver *> m_observers;
    std::shared_ptr<lt::session> m_session;
};