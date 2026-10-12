#include "LobbyListener.h"

#include <nlohmann/json.hpp>

void LobbyListener::addObserver(std::shared_ptr<LobbyObserver> observer)
{
    if (!observer)
        return;
    std::unique_lock<std::shared_mutex> lock(m_listenerMutex);
    std::string name = observer->getLobbyName();
    auto it = std::find_if(m_observers.begin(), m_observers.end(), [&name](const std::shared_ptr<LobbyObserver> &o) {
        return o && o->getLobbyName() == name;
    });
    if (it == m_observers.end())
    {
        m_observers.push_back(observer);
    }
}

std::shared_ptr<LobbyObserver> LobbyListener::getOrCreateObserver(
    const std::string &lobbyName, 
    std::function<std::shared_ptr<LobbyObserver>()> factory)
{
    {
        std::shared_lock<std::shared_mutex> lock(m_listenerMutex);
        auto it = std::find_if(m_observers.begin(), m_observers.end(), [&lobbyName](const std::shared_ptr<LobbyObserver> &o) {
            return o && o->getLobbyName() == lobbyName;
        });
        if (it != m_observers.end())
        {
            return *it;
        }
    }

    std::unique_lock<std::shared_mutex> lock(m_listenerMutex);
    auto it = std::find_if(m_observers.begin(), m_observers.end(), [&lobbyName](const std::shared_ptr<LobbyObserver> &o) {
        return o && o->getLobbyName() == lobbyName;
    });
    if (it != m_observers.end())
    {
        return *it;
    }

    auto newObserver = factory();
    if (newObserver)
    {
        m_observers.push_back(newObserver);
    }
    return newObserver;
}

void LobbyListener::removeObserver(std::shared_ptr<LobbyObserver> observer)
{
    std::unique_lock<std::shared_mutex> lock(m_listenerMutex);
    m_observers.erase(std::remove(m_observers.begin(), m_observers.end(), observer), m_observers.end());
}

std::shared_ptr<LobbyObserver> LobbyListener::getObserver(const std::string &lobbyName)
{
    std::shared_lock<std::shared_mutex> lock(m_listenerMutex);
    auto it = std::find_if(m_observers.begin(), m_observers.end(), [&lobbyName](const std::shared_ptr<LobbyObserver> &observer)
                           { return observer->getLobbyName() == lobbyName; });
    if (it != m_observers.end())
    {
        return *it;
    }
    return nullptr;
}


