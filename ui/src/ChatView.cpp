#include "ChatView.h"

#include <ncurses.h>
#undef timeout

#include "KillSwitch.h"

namespace ui
{

ChatView::ChatView(std::shared_ptr<LobbyObserver> lobbyObserver, DhtChat *dhtChat)
    : m_viewModel(std::make_unique<ChatViewModel>(std::move(lobbyObserver), dhtChat))
{
}

void ChatView::render(int chatAreaHeight, int chatAreaTop)
{
    clear();

    const auto &header = m_viewModel->getHeader();
    mvprintw(1, 2, "%s", header.title.c_str());

    int statusX = 2;
    attron(COLOR_PAIR(header.statusColorPair) | (header.statusColorPair == 4 ? A_BOLD : 0));
    mvprintw(2, statusX, "%s", header.statusText.c_str());
    attroff(COLOR_PAIR(header.statusColorPair) | (header.statusColorPair == 4 ? A_BOLD : 0));
    statusX += static_cast<int>(header.statusText.length());

    if (header.showConnectionStatus)
    {
        if (header.isPeerConnected)
        {
            attron(COLOR_PAIR(3));
            mvprintw(2, statusX, " [Connected]");
            attroff(COLOR_PAIR(3));
        }
        else
        {
            attron(COLOR_PAIR(4));
            mvprintw(2, statusX, " [Disconnected]");
            attroff(COLOR_PAIR(4));
        }
    }

    if (!header.timezoneText.empty())
    {
        mvprintw(3, 2, "%s", header.timezoneText.c_str());
    }

    mvprintw(4, 2, "--- Chat History ---");

    if (!m_viewModel->hasMessages())
    {
        mvprintw(chatAreaTop, 2, "(No messages yet)");
    }
    else
    {
        const auto &lines = m_viewModel->getVisibleLines();
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const auto &line = lines[i];
            int row = chatAreaTop + static_cast<int>(i);

            attron(COLOR_PAIR(line.textColorPair));
            if (line.isBold)
                attron(A_BOLD);
            mvprintw(row, line.displayX, "%s", line.textWithoutStatus.c_str());
            if (line.isBold)
                attroff(A_BOLD);
            attroff(COLOR_PAIR(line.textColorPair));

            if (!line.statusText.empty() && line.statusColorPair > 0)
            {
                attron(COLOR_PAIR(line.statusColorPair) | A_BOLD);
                mvprintw(row, line.displayX + static_cast<int>(line.textWithoutStatus.size()), "%s", line.statusText.c_str());
                attroff(COLOR_PAIR(line.statusColorPair) | A_BOLD);
            }
        }

        if (m_viewModel->getScrollOffset() > 0)
        {
            mvprintw(chatAreaTop + chatAreaHeight, COLS - 25, "[Scroll: %d/%d]",
                     m_viewModel->getScrollOffset(), m_viewModel->getMaxScroll());
        }
    }

    int focus = m_viewModel->getFocusIndex();
    int leaveAttr = (focus == 1) ? A_REVERSE : 0;
    int resyncAttr = (focus == 2) ? A_REVERSE : 0;

    attron(leaveAttr);
    mvprintw(LINES - 5, 2, "[ Leave Chat ]");
    attroff(leaveAttr);

    attron(resyncAttr);
    mvprintw(LINES - 5, 20, "[ Resync Pending ]");
    attroff(resyncAttr);

    mvprintw(LINES - 5, 42, "(Use TAB or ARROWS to switch)");

    const auto &warning = m_viewModel->getStatusWarning();
    if (!warning.empty())
    {
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(LINES - 4, 2, "%s", warning.c_str());
        attroff(COLOR_PAIR(4) | A_BOLD);
    }

    mvprintw(LINES - 3, 2, "Message:");
    if (focus == 0)
        attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(LINES - 2, 2, "> %s_", m_viewModel->getCurrentInput().c_str());
    if (focus == 0)
        attroff(A_BOLD | COLOR_PAIR(1));
}

void ChatView::show()
{
    bool inChat = true;
    bool needsRedraw = true;
    wtimeout(stdscr, 200);

    while (inChat)
    {
        if (KillSwitch::getInstance().isTriggered())
        {
            inChat = false;
            break;
        }

        int chatAreaHeight = LINES - 11;
        int chatAreaTop = 5;
        m_viewModel->setDimensions(chatAreaHeight, COLS);

        if (m_viewModel->update())
        {
            needsRedraw = true;
        }

        if (needsRedraw)
        {
            render(chatAreaHeight, chatAreaTop);
            refresh();
            needsRedraw = false;
        }

        int ch = getch();
        if (ch == ERR)
            continue;

        needsRedraw = true;

        if (ch == '\t')
        {
            m_viewModel->cycleFocus();
        }
        else if (ch == KEY_UP)
        {
            if (m_viewModel->getFocusIndex() == 0)
            {
                m_viewModel->setFocusIndex(1);
            }
            else
            {
                m_viewModel->scrollUp(1);
            }
        }
        else if (ch == KEY_DOWN)
        {
            if (m_viewModel->getFocusIndex() >= 1)
            {
                m_viewModel->setFocusIndex(0);
            }
            else
            {
                m_viewModel->scrollDown(1);
            }
        }
        else if (ch == KEY_LEFT)
        {
            if (m_viewModel->getFocusIndex() == 2)
                m_viewModel->setFocusIndex(1);
        }
        else if (ch == KEY_RIGHT)
        {
            if (m_viewModel->getFocusIndex() == 1)
                m_viewModel->setFocusIndex(2);
        }
        else if (ch == KEY_PPAGE)
        {
            m_viewModel->pageUp();
        }
        else if (ch == KEY_NPAGE)
        {
            m_viewModel->pageDown();
        }
        else if (ch == 10 || ch == 13 || ch == KEY_ENTER)
        {
            auto result = m_viewModel->handleEnter();
            if (result == ChatViewModel::EnterResult::ExitView)
            {
                inChat = false;
            }
        }
        else if (m_viewModel->getFocusIndex() == 0)
        {
            if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b' || ch == KEY_DC)
            {
                m_viewModel->deleteLastInputChar();
            }
            else if (ch >= 32 && ch <= 126)
            {
                m_viewModel->appendInputChar(static_cast<char>(ch));
            }
        }
    }
}

} // namespace ui
