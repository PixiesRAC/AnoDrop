#include "ChangePasswordView.h"

#include <ncurses.h>
#include <openssl/crypto.h>

#include "SecureBuffer.h"

namespace ui
{
ChangePasswordView::ChangePasswordView(std::shared_ptr<AppFacade> appFacade) : m_appFacade(std::move(appFacade))
{
}

void ChangePasswordView::show()
{
    clear();
    mvprintw(2, 4, "=== Change Master Password ===");
    mvprintw(4, 4, "Enter new Master Password: ");

    struct CursorGuard
    {
        ~CursorGuard()
        {
            curs_set(0);
            noecho();
        }
    } guard;

    noecho();
    curs_set(1);
    char pwdBuf[256]{};
    int rc = getnstr(pwdBuf, static_cast<int>(sizeof(pwdBuf) - 1));
    std::string newPwd;
    if (rc != ERR)
    {
        newPwd = pwdBuf;
    }
    OPENSSL_cleanse(pwdBuf, sizeof(pwdBuf));
    ScopedCleanse pwdGuard(newPwd);
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
} // namespace ui
