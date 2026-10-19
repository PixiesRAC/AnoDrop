#pragma once
#include <libtorrent/session.hpp>

#include "AlertObserver.h"

class AlertListener
{
  public:
    AlertListener(std::shared_ptr<lt::session> session, AlertObserver &observer);
    void checkForAlerts(std::stop_token stop);

  private:
    std::shared_ptr<lt::session> m_session;
    AlertObserver &m_observer; // Owner joins the alert thread before destroying either object.
};
