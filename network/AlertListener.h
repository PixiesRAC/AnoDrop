#pragma once

#include "network/AlertObserver.h"

#include <libtorrent/session.hpp>
#include <memory>
#include <stop_token>
#include <vector>

class AlertListener
{
  public:
    AlertListener(std::shared_ptr<lt::session> session);
    ~AlertListener();

    void checkForAlerts(std::stop_token st);
    void addObserver(AlertObserver *observer);
    void removeObserver(AlertObserver *observer);

  private:
    std::vector<AlertObserver *> m_observers;
    std::shared_ptr<lt::session> m_session;
};