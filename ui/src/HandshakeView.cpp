#include "HandshakeView.h"

#include <ncurses.h>
#include <openssl/crypto.h>

#include "SecureBuffer.h"

namespace ui
{
HandshakeView::HandshakeView(std::shared_ptr<AppFacade> appFacade) : m_appFacade(std::move(appFacade))
{
}

namespace
{
struct TerminalModeGuard
{
    TerminalModeGuard()
    {
        echo();
        curs_set(1);
    }
    ~TerminalModeGuard()
    {
        restore();
    }
    void restore()
    {
        if (!restored)
        {
            noecho();
            curs_set(0);
            timeout(-1);
            restored = true;
        }
    }
    TerminalModeGuard(const TerminalModeGuard &) = delete;
    TerminalModeGuard &operator=(const TerminalModeGuard &) = delete;
  private:
    bool restored{false};
};
} // namespace

void HandshakeView::show()
{
    clear();
    mvprintw(2, 4, "=== Handshake ===");
    mvprintw(4, 4, "Enter Channel Name (Passphrase): ");

    std::string passphrase;
    std::string pseudo;
    {
        TerminalModeGuard modeGuard;

        char channelName[256] = {0};
        if (getnstr(channelName, 255) == ERR)
        {
            OPENSSL_cleanse(channelName, sizeof(channelName));
            return;
        }
        passphrase = channelName;
        OPENSSL_cleanse(channelName, sizeof(channelName));
    }
    ScopedCleanse passphraseGuard(passphrase);
    {
        TerminalModeGuard modeGuard;

        if (m_appFacade && m_appFacade->unhideLobbyIfExists(passphrase))
        {
            modeGuard.restore();
            mvprintw(7, 4, "Lobby was hidden. It is now restored!");
            mvprintw(9, 4, "Press any key to return to menu...");
            refresh();
            getch();
            return;
        }

        mvprintw(5, 4, "Enter your Pseudo: ");
        char pseudoBuf[256] = {0};
        if (getnstr(pseudoBuf, 255) == ERR)
        {
            OPENSSL_cleanse(pseudoBuf, sizeof(pseudoBuf));
            return;
        }

        pseudo = pseudoBuf;
        OPENSSL_cleanse(pseudoBuf, sizeof(pseudoBuf));
    }

    try
    {
        if (!m_appFacade)
            throw std::runtime_error("Facade not initialized.");
        HandshakeKeyService::ChannelKeys channelKeys = m_appFacade->initiateHandshake(passphrase, pseudo);

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
