#include "ui/GuiBuilder.h"
#include "chat/DhtChatHandler.h"
#include "ui/AnomalyFlagView.h"
#include "ui/ChangePasswordView.h"
#include "ui/HandshakeView.h"
#include "ui/LobbiesView.h"

#include <ncurses.h>
#include <spdlog/spdlog.h>

namespace ui
{

GuiBuilder::GuiBuilder() : m_running(false), m_selectedOption(1), m_chatHandler(std::make_unique<DhtChatHandler>())
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
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_YELLOW, -1);
        init_pair(3, COLOR_GREEN, -1);
        init_pair(4, COLOR_RED, -1);
        init_pair(5, COLOR_MAGENTA, -1);
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
    if (m_chatHandler)
        m_chatHandler->setDhtPublisher(m_dhtPublisher);
}
void GuiBuilder::setLobbyStore(std::shared_ptr<LobbyStore> lobbyStore)
{
    m_lobbyStore = lobbyStore;
    if (m_lobbyStore)
        m_preferences = std::make_unique<GuiPreferences>(m_lobbyStore);
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

void GuiBuilder::showMainMenu()
{
    clear();
    mvprintw(2, 4, "=== AnoDrop: Decentralized Messaging ===");

    if (m_selectedOption == 1)
        attron(A_REVERSE);
    mvprintw(4, 4, "1. Handshake (Find new lobbies)");
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
    mvprintw(7, 4, "4. Flag Lobby Anomaly");
    if (m_selectedOption == 4)
        attroff(A_REVERSE);

    if (m_selectedOption == 5)
        attron(A_REVERSE);
    mvprintw(8, 4, "5. Exit");
    if (m_selectedOption == 5)
        attroff(A_REVERSE);

    mvprintw(10, 4, "Use ARROWS to navigate, ENTER to select.");
    refresh();
}

void GuiBuilder::buildAndRun()
{
    m_running = true;
    while (m_running)
    {
        showMainMenu();
        int choice = getch();
        if (choice == KEY_UP && m_selectedOption > 1)
            m_selectedOption--;
        else if (choice == KEY_DOWN && m_selectedOption < 5)
            m_selectedOption++;
        else if (choice == 10)
        { // ENTER
            if (m_selectedOption == 5)
            {
                m_running = false;
                continue;
            }

            std::unique_ptr<IView> view;
            switch (m_selectedOption)
            {
            case 1:
                view = std::make_unique<HandshakeView>(m_appFacade);
                break;
            case 2:
                view = std::make_unique<LobbiesView>(m_appFacade, m_preferences.get(), m_chatHandler.get());
                break;
            case 3:
                view = std::make_unique<ChangePasswordView>(m_appFacade);
                break;
            case 4:
                view = std::make_unique<AnomalyFlagView>(m_appFacade, m_lobbyStore);
                break;
            }

            if (view)
            {
                view->show();
            }
        }
        else if (choice == 'q' || choice == 'Q')
            m_running = false;
    }
}
} // namespace ui
