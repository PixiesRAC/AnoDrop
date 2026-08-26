#include "ui/GuiBuilder.h"

#include <algorithm>
#include <ncurses.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

constexpr int GUI_KEY_TAB = 9;
constexpr int GUI_KEY_ENTER = 10;

namespace ui
{

GuiBuilder::GuiBuilder() : m_running(false), m_selectedOption(1), m_focusRightPane(false), m_activeChatIndex(0)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors())
    {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);   // Borders and focus
        init_pair(2, COLOR_YELLOW, -1); // PENDING_HANDSHAKE
        init_pair(3, COLOR_GREEN, -1);  // HANDSHAKE_DONE
        init_pair(4, COLOR_RED, -1);    // ANOMALY
    }
}

GuiBuilder::~GuiBuilder()
{
    SPDLOG_INFO("~GuiBuilder: closing ncurses...");
    endwin();
}

void GuiBuilder::setDhtPublisher(std::shared_ptr<DhtPublisher> dhtPublisher)
{
    m_dhtPublisher = std::move(dhtPublisher);
}

void GuiBuilder::setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore)
{
    m_lobbyStore = lobbyStore;
    if (m_lobbyStore)
    {
        m_preferences = std::make_unique<GuiPreferences>(m_lobbyStore);
    }
}

void GuiBuilder::setAppFacade(std::shared_ptr<AppFacade> appFacade)
{
    m_appFacade = std::move(appFacade);
}

void GuiBuilder::showLoadingScreen()
{
    clear();
    mvprintw(2, 4, "=== AnoDrop: Decentralized Messaging ===");
    mvprintw(4, 4, "Bootstrapping DHT network... Please wait...");
    refresh();
}

void GuiBuilder::revealLobbies()
{
    if (!m_appFacade)
        return;
    auto lobbiesObservers = m_appFacade->getLobbies();
    std::vector<LobbyData> lobbies;
    for (const auto &obs : lobbiesObservers)
    {
        lobbies.push_back(obs->getLobbyData());
    }
    if (lobbies.empty())
    {
        clear();
        mvprintw(4, 4, "No lobbies found in your local store.");
        mvprintw(5, 4, "(The vault is empty, or the Master Password was incorrect)");
        mvprintw(7, 4, "Press any key to return to Main Menu...");
        refresh();
        getch();
        return;
    }

    if (m_preferences)
    {
        m_preferences->load(lobbies, m_openChats);
    }
    lobbyMenuLoop();
}

