#include "HandshakeView.h"
#include <ncurses.h>

namespace ui {
HandshakeView::HandshakeView(std::shared_ptr<AppFacade> appFacade) : m_appFacade(appFacade) {}

void HandshakeView::show() {
    clear();
    mvprintw(2, 4, "=== Handshake ===");
    mvprintw(4, 4, "Enter Channel Name (Passphrase): ");

    echo();
    curs_set(1);
    char channelName[256];
    getnstr(channelName, 255);
    std::string passphrase(channelName);

    if (m_appFacade && m_appFacade->unhideLobbyIfExists(passphrase))
    {
        noecho();
        curs_set(0);
        mvprintw(7, 4, "Lobby was hidden. It is now restored!");
        mvprintw(9, 4, "Press any key to return to menu...");
        refresh();
        getch();
        return;
    }

    mvprintw(5, 4, "Enter your Pseudo: ");
    char pseudoBuf[256];
    getnstr(pseudoBuf, 255);

    noecho();
    curs_set(0);

    std::string pseudo(pseudoBuf);

    try {
        if (!m_appFacade) throw std::runtime_error("Facade not initialized.");
        HandshakeKeyService::ChannelKeys channelKeys = m_appFacade->initiateHandshake(passphrase, pseudo);

        mvprintw(7, 4, "Public Key : %s", channelKeys.pubKeyHex.c_str());
        mvprintw(8, 4, "Target ID  : %s", channelKeys.targetIDHex.c_str());
        refresh();

        mvprintw(10, 4, "Lobby added to store! The background fetcher");
        mvprintw(11, 4, "will now dispatch it to the DHT network.");
        mvprintw(12, 4, "Check 'Display Unlocked Lobbies' for status.");
    } catch (const std::exception &e) {
        attron(COLOR_PAIR(4));
        mvprintw(14, 4, "Error: %s", e.what());
        attroff(COLOR_PAIR(4));
    }

    mvprintw(16, 4, "Press any key to return to menu...");
    refresh();
    getch();
}
}
