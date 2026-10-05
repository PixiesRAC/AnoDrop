#include <nlohmann/json.hpp>
#include "LobbyDataSerializer.h"
#include "ChatView.h"

#include <ncurses.h>
#undef timeout

#include "ChatResyncer.h"
#include "MessageStatusResolver.h"

namespace ui
{
ChatView::ChatView(std::shared_ptr<LobbyObserver> lobbyObserver, DhtChat *dhtChat) : m_lobbyObserver(lobbyObserver), m_dhtChat(dhtChat), m_focusIndex(0), m_scrollOffset(0)
{
}

std::string ChatView::formatMessagePrefix(const ChatMessage &msg, const LobbyData &lobbyData) const
{
    std::time_t ts = msg.fake_timestamp;
    std::tm *tm_info = std::gmtime(&ts);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%d/%m/%Y %H:%M:%S", tm_info);

    std::string pseudo = (msg.direction == MessageDirection::OUT) ? lobbyData.my_pseudo : (lobbyData.other_pseudo.empty() ? "Peer" : lobbyData.other_pseudo);
    if (pseudo.empty())
        pseudo = "Me";

    return "[" + std::string(timeBuf) + " | Seq:" + std::to_string(msg.seq_num) + "] " + pseudo + ": ";
}

void ChatView::show()
{
    bool inChat = true;
    bool needsRedraw = true;
    wtimeout(stdscr, 200); // 200ms timeout for getch()

    bool lastConnectedState = m_lobbyObserver->getLobbyData().isPeerConnected();
    std::string statusMessage = "";
    auto statusMessageTime = std::chrono::steady_clock::now();

    auto triggerRateLimitWarning = [&](int count) {
        statusMessage = "Rate limit reached. " + std::to_string(count) + " msgs not sent. (Max " + 
                        std::to_string(DhtChat::RATE_LIMIT_MAX_MESSAGES) + "/" + 
                        std::to_string(DhtChat::RATE_LIMIT_WINDOW_SECONDS) + "s)";
        statusMessageTime = std::chrono::steady_clock::now();
        needsRedraw = true;
    };

    while (inChat)
    {
        if (!statusMessage.empty() && std::chrono::steady_clock::now() - statusMessageTime > std::chrono::seconds(5))
        {
            statusMessage.clear();
            needsRedraw = true;
        }

        // If external update received (e.g. peer message)
        if (m_lobbyObserver->consumeUpdateFlag())
        {
            needsRedraw = true;
        }

        LobbyData m_lobby = m_lobbyObserver->getLobbyData();

        bool currentConnectedState = m_lobby.isPeerConnected();
        if (currentConnectedState != lastConnectedState)
        {
            needsRedraw = true;
            lastConnectedState = currentConnectedState;
        }

        const auto &messages = m_lobby.history.messages;

        // Mark as read
        if (messages.size() > m_lobby.read_messages_count)
        {
            nlohmann::json patch = {{"read_messages_count", messages.size()}};
            m_dhtChat->dispatchPatch(patch);
            m_lobby.read_messages_count = messages.size();
        }

        int chatAreaHeight = LINES - 11; // Reserve space for timezone, messages, scroll indicator, buttons, input

        if (needsRedraw)
        {
            clear();
            std::string lobbyName = m_lobby.lobby_name.empty() ? "Unknown" : m_lobby.lobby_name;
            std::string pseudo1 = m_lobby.my_pseudo.empty() ? "Me" : m_lobby.my_pseudo;
            std::string pseudo2 = m_lobby.other_pseudo.empty() ? "?" : m_lobby.other_pseudo;
            std::string title = "=== Chat: " + lobbyName + " [" + pseudo1 + " - " + pseudo2 + "] ===";
            mvprintw(1, 2, "%s", title.c_str());

            // Display Status
            int statusX = 2;
            if (m_lobby.status == LobbyStatus::ANOMALY)
            {
                attron(COLOR_PAIR(4) | A_BOLD); // RED
                if (m_lobby.anomaly_message.empty())
                {
                    mvprintw(2, statusX, "STATUS: [ANOMALY]");
                    statusX += 17;
                }
                else
                {
                    std::string errStr = "STATUS: [ANOMALY] - " + m_lobby.anomaly_message;
                    mvprintw(2, statusX, "%s", errStr.c_str());
                    statusX += static_cast<int>(errStr.length());
                }
                attroff(COLOR_PAIR(4) | A_BOLD);
            }
            else if (m_lobby.status == LobbyStatus::HANDSHAKE_DONE)
            {
                attron(COLOR_PAIR(3)); // GREEN
                mvprintw(2, statusX, "STATUS: [READY]");
                statusX += 15;
                attroff(COLOR_PAIR(3));
            }
            else
            {
                attron(COLOR_PAIR(2)); // YELLOW
                mvprintw(2, statusX, "STATUS: [PENDING]");
                statusX += 17;
                attroff(COLOR_PAIR(2));
            }

            if (m_lobby.status == LobbyStatus::HANDSHAKE_DONE || m_lobby.status == LobbyStatus::ANOMALY)
            {
                if (m_lobby.isPeerConnected())
                {
                    attron(COLOR_PAIR(3)); // GREEN
                    mvprintw(2, statusX, " [Connected]");
                    attroff(COLOR_PAIR(3));
                }
                else
                {
                    attron(COLOR_PAIR(4)); // RED
                    mvprintw(2, statusX, " [Disconnected]");
                    attroff(COLOR_PAIR(4));
                }
            }

            // Display fake timezone
            if (!m_lobby.fake_timezone.name.empty())
            {
                mvprintw(3, 2, "TZ: %ld", m_lobby.announced_seq_num);
            }

            // Draw chat history
            mvprintw(4, 2, "--- Chat History ---");

            int chatAreaTop = 5;

            if (messages.empty())
            {
                mvprintw(chatAreaTop, 2, "(No messages yet)");
            }
            else
            {
                // Messages are already in chronological order (appended in order)
                // Apply scroll: show the latest messages by default, scroll up to see older
                int totalMessages = static_cast<int>(messages.size());
                int maxScroll = std::max(0, totalMessages - chatAreaHeight);
                m_scrollOffset = std::clamp(m_scrollOffset, 0, maxScroll);

                int startIdx = std::max(0, totalMessages - chatAreaHeight - m_scrollOffset);
                int endIdx = std::min(totalMessages, startIdx + chatAreaHeight);

                // Map original seqs to their resyncs, and vice versa
                std::map<std::int64_t, std::vector<std::int64_t>> origToResyncs;
                std::map<std::int64_t, std::int64_t> resyncToOrig;
                MessageStatusResolver::buildResyncMaps(messages, origToResyncs, resyncToOrig);

                for (int i = startIdx; i < endIdx; ++i)
                {
                    const auto &msg = messages[i];

                    // Format message line
                    std::string statusStr = "";
                    int statusColorPair = 0;
                    
                    if (msg.direction == MessageDirection::OUT)
                    {
                        auto status = MessageStatusResolver::resolveStatus(msg, m_lobby, origToResyncs, resyncToOrig);
                        statusStr = status.first;
                        statusColorPair = status.second;
                    }

                    std::string prefix = formatMessagePrefix(msg, m_lobby);
                    std::string lineWithoutStatus = prefix + msg.content;
                    std::string fullLine = lineWithoutStatus + statusStr;

                    // Truncate to screen width
                    int maxWidth = COLS - 4;
                    if (static_cast<int>(fullLine.size()) > maxWidth)
                    {
                        fullLine = fullLine.substr(0, maxWidth - 3) + "...";
                        lineWithoutStatus = fullLine; // Truncated, no separate status rendering
                        statusStr = "";
                    }

                    int xPos = 2;      // Default left align for PEER (IN)
                    int colorPair = 1; // Cyan for PEER

                    if (msg.direction == MessageDirection::OUT)
                    {
                        // Right align for ME (OUT)
                        xPos = COLS - static_cast<int>(fullLine.size()) - 2;
                        if (xPos < 2)
                            xPos = 2;

                        colorPair = 3; // Green for ME
                    }

                    int row = chatAreaTop + (i - startIdx);

                    // Draw message text
                    attron(COLOR_PAIR(colorPair));
                    if (msg.direction == MessageDirection::OUT)
                        attron(A_BOLD);
                    mvprintw(row, xPos, "%s", lineWithoutStatus.c_str());
                    if (msg.direction == MessageDirection::OUT)
                        attroff(A_BOLD);
                    attroff(COLOR_PAIR(colorPair));

                    // Draw status tag in its own color
                    if (!statusStr.empty() && statusColorPair > 0)
                    {
                        attron(COLOR_PAIR(statusColorPair) | A_BOLD);
                        mvprintw(row, xPos + static_cast<int>(lineWithoutStatus.size()), "%s", statusStr.c_str());
                        attroff(COLOR_PAIR(statusColorPair) | A_BOLD);
                    }
                }

                // Scroll indicator
                if (m_scrollOffset > 0)
                {
                    mvprintw(chatAreaTop + chatAreaHeight, COLS - 25, "[Scroll: %d/%d]", m_scrollOffset, maxScroll);
                }
            }

            // Draw Action Buttons
            int leaveAttr = (m_focusIndex == 1) ? A_REVERSE : 0;
            int resyncAttr = (m_focusIndex == 2) ? A_REVERSE : 0;

            attron(leaveAttr);
            mvprintw(LINES - 5, 2, "[ Leave Chat ]");
            attroff(leaveAttr);

            attron(resyncAttr);
            mvprintw(LINES - 5, 20, "[ Resync Pending ]");
            attroff(resyncAttr);

            mvprintw(LINES - 5, 42, "(Use TAB or ARROWS to switch)");

            if (!statusMessage.empty())
            {
                attron(COLOR_PAIR(4) | A_BOLD); // Red text for warning
                mvprintw(LINES - 4, 2, "%s", statusMessage.c_str());
                attroff(COLOR_PAIR(4) | A_BOLD);
            }

            // Draw Input Box
            mvprintw(LINES - 3, 2, "Message:");
            if (m_focusIndex == 0)
                attron(A_BOLD | COLOR_PAIR(1)); // Use cyan border highlight color
            mvprintw(LINES - 2, 2, "> %s_", m_currentMessage.c_str());
            if (m_focusIndex == 0)
                attroff(A_BOLD | COLOR_PAIR(1));

        } // end of if (needsRedraw)

        refresh();
        needsRedraw = false;

        int ch = getch();
        if (ch == ERR)
            continue;

        needsRedraw = true; // Key pressed, something changed (focus, typing, scrolling)

        if (ch == KEY_UP || ch == KEY_DOWN || ch == '\t' || ch == KEY_LEFT || ch == KEY_RIGHT)
        {
            if (ch == '\t')
            {
                m_focusIndex = (m_focusIndex + 1) % 3;
            }
            else if (ch == KEY_UP)
            {
                if (m_focusIndex == 0)
                {
                    m_focusIndex = 1; // Default to first button when going up
                }
                else
                {
                    // Already on buttons, scroll up history
                    m_scrollOffset = std::min(m_scrollOffset + 1, std::max(0, static_cast<int>(messages.size()) - chatAreaHeight));
                }
            }
            else if (ch == KEY_DOWN)
            {
                if (m_focusIndex >= 1 && m_focusIndex <= 2)
                {
                    m_focusIndex = 0;
                }
                else
                {
                    // Already on Input, scroll down history
                    m_scrollOffset = std::max(0, m_scrollOffset - 1);
                }
            }
            else if (ch == KEY_LEFT)
            {
                if (m_focusIndex == 2)
                    m_focusIndex = 1;
            }
            else if (ch == KEY_RIGHT)
            {
                if (m_focusIndex == 1)
                    m_focusIndex = 2;
            }
        }
        else if (ch == KEY_PPAGE)
        {
            // Page Up: scroll up by half the chat area
            m_scrollOffset = std::min(m_scrollOffset + chatAreaHeight / 2, std::max(0, static_cast<int>(messages.size()) - chatAreaHeight));
        }
        else if (ch == KEY_NPAGE)
        {
            // Page Down: scroll down by half the chat area
            m_scrollOffset = std::max(0, m_scrollOffset - chatAreaHeight / 2);
        }
        else if (ch == 10)
        { // ENTER
            if (m_focusIndex == 1)
            {
                inChat = false; // Leave button pressed
            }
            else if (m_focusIndex == 2)
            {
                if (m_dhtChat)
                {
                    int skipped = chat::ChatResyncer::resyncPendingMessages(*m_lobbyObserver, *m_dhtChat);
                    if (skipped > 0)
                    {
                        triggerRateLimitWarning(skipped);
                    }
                    m_scrollOffset = 0; // Jump to latest after resyncing
                    m_focusIndex = 0;
                }
            }
            else if (m_focusIndex == 0)
            {
                if (!m_currentMessage.empty() && m_dhtChat)
                {
                    if (m_dhtChat->sendMessage(m_currentMessage) != -1)
                    {
                        m_currentMessage.clear();
                        m_scrollOffset = 0; // Jump to latest after sending
                    }
                    else
                    {
                        triggerRateLimitWarning(1);
                    }

                    // Refresh lobby data to see the persisted message
                    // Note: m_lobby is a copy, so we need to re-read from the source
                    // This will be improved when we have a reference to the Lobby object
                }
            }
        }
        else if (m_focusIndex == 0)
        {
            // Typing
            if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b')
            {
                if (!m_currentMessage.empty())
                    m_currentMessage.pop_back();
            }
            else if (ch >= 32 && ch <= 126)
            {
                m_currentMessage.push_back((char)ch);
            }
        }
    }
}
} // namespace ui
