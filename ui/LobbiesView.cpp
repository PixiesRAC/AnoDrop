#include "LobbiesView.h"
#include "ChatView.h"
#include <ncurses.h>

namespace ui
{
LobbiesView::LobbiesView(std::shared_ptr<AppFacade> appFacade, GuiPreferences *prefs, DhtChatHandler *chatHandler) : m_appFacade(appFacade), m_preferences(prefs), m_chatHandler(chatHandler)
{
}

void LobbiesView::show()
{
    bool inLobbyMenu = true;
    int selectedLobby = 0;

    timeout(200);

    while (inLobbyMenu)
    {
        auto allObservers = m_appFacade ? m_appFacade->getLobbies() : std::vector<std::shared_ptr<LobbyObserver>>{};
        std::vector<std::shared_ptr<LobbyObserver>> lobbiesObservers;
        for (const auto &obs : allObservers)
        {
            if (!obs->getLobbyData().is_hidden)
            {
                lobbiesObservers.push_back(obs);
            }
        }

        if (m_chatHandler->getChatCount() != lobbiesObservers.size())
        {
            m_chatHandler->clearChats();
            for (const auto &obs : lobbiesObservers)
            {
                auto lobbyPtr = std::dynamic_pointer_cast<Lobby>(obs);
                if (lobbyPtr)
                {
                    m_chatHandler->addChat(*lobbyPtr);
                }
            }
        }

        std::vector<LobbyData> lobbies;
        for (const auto &obs : lobbiesObservers)
        {
            lobbies.push_back(obs->getLobbyData());
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

        clear();
        mvprintw(1, 2, "=== LOBBIES ===");

        for (size_t i = 0; i < lobbies.size(); ++i)
        {
            if ((int)i == selectedLobby)
                attron(A_REVERSE);

            std::string lobbyName = lobbies[i].lobby_name.empty() ? "Unknown Lobby" : lobbies[i].lobby_name;
            std::string pseudo1 = lobbies[i].my_pseudo.empty() ? "Me" : lobbies[i].my_pseudo;
            std::string pseudo2 = lobbies[i].other_pseudo.empty() ? "?" : lobbies[i].other_pseudo;

            std::string dispName = lobbyName + " [" + pseudo1 + " - " + pseudo2 + "]";
            
            bool hasUnread = lobbies[i].history.messages.size() > lobbies[i].read_messages_count;

            if (dispName.length() > 50)
                dispName = dispName.substr(0, 50) + "...";

            mvprintw(3 + static_cast<int>(i), 2, "%zu: %-52s", i + 1, dispName.c_str());

            int colorPair = 1;

            std::string statusStr;
            switch (lobbies[i].status)
            {
            case LobbyStatus::PENDING_HANDSHAKE:
                statusStr = "[PENDING]";
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

            if (lobbies[i].status == LobbyStatus::ANOMALY && !lobbies[i].anomaly_message.empty())
            {
                attron(COLOR_PAIR(4));
                printw(" - Anomaly: %s", lobbies[i].anomaly_message.c_str());
                attroff(COLOR_PAIR(4));
            }

            if (hasUnread) {
                attron(COLOR_PAIR(2) | A_BLINK | A_BOLD);
                printw("  * NEW MESSAGE *");
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
                auto &lobbyObs = lobbiesObservers[selectedLobby];
                m_appFacade->hideLobby(lobbyObs->getLobbyData().lobby_name);

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
        else if (ch == 10)
        { // ENTER
            if (lobbies[selectedLobby].status == LobbyStatus::HANDSHAKE_DONE || lobbies[selectedLobby].status == LobbyStatus::ANOMALY)
            {
                // Open specific Chat View
                timeout(-1); // Disable timeout to wait for user input infinitely in chat
                auto &lobbyObs = lobbiesObservers[selectedLobby];
                m_appFacade->synchronizeAndProcessNewIncomingMessage(lobbyObs);
                ChatView chatView(lobbyObs, m_chatHandler->getChat(selectedLobby));
                chatView.show();
                timeout(200); // Restore timeout
            }
        }
    }
    timeout(-1); // reset
    clear();
}
} // namespace ui
