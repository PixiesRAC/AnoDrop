#include "AlertListener.h"

#include <spdlog/spdlog.h>

AlertListener::AlertListener(std::shared_ptr<lt::session> session) : m_session(std::move(session))
{
}

AlertListener::~AlertListener()
{
    SPDLOG_INFO("~AlertListener: clearing observers and stopping alert thread...");
    std::unique_lock<std::shared_mutex> lock(m_observersMutex);
    m_observers.clear();
    SPDLOG_INFO("~AlertListener: cleared observers.");
}

void AlertListener::addObserver(AlertObserver *observer)
{
    if (observer != nullptr)
    {
        std::unique_lock<std::shared_mutex> lock(m_observersMutex);
        if (std::find(m_observers.begin(), m_observers.end(), observer) == m_observers.end())
        {
            m_observers.push_back(observer);
        }
    }
}

void AlertListener::removeObserver(AlertObserver *observer)
{
    std::unique_lock<std::shared_mutex> lock(m_observersMutex);
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
        if (st.stop_requested())
        {
            break;
        }

        std::vector<lt::alert *> alerts;
        m_session->pop_alerts(&alerts);

        if (!alerts.empty())
        {
            std::shared_lock<std::shared_mutex> lock(m_observersMutex);
            for (lt::alert *alert : alerts)
            {
                if (st.stop_requested())
                {
                    break;
                }
                for (AlertObserver *observer : m_observers)
                {
                    try
                    {
                        observer->onAlert(alert);
                    }
                    catch (const std::exception &e)
                    {
                        SPDLOG_ERROR("Exception in observer onAlert: {}", e.what());
                    }
                }
            }
        }

        if (st.stop_requested())
        {
            break;
        }

        {
            std::shared_lock<std::shared_mutex> lock(m_observersMutex);
            for (AlertObserver *observer : m_observers)
            {
                if (st.stop_requested())
                {
                    break;
                }
                try
                {
                    observer->onTick();
                }
                catch (const std::exception &e)
                {
                    SPDLOG_ERROR("Exception in observer onTick: {}", e.what());
                }
            }
        }
    }
}

