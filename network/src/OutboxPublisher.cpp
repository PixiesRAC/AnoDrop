#include "OutboxPublisher.h"
#include "DhtMessageCodec.h"
#include "LogEvents.h"
#include <spdlog/spdlog.h>

#include "DhtTransport.h"
#include "LobbyStore.h"
#include "PartitionedThreadPool.h"

void OutboxPublisher::publishChatMessage(const ChatPublishParams &params)
{
    ChatPublishParams announceParams = params;
    std::string{}.swap(announceParams.payload);
    // The journal keeps the plaintext; only the DHT copy is sealed for the peer, right before the PUT.
    // A sealing failure never falls back to plaintext: the durable outbox retries on the next pass.
    auto sealed = network::DhtMessageCodec::sealChatPayload(params.payload, params.recipientX25519Pub, params.targetSeqNum);
    if (!sealed)
    {
        SPDLOG_ERROR("[MSG-OUT] lobby={} seq={} | Publication aborted: payload sealing failed ({})", LogEvents::lobbyId(params.lobbyName), params.targetSeqNum, sealed.error());
        return;
    }
    ChatPublishParams wireParams = announceParams;
    wireParams.payload = std::move(*sealed);
    // Announce progress only after the body has been replicated successfully.
    SPDLOG_INFO("[MSG-OUT] lobby={} seq={} | Publication requested", LogEvents::lobbyId(params.lobbyName), params.targetSeqNum);
    const bool accepted = m_manager.publishChatMessage(wireParams, [this, announceParams = std::move(announceParams)](bool success)
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
