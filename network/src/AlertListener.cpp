#include "AlertListener.h"

#include <spdlog/spdlog.h>

AlertListener::AlertListener(std::shared_ptr<lt::session> session, AlertObserver &observer)
    : m_session(std::move(session)), m_observer(observer)
{
    if (!m_session)
        throw std::invalid_argument("AlertListener requires a session");
}

void AlertListener::checkForAlerts(std::stop_token stop)
{
    auto dispatch = [](auto &&callback)
    {
        try
        {
            callback();
        }
        catch (const std::exception &e)
        {
            SPDLOG_ERROR("[DHT] Alert callback failed: {}", e.what());
        }
        catch (...)
        {
            SPDLOG_ERROR("[DHT] Alert callback threw a non-standard exception");
        }
    };
    std::vector<lt::alert *> alerts;
    while (!stop.stop_requested())
    {
        m_session->wait_for_alert(std::chrono::milliseconds(200));
        if (stop.stop_requested())
            break;
        alerts.clear();
        m_session->pop_alerts(&alerts);
        for (auto *alert : alerts)
        {
            if (stop.stop_requested())
                break;
            dispatch([&]
                     { m_observer.onAlert(alert); });
        }
        if (!stop.stop_requested())
            dispatch([&]
                     { m_observer.onTick(); });
    }
}