void GuiBuilder::lobbyMenuLoop()
{
    bool inLobbyMenu = true;
    int selectedLobby = 0;

    int leftWidth = COLS / 3;
    int rightWidth = COLS - leftWidth;

    WINDOW *leftWin = newwin(LINES, leftWidth, 0, 0);
    WINDOW *rightWin = newwin(LINES, rightWidth, 0, leftWidth);
    keypad(leftWin, TRUE);
    keypad(rightWin, TRUE);

    wtimeout(leftWin, 200);
    wtimeout(rightWin, 200);

    bool m_uiNeedsRefresh = true;
    std::vector<LobbyData> lobbies;

    while (inLobbyMenu)
    {
        auto lobbiesObservers = m_appFacade ? m_appFacade->getLobbies() : std::vector<std::shared_ptr<LobbyObserver>>{};
        for (const auto &obs : lobbiesObservers)
        {
            if (obs->consumeUpdateFlag())
            {
                m_uiNeedsRefresh = true;
            }
        }

        if (m_uiNeedsRefresh)
        {
            m_uiNeedsRefresh = false;
            lobbies.clear();
            for (const auto &obs : lobbiesObservers)
            {
                lobbies.push_back(obs->getLobbyData());
            }

            // --- DRAW LEFT WINDOW (LOBBIES) ---
            wclear(leftWin);
            box(leftWin, 0, 0);

            if (!m_focusRightPane)
                wattron(leftWin, A_BOLD | COLOR_PAIR(1)); // Highlight border if focused
            mvwprintw(leftWin, 1, 2, "=== LOBBIES ===");
            if (!m_focusRightPane)
                wattroff(leftWin, A_BOLD | COLOR_PAIR(1));

            for (size_t i = 0; i < lobbies.size(); ++i)
            {
                if (i == selectedLobby && !m_focusRightPane)
                    wattron(leftWin, A_REVERSE);

                std::string dispName = lobbies[i].lobby_name.empty() ? (lobbies[i].my_x25519_pub.empty() ? "Unknown" : lobbies[i].my_x25519_pub) : lobbies[i].lobby_name;
                if (dispName.length() > 12)
                    dispName = dispName.substr(0, 12) + "...";

                mvwprintw(leftWin, 3 + i, 2, "%zu: %-15s", i + 1, dispName.c_str());

                // Display status with colors
                int colorPair = 1;
                std::string statusStr;
                switch (lobbies[i].status)
                {
                case LobbyStatus::PENDING_HANDSHAKE:
                    statusStr = "[PENDING]";
                    colorPair = 2; // Yellow
                    break;
                case LobbyStatus::HANDSHAKE_DONE:
                    statusStr = "[READY]";
                    colorPair = 3; // Green
                    break;
                case LobbyStatus::ANOMALY:
                    statusStr = "[ERROR]";
                    colorPair = 4; // Red
                    break;
                }

                wattron(leftWin, COLOR_PAIR(colorPair));
                wprintw(leftWin, "%s", statusStr.c_str());
                wattroff(leftWin, COLOR_PAIR(colorPair));

                if (i == selectedLobby && !m_focusRightPane)
                    wattroff(leftWin, A_REVERSE);
            }
            mvwprintw(leftWin, LINES - 3, 2, "[TAB] Switch Panel");
            mvwprintw(leftWin, LINES - 2, 2, "[ENTER] Open | [q] Back");
            wrefresh(leftWin);

            // --- DRAW RIGHT WINDOW (CHATS) ---
            wclear(rightWin);
            box(rightWin, 0, 0);

            if (m_focusRightPane)
                wattron(rightWin, A_BOLD | COLOR_PAIR(1));
            mvwprintw(rightWin, 1, 2, "=== CHAT AREA ===");
            if (m_focusRightPane)
                wattroff(rightWin, A_BOLD | COLOR_PAIR(1));

            if (m_openChats.empty())
            {
                mvwprintw(rightWin, LINES / 2, rightWidth / 2 - 15, "No conversation selected.");
                mvwprintw(rightWin, LINES / 2 + 1, rightWidth / 2 - 20, "Press ENTER on a lobby to open it here.");
            }
            else
            {
                int chatHeight = (LINES - 4) / m_openChats.size();

                for (size_t i = 0; i < m_openChats.size(); ++i)
                {
                    int chatIdx = m_openChats[i];
                    int startY = 2 + (i * chatHeight);

                    // Draw separator
                    if (i > 0)
                        mvwhline(rightWin, startY - 1, 1, ACS_HLINE, rightWidth - 2);

                    if (m_focusRightPane && i == m_activeChatIndex)
                        wattron(rightWin, A_REVERSE);
                    std::string chatName = lobbies[chatIdx].lobby_name.empty() ? "Unknown Lobby" : lobbies[chatIdx].lobby_name;
                    mvwprintw(rightWin, startY, 2, "[ %s ]", chatName.c_str());
                    if (m_focusRightPane && i == m_activeChatIndex)
                        wattroff(rightWin, A_REVERSE);

                    mvwprintw(rightWin, startY + 2, 2, "Chat history...");
                }
                mvwprintw(rightWin, LINES - 2, 2, "[TAB] Switch Panel | [UP/DOWN] Select Chat | [c] Close Chat");
            }
            wrefresh(rightWin);
        } // End of m_uiNeedsRefresh block

        // --- HANDLE INPUT ---
        int ch = m_focusRightPane ? wgetch(rightWin) : wgetch(leftWin);

        if (ch == ERR)
        {
            continue;
        }

        m_uiNeedsRefresh = true; // Any key press dirties the UI

        if (ch == 'q' || ch == 'Q')
        {
            inLobbyMenu = false;
        }
        else if (ch == GUI_KEY_TAB)
        { // TAB
            m_focusRightPane = !m_focusRightPane;
            if (m_focusRightPane && m_openChats.empty())
                m_focusRightPane = false; // Can't focus right if empty
        }
        else
        {
            if (!m_focusRightPane)
            {
                // Left pane logic
                if (ch == KEY_UP && selectedLobby > 0)
                {
                    selectedLobby--;
                }
                else if (ch == KEY_DOWN && selectedLobby < (int)lobbies.size() - 1)
                {
                    selectedLobby++;
                }
                else if (ch == GUI_KEY_ENTER)
                { // Enter
                    if (std::find(m_openChats.begin(), m_openChats.end(), selectedLobby) == m_openChats.end())
                    {
                        m_openChats.push_back(selectedLobby);
                        m_activeChatIndex = m_openChats.size() - 1;

                        if (m_preferences)
                        {
                            m_preferences->save(lobbies, m_openChats);
                        }
                    }
                }
            }
            else
            {
                // Right pane logic
                if (ch == KEY_UP && m_activeChatIndex > 0)
                {
                    m_activeChatIndex--;
                }
                else if (ch == KEY_DOWN && m_activeChatIndex < (int)m_openChats.size() - 1)
                {
                    m_activeChatIndex++;
                }
                else if (ch == 'c' || ch == 'C')
                {
                    if (!m_openChats.empty())
                    {
                        m_openChats.erase(m_openChats.begin() + m_activeChatIndex);
                        if (m_activeChatIndex >= m_openChats.size() && m_activeChatIndex > 0)
                        {
                            m_activeChatIndex--;
                        }
                        if (m_openChats.empty())
                            m_focusRightPane = false;

                        if (m_preferences)
                        {
                            m_preferences->save(lobbies, m_openChats);
                        }
                    }
                }
            }
        }
    }

    delwin(leftWin);
    delwin(rightWin);
    clear();
}

