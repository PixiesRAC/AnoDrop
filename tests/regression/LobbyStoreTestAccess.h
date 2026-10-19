#pragma once
#include "LobbyStore.h"
class LobbyStoreTestAccess : public LobbyStore
{
  public:
    using LobbyStore::commit;
    using LobbyStore::LobbyStore;
    using LobbyStore::transact;
    void closeQueue() { drainWorkerQueue(); }
};
