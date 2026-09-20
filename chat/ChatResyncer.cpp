#include "ChatResyncer.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace chat {

void ChatResyncer::resyncPendingMessages(LobbyObserver& lobbyObserver, DhtChat& dhtChat) {
    LobbyData data = lobbyObserver.getLobbyData();
    
    std::vector<ChatMessage> toResend;
    
    for (const auto& msg : data.history.messages) {
        if (msg.direction == MessageDirection::OUT) {
            bool isAcked = std::binary_search(
                data.acked_seq_nums.begin(), 
                data.acked_seq_nums.end(), 
                msg.seq_num
            );
            
            bool isResynced = std::binary_search(
                data.resynced_seq_nums.begin(), 
                data.resynced_seq_nums.end(), 
                msg.seq_num
            );
            
            if (!isAcked && !isResynced) {
                toResend.push_back(msg);
            }
        }
    }
    
    std::map<std::int64_t, std::int64_t> newPendingResyncs;
    
    for (const auto& msg : toResend) {
        std::time_t t = msg.fake_timestamp;
        std::tm* tm_info = std::localtime(&t);
        
        std::ostringstream timeStream;
        timeStream << std::put_time(tm_info, "%H:%M:%S");
        
        std::string newContent = "[Resync " + timeStream.str() + "] " + msg.content;
        std::int64_t newSeq = dhtChat.sendMessage(newContent);
        newPendingResyncs[newSeq] = msg.seq_num;
    }
    
    if (!newPendingResyncs.empty()) {
        nlohmann::json patch;
        patch["add_pending_resyncs"] = newPendingResyncs;
        dhtChat.dispatchPatch(patch);
    }
}

void ChatResyncer::resyncAllMessages(LobbyObserver& lobbyObserver, DhtChat& dhtChat) {
    LobbyData data = lobbyObserver.getLobbyData();
    std::vector<ChatMessage> toResend;
    
    for (const auto& msg : data.history.messages) {
        if (msg.direction == MessageDirection::OUT) {
            toResend.push_back(msg);
        }
    }
    
    std::map<std::int64_t, std::int64_t> newPendingResyncs;
    for (const auto& msg : toResend) {
        std::time_t t = msg.fake_timestamp;
        std::tm* tm_info = std::localtime(&t);
        
        std::ostringstream timeStream;
        timeStream << std::put_time(tm_info, "%H:%M:%S");
        
        std::string newContent = "[Resync " + timeStream.str() + "] " + msg.content;
        std::int64_t newSeq = dhtChat.sendMessage(newContent);
        newPendingResyncs[newSeq] = msg.seq_num;
    }
    
    if (!newPendingResyncs.empty()) {
        nlohmann::json patch;
        patch["add_pending_resyncs"] = newPendingResyncs;
        dhtChat.dispatchPatch(patch);
    }
}

} // namespace chat
