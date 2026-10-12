#include "ChangePasswordView.h"

#include <ncurses.h>
#include <openssl/crypto.h>

namespace ui
{
ChangePasswordView::ChangePasswordView(std::shared_ptr<AppFacade> appFacade) : m_appFacade(appFacade)
{
}

void ChangePasswordView::show()
{
    clear();
    mvprintw(2, 4, "=== Change Master Password ===");
    mvprintw(4, 4, "Enter new Master Password: ");

    noecho();
    curs_set(1);
    char pwdBuf[256];
    getnstr(pwdBuf, 255);
    curs_set(0);

    std::string newPwd(pwdBuf);
    OPENSSL_cleanse(pwdBuf, sizeof(pwdBuf));
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
