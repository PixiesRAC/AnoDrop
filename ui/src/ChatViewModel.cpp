#include "ChatViewModel.h"

#include <algorithm>
#include <climits>
#include <ctime>
#include <spdlog/spdlog.h>

#include "ChatConstants.h"
#include "ChatResyncer.h"
#include "MessageStatusResolver.h"

namespace ui
{

ChatViewModel::ChatViewModel(std::shared_ptr<LobbyObserver> lobbyObserver, DhtChat *dhtChat)
    : m_lobbyObserver(std::move(lobbyObserver)), m_dhtChat(dhtChat)
{
    if (m_lobbyObserver)
    {
        m_lastConnectedState = m_lobbyObserver->isPeerConnected();
        rebuildViewData();
    }
}

void ChatViewModel::setDimensions(int chatAreaHeight, int screenWidth)
{
    int newHeight = std::max(1, chatAreaHeight);
    int newWidth = std::max(20, screenWidth);
    if (newHeight != m_chatAreaHeight || newWidth != m_screenWidth)
    {
        m_chatAreaHeight = newHeight;
        m_screenWidth = newWidth;
        m_dimensionsChanged = true;
        rebuildViewData();
    }
}

std::string ChatViewModel::formatMessagePrefix(const ChatMessage &msg, const LobbyViewData &lobbyData) const
{
    std::time_t ts = static_cast<std::time_t>(msg.fake_timestamp);
    std::tm tm_info{};
    char timeBuf[32];
    if (gmtime_r(&ts, &tm_info) == nullptr ||
        std::strftime(timeBuf, sizeof(timeBuf), "%d/%m/%Y %H:%M:%S", &tm_info) == 0)
    {
        std::snprintf(timeBuf, sizeof(timeBuf), "--/--/---- --:--:--");
    }

    std::string pseudo = (msg.direction == MessageDirection::OUT)
                             ? lobbyData.metadata.my_pseudo
                             : (lobbyData.metadata.other_pseudo.empty() ? "Peer" : lobbyData.metadata.other_pseudo);
    if (pseudo.empty())
        pseudo = "Me";

    return "[" + std::string(timeBuf) + " | Seq:" + std::to_string(msg.seq_num) + "] " + pseudo + ": ";
}

void ChatViewModel::triggerRateLimitWarning(int count)
{
    m_statusWarning = "Rate limit reached. " + std::to_string(count) + " msgs not sent. (Max " +
                      std::to_string(DhtChat::RATE_LIMIT_MAX_MESSAGES) + "/" +
                      std::to_string(DhtChat::RATE_LIMIT_WINDOW_SECONDS) + "s)";
    m_statusWarningTime = std::chrono::steady_clock::now();
}

void ChatViewModel::indexNewMessages(std::span<const ChatMessage> messages)
{
    MessageStatusResolver::buildResyncMaps(messages, m_origToResyncs, m_resyncToOrig);
    m_lastIndexedMessageCount += messages.size();
}

void ChatViewModel::rebuildViewData()
{
    if (!m_lobbyObserver)
        return;

    m_lobbyData = m_lobbyObserver->getViewData();

    std::string lobbyName = m_lobbyData.metadata.lobby_name.empty() ? "Unknown" : m_lobbyData.metadata.lobby_name;
    std::string pseudo1 = m_lobbyData.metadata.my_pseudo.empty() ? "Me" : m_lobbyData.metadata.my_pseudo;
    std::string pseudo2 = m_lobbyData.metadata.other_pseudo.empty() ? "?" : m_lobbyData.metadata.other_pseudo;
    m_header.title = "=== Chat: " + lobbyName + " [" + pseudo1 + " - " + pseudo2 + "] ===";

    if (m_lobbyData.metadata.status == LobbyStatus::ANOMALY)
    {
        m_header.statusText = m_lobbyData.metadata.anomaly_message.empty()
                                  ? "STATUS: [ANOMALY]"
                                  : "STATUS: [ANOMALY] - " + m_lobbyData.metadata.anomaly_message;
        m_header.statusColorPair = 4;
    }
    else if (m_lobbyData.metadata.status == LobbyStatus::HANDSHAKE_DONE)
    {
        m_header.statusText = "STATUS: [READY]";
        m_header.statusColorPair = 3;
    }
    else
    {
        m_header.statusText = "STATUS: [PENDING]";
        m_header.statusColorPair = 2;
    }

    m_header.showConnectionStatus = (m_lobbyData.metadata.status == LobbyStatus::HANDSHAKE_DONE ||
                                     m_lobbyData.metadata.status == LobbyStatus::ANOMALY);
    m_header.isPeerConnected = m_lobbyData.metadata.isPeerConnected();

    if (!m_lobbyData.metadata.fake_timezone.name.empty())
    {
        m_header.timezoneText = "TZ: " + m_lobbyData.metadata.fake_timezone.name;
    }
    else
    {
        m_header.timezoneText.clear();
    }

    m_visibleLines.clear();
    size_t newReadCount = 0;

    m_totalMessageCount = m_lobbyObserver->historySize();
    newReadCount = m_totalMessageCount;
    const auto totalMessages = static_cast<int>(std::min(m_totalMessageCount, static_cast<std::size_t>(INT_MAX)));
    m_maxScroll = std::max(0, totalMessages - m_chatAreaHeight);
    m_scrollOffset = std::clamp(m_scrollOffset, 0, m_maxScroll);
    const auto start = static_cast<std::size_t>(std::max(0, totalMessages - m_chatAreaHeight - m_scrollOffset));
    const auto count = static_cast<std::size_t>(m_chatAreaHeight);
    const bool pageMatches = m_hasCachedPage && m_cachedPage.offset == start &&
        m_cachedPage.count == count && m_cachedPage.total == m_totalMessageCount;
    if (!m_historyFuture.valid() && (!pageMatches || m_lastIndexedMessageCount < m_totalMessageCount)) {
        auto observer = m_lobbyObserver;
        const auto total = m_totalMessageCount;
        const auto indexOffset = m_lastIndexedMessageCount;
        m_historyFuture = m_tasks.submit([observer, start, count, total, indexOffset, pageMatches] {
            HistoryPage page;
            page.offset = start; page.count = count; page.total = total; page.indexOffset = indexOffset;
            page.loadedVisible = !pageMatches;
            if (page.loadedVisible) page.visible = observer->historyPage(start, count);
            page.toIndex = observer->historyPage(indexOffset, 256);
            return page;
        });
    }
    // Keep rendering the last known page while the worker fetches the new one,
    // avoiding a blank screen flicker during scrolling.
    const std::vector<ChatMessage> emptyPage;
    const auto& visible = m_hasCachedPage ? m_cachedPage.visible : emptyPage;
    for (const auto& msg : visible) {
            std::string statusStr = "";
            int statusColorPair = 0;

            if (msg.direction == MessageDirection::OUT)
            {
                auto status = MessageStatusResolver::resolveStatus(msg, m_lobbyData, m_origToResyncs, m_resyncToOrig);
                statusStr = status.first;
                statusColorPair = status.second;
            }

            std::string prefix = formatMessagePrefix(msg, m_lobbyData);
            std::string lineWithoutStatus = prefix + msg.content;
            std::string fullLine = lineWithoutStatus + statusStr;

            int maxWidth = m_screenWidth - 4;
            if (static_cast<int>(fullLine.size()) > maxWidth)
            {
                fullLine = fullLine.substr(0, static_cast<size_t>(maxWidth - 3)) + "...";
                lineWithoutStatus = fullLine;
                statusStr = "";
            }

            int xPos = 2;
            int textColorPair = 1;
            bool isBold = false;
            bool isOut = (msg.direction == MessageDirection::OUT);

            if (isOut)
            {
                xPos = m_screenWidth - static_cast<int>(fullLine.size()) - 2;
                if (xPos < 2)
                    xPos = 2;
                textColorPair = 3;
                isBold = true;
            }

            m_visibleLines.push_back(FormattedMessageLine{
                .textWithoutStatus = std::move(lineWithoutStatus),
                .statusText = std::move(statusStr),
                .statusColorPair = statusColorPair,
                .textColorPair = textColorPair,
                .isBold = isBold,
                .displayX = xPos,
            });
    }

    if (m_dhtChat && pageMatches && !m_readFuture.valid() && newReadCount > m_markedReadCount &&
        newReadCount > m_lobbyData.metadata.read_messages_count) {
        auto chat = m_dhtChat;
        m_readFuture = m_tasks.submit([chat, newReadCount] {
            chat->markRead(newReadCount);
            return newReadCount;
        });
    }

}

bool ChatViewModel::update()
{
    if (!m_lobbyObserver)
        return false;

    bool needsRedraw = collectCompletedWork();

    if (m_dimensionsChanged)
    {
        needsRedraw = true;
        m_dimensionsChanged = false;
    }

    // Check connection change
    bool currentConnectedState = m_lobbyObserver->isPeerConnected();
    if (currentConnectedState != m_lastConnectedState)
    {
        needsRedraw = true;
        m_lastConnectedState = currentConnectedState;
    }

    // Check external message update flag
    if (m_lobbyObserver->consumeUpdateFlag())
    {
        needsRedraw = true;
    }

    // Check rate limit warning expiration
    if (!m_statusWarning.empty() &&
        std::chrono::steady_clock::now() - m_statusWarningTime > std::chrono::seconds(5))
    {
        m_statusWarning.clear();
        needsRedraw = true;
    }

    if (needsRedraw)
    {
        rebuildViewData();
    }

    return needsRedraw;
}

void ChatViewModel::cycleFocus()
{
    m_focusIndex = (m_focusIndex + 1) % 3;
}

void ChatViewModel::setFocusIndex(int index)
{
    m_focusIndex = std::clamp(index, 0, 2);
}

void ChatViewModel::scrollUp(int lines)
{
    m_scrollOffset = std::min(m_scrollOffset + lines, m_maxScroll);
    rebuildViewData();
}

void ChatViewModel::scrollDown(int lines)
{
    m_scrollOffset = std::max(0, m_scrollOffset - lines);
    rebuildViewData();
}

void ChatViewModel::pageUp()
{
    scrollUp(std::max(1, m_chatAreaHeight / 2));
}

void ChatViewModel::pageDown()
{
    scrollDown(std::max(1, m_chatAreaHeight / 2));
}

void ChatViewModel::appendInputChar(char c)
{
    m_currentInput.push_back(c);
}

void ChatViewModel::deleteLastInputChar()
{
    if (!m_currentInput.empty())
    {
        m_currentInput.pop_back();
    }
}

ChatViewModel::EnterResult ChatViewModel::handleEnter()
{
    if (m_focusIndex == 1)
    {
        return EnterResult::ExitView;
    }

    if (m_focusIndex == 2)
    {
        if (m_dhtChat && m_lobbyObserver)
        {
            if (m_resyncFuture.valid() || m_sendFuture.valid()) return EnterResult::None;
            auto observer = m_lobbyObserver;
            auto chat = m_dhtChat;
            m_resyncFuture = m_tasks.submit([observer, chat] {
                return chat::ChatResyncer::resyncPendingMessages(*observer, *chat);
            });
            m_statusWarning = "Resynchronizing...";
            m_statusWarningTime = std::chrono::steady_clock::now();
            m_focusIndex = 0;
            return EnterResult::ResyncTriggered;
        }
        return EnterResult::None;
    }

    if (m_focusIndex == 0)
    {
        if (!m_currentInput.empty() && m_dhtChat)
        {
            if (m_sendFuture.valid() || m_resyncFuture.valid()) return EnterResult::None;
            auto chat = m_dhtChat;
            m_submittedInput = m_currentInput;
            m_sendFuture = m_tasks.submit([chat, text = m_submittedInput] { return chat->sendMessage(text); });
            m_statusWarning = "Saving message...";
            m_statusWarningTime = std::chrono::steady_clock::now();
            return EnterResult::None;

        }
    }

    return EnterResult::None;
}

void ChatViewModel::showSendError(SendError error)
{
    switch (error) {
    case SendError::RateLimited: triggerRateLimitWarning(1); break;
    case SendError::TooLarge: m_statusWarning = "Message exceeds BEP44 size limit."; break;
    case SendError::RecoveryRequired: m_statusWarning = "Storage needs recovery. Reopen AnoDrop before retrying this message."; break;
    case SendError::StorageFailed: m_statusWarning = "Storage failed; message was not accepted."; break;
    case SendError::Unavailable: m_statusWarning = "Chat is not ready."; break;
    case SendError::SequenceExhausted: m_statusWarning = "Message sequence exhausted."; break;
    }
    m_statusWarningTime = std::chrono::steady_clock::now();
}

bool ChatViewModel::collectCompletedWork()
{
    const auto ready = [](auto& future) { return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
    bool changed = false;
    auto collect = [&](auto& future, auto consume) {
        if (!ready(future)) return;
        changed = true;
        try { consume(); }
        catch (const std::exception& error) {
            m_statusWarning = std::string("Chat operation failed: ") + error.what();
            m_statusWarningTime = std::chrono::steady_clock::now();
        }
    };
    collect(m_historyFuture, [&] {
        auto page = m_historyFuture.get();
        if (page.indexOffset == m_lastIndexedMessageCount) indexNewMessages(page.toIndex);
        if (page.loadedVisible) { m_cachedPage = std::move(page); m_hasCachedPage = true; }
    });
    collect(m_sendFuture, [&] {
        auto sent = m_sendFuture.get();
        if (!sent) { showSendError(sent.error()); return; }
        if (m_currentInput == m_submittedInput) m_currentInput.clear();
        m_scrollOffset = 0;
        m_statusWarning.clear();
    });
    collect(m_resyncFuture, [&] {
        const auto skipped = m_resyncFuture.get();
        m_statusWarning.clear();
        if (skipped > 0) { m_statusWarning = std::to_string(skipped) + " resync message(s) could not be queued."; m_statusWarningTime = std::chrono::steady_clock::now(); }
        m_scrollOffset = 0;
    });
    collect(m_readFuture, [&] { m_markedReadCount = m_readFuture.get(); });
    return changed;
}

} // namespace ui
