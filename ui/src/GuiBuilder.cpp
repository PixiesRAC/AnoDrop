#include "GuiBuilder.h"

#include <cstdlib>
#include <ncurses.h>
#include <termios.h>
#include <unistd.h>
#include <spdlog/spdlog.h>

#include "AnomalyFlagView.h"
#include "ChangePasswordView.h"
#include "HandshakeView.h"
#include "LobbiesView.h"
#include "KillSwitch.h"

namespace ui
{

namespace {
termios initialTerminal{};
bool haveInitialTerminal = false;
void onQuickExitResetTerminal()
{
    // quick_exit may run on the network thread: never enter ncurses here.
    if (haveInitialTerminal) ::tcsetattr(STDIN_FILENO, TCSANOW, &initialTerminal);
}
}

GuiBuilder::GuiBuilder() : m_running(false), m_selectedOption(1)
{
    haveInitialTerminal = ::tcgetattr(STDIN_FILENO, &initialTerminal) == 0;
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    std::at_quick_exit(onQuickExitResetTerminal);

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

void GuiBuilder::shutdown()
{
    resetTerminal();
    if (m_appFacade)
    {
        m_appFacade->shutdown();
        m_appFacade.reset();
    }
}

GuiBuilder::~GuiBuilder()
{
    SPDLOG_INFO("[USER] ~GuiBuilder: closing ncurses and shutting down...");
    shutdown();
}

void GuiBuilder::resetTerminal()
{
    if (stdscr != nullptr)
    {
        if (!isendwin())
        {
            endwin();
        }
        if (SCREEN *sp = set_term(nullptr))
        {
            set_term(sp);
            delscreen(sp);
        }
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
        if (KillSwitch::getInstance().isTriggered())
        {
            m_running = false;
            break;
        }

        showMainMenu();
        int choice = getch();
        if (choice == KEY_UP && m_selectedOption > 1)
            m_selectedOption--;
        else if (choice == KEY_DOWN && m_selectedOption < 5)
            m_selectedOption++;
        else if (choice == 10 || choice == 13 || choice == KEY_ENTER)
        { // ENTER
            SPDLOG_INFO("[USER] Menu selected action={}", m_selectedOption == 1 ? "handshake" : m_selectedOption == 2 ? "lobbies" : m_selectedOption == 3 ? "change_password" : m_selectedOption == 4 ? "report_anomaly" : "quit");
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
                view = std::make_unique<LobbiesView>(m_appFacade);
                break;
            case 3:
                view = std::make_unique<ChangePasswordView>(m_appFacade);
                break;
            case 4:
                view = std::make_unique<AnomalyFlagView>(m_appFacade);
                break;
            }

            if (view)
            {
                try
                {
                    view->show();
                }
                catch (const std::exception &e)
                {
                    SPDLOG_ERROR("[USER] Exception caught in view {}: {}", m_selectedOption, e.what());
                }
            }
        }
        else if (choice == 'q' || choice == 'Q')
            m_running = false;
    }
}
} // namespace ui
