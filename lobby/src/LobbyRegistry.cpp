#include "LobbyRegistry.h"

void LobbyRegistry::add(std::shared_ptr<LobbyObserver> observer)
{
    if (!observer)
        throw std::invalid_argument("Cannot register a null lobby");
    auto name = observer->getLobbyName();
    std::unique_lock lock(m_registryMutex);
    m_lobbies.try_emplace(std::move(name), std::move(observer));
}

std::shared_ptr<LobbyObserver> LobbyRegistry::getOrCreate(
    const std::string &name, std::function<std::shared_ptr<LobbyObserver>()> factory)
{
    if (auto existing = find(name))
        return existing;
    auto candidate = factory(); // User code never runs under the registry lock.
    if (!candidate || candidate->getLobbyName() != name)
        throw std::invalid_argument("Lobby factory returned a mismatched lobby");
    std::unique_lock lock(m_registryMutex);
    return m_lobbies.try_emplace(name, std::move(candidate)).first->second;
}

std::shared_ptr<LobbyObserver> LobbyRegistry::find(const std::string &name)
{
    std::shared_lock lock(m_registryMutex);
    const auto it = m_lobbies.find(name);
    return it == m_lobbies.end() ? nullptr : it->second;
}
