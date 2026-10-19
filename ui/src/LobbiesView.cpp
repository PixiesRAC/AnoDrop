#include "LobbiesView.h"
#include "LogEvents.h"
#include <spdlog/spdlog.h>

#include <ncurses.h>
#undef timeout

#include "ChatView.h"

namespace ui
{
LobbiesView::LobbiesView(std::shared_ptr<AppFacade> appFacade) : m_appFacade(std::move(appFacade))
{
}

void LobbiesView::show()
{
    bool inLobbyMenu = true;
    int selectedLobby = 0;

    struct TimeoutGuard
    {
        ~TimeoutGuard()
        {
            wtimeout(stdscr, -1);
        }
    } timeoutGuard;
    wtimeout(stdscr, 200);

    while (inLobbyMenu)
    {
        auto allObservers = m_appFacade ? m_appFacade->getLobbies() : std::vector<std::shared_ptr<LobbyObserver>>{};
        std::vector<std::shared_ptr<LobbyObserver>> lobbiesObservers;
        std::vector<LobbyMetadata> lobbies;
        lobbiesObservers.reserve(allObservers.size());
        lobbies.reserve(allObservers.size());
        for (const auto &obs : allObservers)
        {
            auto meta = obs->getMetadata();
            if (!meta.is_hidden)
            {
                lobbiesObservers.push_back(obs);
                lobbies.push_back(std::move(meta));
            }
        }

        if (lobbies.empty())
        {
            clear();
            mvprintw(4, 4, "No lobbies found in your local store.");
            mvprintw(6, 4, "Press 'q' to return.");
            refresh();
            int ch = getch();
            if (ch == 'q' || ch == 'Q')
                inLobbyMenu = false;
            continue;
        }

        if (selectedLobby >= static_cast<int>(lobbies.size()))
        {
            selectedLobby = std::max(0, static_cast<int>(lobbies.size()) - 1);
        }

        clear();
        mvprintw(1, 2, "=== LOBBIES ===");

        for (size_t i = 0; i < lobbies.size(); ++i)
        {
            if ((int)i == selectedLobby)
                attron(A_REVERSE);

            std::string lobbyName = lobbies[i].lobby_name.empty() ? "Unknown Lobby" : lobbies[i].lobby_name;
            std::string pseudo1 = lobbies[i].my_pseudo.empty() ? "Me" : lobbies[i].my_pseudo;
            std::string pseudo2 = lobbies[i].other_pseudo.empty() ? "?" : lobbies[i].other_pseudo;

            std::string dispName = "[" + pseudo1 + " - " + pseudo2 + "] " + lobbyName;

            bool hasUnread = lobbiesObservers[i]->hasUnreadMessages();

            int maxDispLen = std::max(10, COLS > 80 ? COLS - 45 : std::min(35, COLS - 20)); // Responsive to terminal size
            if (dispName.length() > (size_t)maxDispLen)
                dispName = dispName.substr(0, maxDispLen) + "...";

            mvprintw(3 + static_cast<int>(i), 2, "%zu: %s ", i + 1, dispName.c_str());

            int colorPair = 1;

            std::string statusStr;
            switch (lobbies[i].status)
            {
            case LobbyStatus::CONFIRMATION_PENDING:
            case LobbyStatus::PENDING_HANDSHAKE:
                statusStr = lobbies[i].handshake_error.empty() ? "[PENDING]" : "[HANDSHAKE ERROR]";
                colorPair = 2;
                break;
            case LobbyStatus::HANDSHAKE_DONE:
                statusStr = "[READY]";
                colorPair = 3;
                break;
            case LobbyStatus::ANOMALY:
                statusStr = "[ERROR]";
                colorPair = 4;
                break;
            }

            attron(COLOR_PAIR(colorPair));
            printw("%s", statusStr.c_str());
            attroff(COLOR_PAIR(colorPair));

            if (lobbies[i].status == LobbyStatus::HANDSHAKE_DONE || lobbies[i].status == LobbyStatus::ANOMALY)
            {
                if (lobbies[i].isPeerConnected())
                {
                    attron(COLOR_PAIR(3)); // Green
                    printw(" [Connected]");
                    attroff(COLOR_PAIR(3));
                }
                else
                {
                    attron(COLOR_PAIR(4)); // Red
                    printw(" [Disconnected]");
                    attroff(COLOR_PAIR(4));
                }
            }

            if (lobbies[i].status == LobbyStatus::ANOMALY && !lobbies[i].anomaly_message.empty())
            {
                attron(COLOR_PAIR(4));
                printw(" - Anomaly: %s", lobbies[i].anomaly_message.c_str());
                attroff(COLOR_PAIR(4));
            }

            if (hasUnread)
            {
                attron(COLOR_PAIR(2) | A_BLINK | A_BOLD);
                printw("  * NEW MSG *");
                attroff(COLOR_PAIR(2) | A_BLINK | A_BOLD);
            }

            if ((int)i == selectedLobby)
                attroff(A_REVERSE);
        }

        mvprintw(LINES - 2, 2, "[UP/DOWN] Navigate | [ENTER] Open Chat | [d/DEL] Forget | [q] Back");
        refresh();

        int ch = getch();
        if (ch == ERR)
            continue;

        if (ch == 'q' || ch == 'Q')
        {
            inLobbyMenu = false;
        }
        else if (ch == KEY_DC || ch == KEY_BACKSPACE || ch == 127 || ch == '\b' || ch == 'd' || ch == 'D')
        {
            if (!lobbiesObservers.empty())
            {
                auto &lobbyObs = lobbiesObservers[static_cast<size_t>(selectedLobby)];
                m_appFacade->hideLobby(lobbyObs->getLobbyName());

                if (selectedLobby > 0 && selectedLobby == (int)lobbiesObservers.size() - 1)
                {
                    selectedLobby--;
                }
            }
        }
        else if (ch == KEY_UP && selectedLobby > 0)
        {
            selectedLobby--;
        }
        else if (ch == KEY_DOWN && selectedLobby < (int)lobbies.size() - 1)
        {
            selectedLobby++;
        }
        else if (ch == 10 || ch == 13 || ch == KEY_ENTER)
        { // ENTER
            if (lobbies[static_cast<size_t>(selectedLobby)].status == LobbyStatus::HANDSHAKE_DONE || lobbies[static_cast<size_t>(selectedLobby)].status == LobbyStatus::ANOMALY)
            {
                // Open specific Chat View
                wtimeout(stdscr, -1); // Disable timeout to wait for user input infinitely in chat
                auto &lobbyObs = lobbiesObservers[static_cast<size_t>(selectedLobby)];
                m_appFacade->synchronizeAndProcessNewIncomingMessage(lobbyObs);
                DhtChat *dhtChat = m_appFacade->chatForLobby(lobbyObs);
                ChatView chatView(lobbyObs, dhtChat);
                SPDLOG_INFO("[USER] lobby={} | Chat opened", LogEvents::lobbyId(lobbyObs->getLobbyName()));
                chatView.show();
                SPDLOG_INFO("[USER] lobby={} | Chat closed", LogEvents::lobbyId(lobbyObs->getLobbyName()));
                wtimeout(stdscr, 200); // Restore timeout
            }
            else
            {
                attron(COLOR_PAIR(2) | A_BOLD);
                const auto& error = lobbies[static_cast<size_t>(selectedLobby)].handshake_error;
                const auto reason = error.empty() ? std::string("Lobby handshake is still pending with peer.") : error;
                mvprintw(LINES - 1, 2, "Cannot open: %.*s", std::max(0, COLS - 16), reason.c_str());
                attroff(COLOR_PAIR(2) | A_BOLD);
                refresh();
            }
        }
    }
    wtimeout(stdscr, -1); // reset
    clear();
}
} // namespace ui