void GuiBuilder::buildAndRun()
{
    m_running = true;

    while (m_running)
    {
        showMainMenu();
        int choice = getch();
        if (choice == KEY_UP)
        {
            if (m_selectedOption > 1)
                m_selectedOption--;
        }
        else if (choice == KEY_DOWN)
        {
            if (m_selectedOption < 4) // Update max option
                m_selectedOption++;
        }
        else if (choice == GUI_KEY_ENTER)
        {
            if (m_selectedOption == 4)
            {
                m_running = false;
            }
            else if (m_selectedOption == 3)
            {
                handleChangeMasterPassword();
            }
            else if (m_selectedOption == 2)
            {
                revealLobbies();
            }
            else if (m_selectedOption == 1)
            {
                handleHandShake();
            }
        }
        else if (choice == 'q' || choice == 'Q')
        {
            m_running = false;
        }
    }
}

void GuiBuilder::showMainMenu()
{
    clear();
    mvprintw(2, 4, "=== AnoDrop: Decentralized Messaging ===");

    if (m_selectedOption == 1)
        attron(A_REVERSE);
    mvprintw(4, 4, "1. Public Rendezvous (Password-Based Generation)");
    if (m_selectedOption == 1)
        attroff(A_REVERSE);

    if (m_selectedOption == 2)
        attron(A_REVERSE);
    mvprintw(5, 4, "2. Display Unlocked Lobbies");
    if (m_selectedOption == 2)
        attroff(A_REVERSE);

    if (m_selectedOption == 3)
        attron(A_REVERSE);
    mvprintw(6, 4, "3. Change Master Password");
    if (m_selectedOption == 3)
        attroff(A_REVERSE);

    if (m_selectedOption == 4)
        attron(A_REVERSE);
    mvprintw(7, 4, "4. Exit");
    if (m_selectedOption == 4)
        attroff(A_REVERSE);

    mvprintw(9, 4, "Use ARROWS to navigate, ENTER to select.");
    refresh();
}

void GuiBuilder::handleChangeMasterPassword()
{
    clear();
    mvprintw(2, 4, "=== Change Master Password ===");
    mvprintw(4, 4, "Enter new Master Password: ");

    curs_set(1);

    char pwdBuf[256];
    getnstr(pwdBuf, 255);

    curs_set(0);

    std::string newPwd(pwdBuf);
    if (newPwd.empty())
    {
        mvprintw(6, 4, "Password cannot be empty! Aborting.");
    }
    else
    {
        if (m_appFacade)
        {
            m_appFacade->updateMasterPassword(newPwd);
        }
        mvprintw(6, 4, "Master Password updated successfully!");
        mvprintw(7, 4, "All lobbies and preferences have been re-encrypted.");
    }

    mvprintw(9, 4, "Press any key to return to menu...");
    refresh();
    getch();
}

void GuiBuilder::handleHandShake()
{
    clear();
    mvprintw(2, 4, "=== Public Rendezvous ===");
    mvprintw(4, 4, "Enter Channel Name (Passphrase): ");

    echo();
    curs_set(1);
    char channelName[256];
    getnstr(channelName, 255);

    mvprintw(5, 4, "Enter your Pseudo: ");
    char pseudoBuf[256];
    getnstr(pseudoBuf, 255);

    noecho();
    curs_set(0);

    std::string passphrase(channelName);
    std::string pseudo(pseudoBuf);

    try
    {
        if (!m_appFacade)
        {
            throw std::runtime_error("Facade not initialized.");
        }

        HandshakeManager::ChannelKeys channelKeys = m_appFacade->initiateHandshake(passphrase, pseudo);

        mvprintw(7, 4, "Public Key : %s", channelKeys.pubKeyHex.c_str());
        mvprintw(8, 4, "Target ID  : %s", channelKeys.targetIDHex.c_str());
        refresh();

        mvprintw(10, 4, "Lobby added to store! The background fetcher");
        mvprintw(11, 4, "will now dispatch it to the DHT network.");
        mvprintw(12, 4, "Check 'Display Unlocked Lobbies' for status.");
    }
    catch (const std::exception &e)
    {
        attron(COLOR_PAIR(4));
        mvprintw(14, 4, "Error: %s", e.what());
        attroff(COLOR_PAIR(4));
    }

    mvprintw(16, 4, "Press any key to return to menu...");
    refresh();
    getch();
}

} // namespace ui
