#include "HandshakeCoordinator.h"

#include <spdlog/spdlog.h>

#include "DhtAddress.h"
#include "DhtTransport.h"
#include "HandshakePreparation.h"
#include "HandshakeStateEngine.h"
#include "LobbyObserver.h"
#include "LobbyStore.h"
#include "LobbyUpdate.h"
#include "PartitionedThreadPool.h"

void HandshakeCoordinator::dispatchHandshake(const std::string &lobbyName, const std::string &pseudo)
{
    auto own = HandshakePreparation::prepareLobbyPayload(m_store, lobbyName, pseudo);
    if (!own)
    {
        SPDLOG_INFO("[HANDSHAKE] Handshake preparation skipped or failed");
        return;
    }
    const auto target = network::DhtAddress::keys(lobbyName, lobbyName).targetIDHex;
    SPDLOG_INFO("[HANDSHAKE] Handshake dispatch target_sha1={}", target);
    std::string payload(own->begin(), own->end());

    const bool accepted = m_manager.fetchDhtItem(lobbyName, lobbyName,
                                                 [this, lobbyName, target, payload = std::move(payload)](DhtFetchResult result)
                                                 {
                                                     SPDLOG_INFO("[HANDSHAKE] Handshake fetch completed target_sha1={} status={} seq={} value_bytes={}",
                                                                 target, result.status == DhtFetchStatus::Found ? "found" : result.status == DhtFetchStatus::Missing ? "missing"
                                                                                                                        : result.status == DhtFetchStatus::Timeout   ? "timeout"
                                                                                                                                                                     : "cancelled",
                                                                 result.sequence, result.value.size());
                                                     if (result.status == DhtFetchStatus::Timeout || result.status == DhtFetchStatus::Cancelled)
                                                         return;
                                                     m_executor.post(lobbyName, [this, lobbyName, target, payload, resultData = std::move(result)]
                                                                     {
                auto evaluated = m_store.advanceHandshake(lobbyName, payload,
                    resultData.sequence, resultData.value, resultData.found());
                if (!evaluated) {
                    SPDLOG_WARN("[HANDSHAKE] Handshake transition rejected target_sha1={}", target);
                    return;
                }
                auto transition = std::move(*evaluated);
                    SPDLOG_INFO("[HANDSHAKE] Handshake decision target_sha1={} received_seq={} target_seq={} established={} should_write={}",
                                target, resultData.sequence, transition.targetSequenceNumber, transition.established, transition.shouldWrite);
                if (!transition.shouldWrite) return;

                const bool established = transition.established;
                const bool published = m_manager.publishHandshake(lobbyName, transition.payloadToWrite, transition.targetSequenceNumber, [this, lobbyName, target, established](bool success) {
                        SPDLOG_INFO("[HANDSHAKE] Handshake publication completed target_sha1={} success={} peer_confirmed={}", target, success, established);
                        if (!success || !established) return;
                        m_executor.post(lobbyName, [this, lobbyName, target] {
                            const bool changed = m_store.execute(lobbyName, lobby::CompleteHandshake{});
                            SPDLOG_INFO("[HANDSHAKE] Handshake completion processed target_sha1={} state_changed={}", target, changed);
                        });
                    });
                if (!published) SPDLOG_WARN("[HANDSHAKE] Handshake publication rejected target_sha1={}", target); });
                                                 },
                                                 {DhtFetchKind::Handshake, -1});
    if (!accepted)
        SPDLOG_WARN("[HANDSHAKE] Handshake fetch rejected target_sha1={}", target);
}
