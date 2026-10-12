#include "ChatViewModel.h"

#include <algorithm>
#include <ctime>
#include <nlohmann/json.hpp>
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

std::string ChatViewModel::formatMessagePrefix(const ChatMessage &msg, const LobbyData &lobbyData) const
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
    if (messages.size() != m_lastIndexedMessageCount)
    {
        if (messages.size() < m_lastIndexedMessageCount)
        {
            m_origToResyncs.clear();
            m_resyncToOrig.clear();
            m_lastIndexedMessageCount = 0;
        }

        for (size_t idx = m_lastIndexedMessageCount; idx < messages.size(); ++idx)
        {
            const auto &m = messages[idx];
            if (m.direction == MessageDirection::OUT && m.content.find(chat::RESYNC_PREFIX) == 0)
            {
                size_t start = chat::RESYNC_PREFIX.length();
                size_t end = m.content.find(" ", start);
                if (end != std::string::npos)
                {
                    try
                    {
                        std::int64_t origSeq = std::stoll(m.content.substr(start, end - start));
                        m_origToResyncs[origSeq].push_back(m.seq_num);
                        m_resyncToOrig[m.seq_num] = origSeq;
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
        m_lastIndexedMessageCount = messages.size();
    }
}

void ChatViewModel::rebuildViewData()
{
    if (!m_lobbyObserver)
        return;

    m_lobbyData = m_lobbyObserver->getLobbyDataWithoutHistory();

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
    m_header.isPeerConnected = m_lobbyObserver->isPeerConnected();

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

    m_lobbyObserver->withHistory([&](std::span<const ChatMessage> messages) {
        m_totalMessageCount = messages.size();
        newReadCount = messages.size();

        if (messages.empty())
        {
            return;
        }

        int totalMessages = static_cast<int>(messages.size());
        m_maxScroll = std::max(0, totalMessages - m_chatAreaHeight);
        m_scrollOffset = std::clamp(m_scrollOffset, 0, m_maxScroll);

        int startIdx = std::max(0, totalMessages - m_chatAreaHeight - m_scrollOffset);
        int endIdx = std::min(totalMessages, startIdx + m_chatAreaHeight);

        indexNewMessages(messages);

        for (int i = startIdx; i < endIdx; ++i)
        {
            const auto &msg = messages[static_cast<size_t>(i)];

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
                .isOutgoing = isOut,
                .displayX = xPos,
            });
        }
    });

    // Mark as read OUTSIDE withHistory lock
    if (m_dhtChat && newReadCount > m_lobbyData.metadata.read_messages_count)
    {
        nlohmann::json patch = {{"read_messages_count", newReadCount}};
        m_dhtChat->dispatchPatch(patch);
        m_lobbyData.metadata.read_messages_count = newReadCount;
    }
}

bool ChatViewModel::update()
{
    if (!m_lobbyObserver)
        return false;

    bool needsRedraw = false;

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

void ChatViewModel::clearInput()
{
    m_currentInput.clear();
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
            int skipped = chat::ChatResyncer::resyncPendingMessages(*m_lobbyObserver, *m_dhtChat);
            if (skipped > 0)
            {
                triggerRateLimitWarning(skipped);
            }
            m_scrollOffset = 0;
            m_focusIndex = 0;
            rebuildViewData();
            return EnterResult::ResyncTriggered;
        }
        return EnterResult::None;
    }

    if (m_focusIndex == 0)
    {
        if (!m_currentInput.empty() && m_dhtChat)
        {
            if (m_dhtChat->sendMessage(m_currentInput) != -1)
            {
                m_currentInput.clear();
                m_scrollOffset = 0;
                rebuildViewData();
                return EnterResult::MessageSent;
            }
            else
            {
                triggerRateLimitWarning(1);
                rebuildViewData();
                return EnterResult::RateLimitHit;
            }
        }
    }

    return EnterResult::None;
}

} // namespace ui
