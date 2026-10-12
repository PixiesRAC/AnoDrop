#include "KillSwitch.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>

#include <curses.h>
#include "NetworkUtils.h"

KillSwitch &KillSwitch::getInstance()
{
    static KillSwitch instance;
    return instance;
}

void KillSwitch::init(const std::string &networkInterface)
{
    m_configuredInterface = networkInterface;
    m_tickCounter = 0;

    if (networkInterface.empty() || networkInterface == "any")
    {
        m_boundInterface = "any";
        m_enabled = false;
        SPDLOG_INFO("[KillSwitch] Disabled (network_interface='any')");
        return;
    }

    m_boundInterface = networkInterface;
    m_enabled = true;
    SPDLOG_INFO("[KillSwitch] Target interface: '{}' (binding via Libtorrent, startup failure handled by listen_failed_alert)", m_boundInterface);
}

void KillSwitch::checkHealth()
{
    if (!m_enabled || m_boundInterface == "any")
    {
        return;
    }

    // Check interface health every 5 ticks (~1 second at 200ms tick interval)
    if (++m_tickCounter % 5 == 0)
    {
        if (!NetworkUtils::isInterfaceUpAndRunning(m_boundInterface))
        {
            trigger("🚨 KILLSWITCH TRIGGERED: Bound interface '" + m_boundInterface + "' disconnected or went down! Killing process immediately to prevent IP leak.");
        }
    }
}

void KillSwitch::handleListenFailed(const std::string &errorMessage)
{
    if (!m_enabled || m_boundInterface == "any")
    {
        return;
    }

    trigger("🚨 KILLSWITCH TRIGGERED: Listen failed on bound interface '" + m_boundInterface + "' (" + errorMessage + ")! Shutting down immediately to prevent IP leak.");
}

[[noreturn]] void KillSwitch::trigger(const std::string &reason)
{
    bool expected = false;
    if (!m_triggered.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        // Another thread is already shutting down
        std::quick_exit(EXIT_FAILURE);
    }

    SPDLOG_CRITICAL("{}", reason);

    // Give a short grace period (50ms) to allow UI thread loops to observe isTriggered() and break
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Reset curses terminal if initialized so the user's terminal isn't corrupted
    try
    {
        if (!isendwin())
        {
            endwin();
        }
    }
    catch (...)
    {
    }

    // Print bold red alert message to stderr and flush
    std::cerr << "\n\033[1;31m" << reason << "\033[0m\n\n" << std::flush;
    std::fflush(nullptr);

    std::quick_exit(EXIT_FAILURE);
}

bool KillSwitch::isEnabled() const
{
    return m_enabled;
}

bool KillSwitch::isTriggered() const
{
    return m_triggered.load(std::memory_order_acquire);
}

const std::string &KillSwitch::getBoundInterface() const
{
    return m_boundInterface;
}

const std::string &KillSwitch::getConfiguredInterface() const
{
    return m_configuredInterface;
}
