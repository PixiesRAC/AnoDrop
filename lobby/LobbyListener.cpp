#include "LobbyListener.h"
#include <algorithm>

void LobbyListener::addObserver(std::shared_ptr<LobbyObserver> observer)
{
    if (observer && std::find(m_observers.begin(), m_observers.end(), observer) == m_observers.end())
    {
        m_observers.push_back(observer);
    }
}

void LobbyListener::removeObserver(std::shared_ptr<LobbyObserver> observer)
{
    m_observers.erase(std::remove(m_observers.begin(), m_observers.end(), observer), m_observers.end());
}

bool LobbyListener::hasAlreadyObserver(const std::string &lobbyName)
{
    return getObserver(lobbyName) != nullptr;
}

std::shared_ptr<LobbyObserver> LobbyListener::getObserver(const std::string &lobbyName)
{
    auto it = std::find_if(m_observers.begin(), m_observers.end(), [&lobbyName](const std::shared_ptr<LobbyObserver> &observer) {
        return observer->getLobbyName() == lobbyName;
    });
    if (it != m_observers.end())
    {
        return *it;
    }
    return nullptr;
}

void LobbyListener::notifyObservers(const std::string &lobbyName, const nlohmann::json &jsonPatch)
{
    for (auto &observer : m_observers)
    {
        if (observer->getLobbyName() == lobbyName)
        {
            observer->onUpdate(jsonPatch);
        }
    }
}
