#include "Unlock.h"

#include <ncurses.h>

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

    noecho(); // Make sure noecho() is active
    curs_set(1);
    char pwdBuf[256];
    getnstr(pwdBuf, 255);
    curs_set(0);

    return std::string(pwdBuf);
}

} // namespace ui
