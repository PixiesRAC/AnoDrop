#pragma once

#include <atomic>
#include <string>

class KillSwitch
{
public:
    [[nodiscard]] static KillSwitch &getInstance();

    // Initializes the kill switch target interface ("any" or explicit interface name).
    // Startup failures are handled asynchronously via listen_failed_alert.
    void init(const std::string &networkInterface);

    // Periodic watchdog check (called from DHT onTick ~every second).
    void checkHealth();

    // Handles libtorrent listen failure alert.
    void handleListenFailed(const std::string &errorMessage);

    // Triggers emergency shutdown: logs critical error, restores terminal,
    // prints red banner to stderr, and terminates process via std::quick_exit.
    [[noreturn]] void trigger(const std::string &reason);

    [[nodiscard]] bool isEnabled() const;
    [[nodiscard]] bool isTriggered() const;
    [[nodiscard]] const std::string &getBoundInterface() const;

private:
    KillSwitch() = default;
    ~KillSwitch() = default;
    KillSwitch(const KillSwitch &) = delete;
    KillSwitch &operator=(const KillSwitch &) = delete;

    std::string m_boundInterface{"any"};
    bool m_enabled{false};
    int m_tickCounter{0};
    std::atomic<bool> m_triggered{false};
};
