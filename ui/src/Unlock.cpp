#include "Unlock.h"

#include <ncurses.h>
#include <openssl/crypto.h>

namespace ui
{

std::string Unlock::askMasterPassword(const std::string& errorMessage)
{
    clear();
    mvprintw(2, 4, "=== AnoDrop Security ===");
    if (!errorMessage.empty()) {
        attron(A_BOLD);
        mvprintw(3, 4, "Error: %s", errorMessage.c_str());
        attroff(A_BOLD);
    }
    mvprintw(4, 4, "Enter your Master Password (will create/unlock your local store): ");

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
    if (rc == ERR)
    {
        OPENSSL_cleanse(pwdBuf, sizeof(pwdBuf));
        return "";
    }

    std::string pwd(pwdBuf);
    OPENSSL_cleanse(pwdBuf, sizeof(pwdBuf));
    return pwd;
}

} // namespace ui
