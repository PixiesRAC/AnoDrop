#include "OutboxPublisher.h"
#include "LogEvents.h"
#include <spdlog/spdlog.h>

#include "DhtTransport.h"
#include "LobbyStore.h"
#include "PartitionedThreadPool.h"

void OutboxPublisher::publishChatMessage(const ChatPublishParams &params)
{
    ChatPublishParams announceParams = params;
    std::string{}.swap(announceParams.payload);
    // Announce progress only after the body has been replicated successfully.
    SPDLOG_INFO("[MSG-OUT] lobby={} seq={} | Publication requested", LogEvents::lobbyId(params.lobbyName), params.targetSeqNum);
    const bool accepted = m_manager.publishChatMessage(params, [this, announceParams = std::move(announceParams)](bool success)
                          {
        SPDLOG_INFO("[MSG-OUT] lobby={} seq={} success={} | Publication completed", LogEvents::lobbyId(announceParams.lobbyName), announceParams.targetSeqNum, success);
        if (!success) return; // Durable outbox retries on the next scheduler pass.
        m_executor.post(announceParams.lobbyName, [this, announceParams] {
            m_store.execute(announceParams.lobbyName, lobby::ConfirmPublication{announceParams.targetSeqNum});
            const bool announced = m_manager.publishSequenceAnnouncement(announceParams);
            SPDLOG_INFO("[MSG-OUT] lobby={} seq={} accepted={} | Sequence announcement requested", LogEvents::lobbyId(announceParams.lobbyName), announceParams.targetSeqNum, announced);
        }); });
    if (!accepted) SPDLOG_WARN("[MSG-OUT] lobby={} seq={} | Publication rejected; durable outbox will retry", LogEvents::lobbyId(params.lobbyName), params.targetSeqNum);
}
