#include "AlertListener.h"

#include <spdlog/spdlog.h>

AlertListener::AlertListener(std::shared_ptr<lt::session> session) : m_session(std::move(session))
{
}

AlertListener::~AlertListener()
{
    SPDLOG_INFO("~AlertListener: clearing observers and stopping alert thread...");
    m_observers.clear();
}

void AlertListener::addObserver(AlertObserver *observer)
{
    if (observer != nullptr)
        m_observers.push_back(observer);
}

void AlertListener::removeObserver(AlertObserver *observer)
{
    auto it = std::find(m_observers.begin(), m_observers.end(), observer);
    if (it != m_observers.end())
    {
        m_observers.erase(it);
    }
}

void AlertListener::checkForAlerts(std::stop_token st)
{
    while (!st.stop_requested())
    {
        m_session->wait_for_alert(std::chrono::milliseconds(200));
        std::vector<lt::alert *> alerts;
        m_session->pop_alerts(&alerts);
        for (lt::alert *alert : alerts)
        {
            for (AlertObserver *observer : m_observers)
            {
                observer->onAlert(alert);
            }
        }
        for (AlertObserver *observer : m_observers)
        {
            observer->onTick();
        }
    }
}
