#include "AnomalyFlagView.h"

#include <ncurses.h>

namespace ui
{

AnomalyFlagView::AnomalyFlagView(std::shared_ptr<AppFacade> appFacade, std::shared_ptr<LobbyStore> lobbyStore)
    : m_appFacade(appFacade), m_lobbyStore(lobbyStore)
{
}

void AnomalyFlagView::show()
{
    bool inView = true;
    int selected = 0;

    while (inView)
    {
        clear();
        mvprintw(2, 4, "=== Flag Lobby Anomaly ===");
        mvprintw(4, 4, "Select a lobby to flag, or choose Manual Entry:");

        std::vector<std::string> options;
        options.push_back("[Manual Entry]");

        if (m_lobbyStore)
        {
            auto lobbies = m_lobbyStore->getLobbies();
            for (const auto &obs : lobbies)
            {
                auto l = obs->getLobbyData();
                if (!l.is_hidden)
                {
                    options.push_back(l.lobby_name);
                }
            }
        }

        for (size_t i = 0; i < options.size(); ++i)
        {
            if ((int)i == selected)
                attron(A_REVERSE);
            mvprintw(6 + static_cast<int>(i), 6, "%zu. %s", i + 1, options[i].c_str());
            if ((int)i == selected)
                attroff(A_REVERSE);
        }

        mvprintw(LINES - 2, 2, "[UP/DOWN] Navigate | [ENTER] Select | [q] Back");
        refresh();

        int ch = getch();
        if (ch == 'q' || ch == 'Q')
        {
            inView = false;
        }
        else if (ch == KEY_UP && selected > 0)
        {
            selected--;
        }
        else if (ch == KEY_DOWN && selected < (int)options.size() - 1)
        {
            selected++;
        }
        else if (ch == 10)
        { // ENTER
            std::string targetLobby;
            if (selected == 0)
            {
                clear();
                mvprintw(2, 4, "=== Manual Anomaly Entry ===");
                mvprintw(4, 4, "Enter Lobby Name (Passphrase): ");
                echo();
                char buf[256];
                getnstr(buf, 255);
                noecho();
                targetLobby = buf;
            }
            else
            {
                targetLobby = options[selected];
                clear();
                mvprintw(2, 4, "=== Flag Lobby Anomaly ===");
                mvprintw(4, 4, "Selected Lobby: %s", targetLobby.c_str());
            }

            if (!targetLobby.empty())
            {
                mvprintw(6, 4, "Enter Anomaly Message (optional): ");
                echo();
                char msgBuf[256];
                getnstr(msgBuf, 255);
                noecho();
                std::string anomalyMsg(msgBuf);

                if (m_appFacade)
                {
                    m_appFacade->flagAnomaly(targetLobby, anomalyMsg);
                    mvprintw(8, 4, "Anomaly broadcasted for '%s'!", targetLobby.c_str());
                    mvprintw(10, 4, "Press any key to return...");
                    getch();
                    inView = false;
                }
            }
        }
    }
}
} // namespace ui
