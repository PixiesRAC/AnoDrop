#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <vector>

// Small VT screen model for the ASCII ncurses views under TERM=xterm.
// Assertions inspect the current screen, never stale text from the raw PTY stream.
class Terminal
{
  public:
    static constexpr int rows = 60, cols = 240;
    std::array<std::string, rows> lines;
    Terminal()
    {
        clear();
    }
    void clear()
    {
        for (auto &line : lines)
            line.assign(cols, ' ');
    }
    std::string text() const
    {
        std::string result;
        for (const auto &line : lines)
            result += line + '\n';
        return result;
    }
    void feed(const std::string &bytes)
    {
        for (unsigned char ch : bytes)
        {
            if (state == Osc)
            {
                if (ch == 7)
                    state = Normal;
                else if (ch == 27)
                    state = OscEscape;
                continue;
            }
            if (state == OscEscape)
            {
                state = ch == '\\' ? Normal : Osc;
                continue;
            }
            if (state == Charset)
            {
                state = Normal;
                continue;
            }
            if (state == Escape)
            {
                state = Normal;
                if (ch == '[')
                {
                    state = Csi;
                    params.clear();
                }
                else if (ch == ']')
                    state = Osc;
                else if (ch == '(' || ch == ')' || ch == '*' || ch == '+')
                    state = Charset;
                else if (ch == '7')
                {
                    savedX = x;
                    savedY = y;
                }
                else if (ch == '8')
                {
                    x = savedX;
                    y = savedY;
                }
                else if (ch == 'M')
                {
                    if (y == top)
                        scrollDown();
                    else
                        y = std::max(0, y - 1);
                }
                else if (ch == 'D')
                    newline();
                else if (ch == 'E')
                {
                    x = 0;
                    newline();
                }
                else if (ch == 'c')
                {
                    clear();
                    x = y = 0;
                }
                continue;
            }
            if (state == Csi)
            {
                if (ch >= 0x40 && ch <= 0x7e)
                {
                    csi(static_cast<char>(ch));
                    state = Normal;
                }
                else
                    params += static_cast<char>(ch);
                continue;
            }
            if (ch == 27)
            {
                state = Escape;
                continue;
            }
            if (ch == '\r')
            {
                x = 0;
                continue;
            }
            if (ch == '\n' || ch == '\v' || ch == '\f')
            {
                newline();
                continue;
            }
            if (ch == '\b')
            {
                x = std::max(0, x - 1);
                continue;
            }
            if (ch == '\t')
            {
                x = std::min(cols - 1, (x / 8 + 1) * 8);
                continue;
            }
            if (ch < 32 || ch == 127)
                continue;
            // UTF-8 continuation bytes do not occupy additional cells.
            if ((ch & 0xc0) == 0x80)
                continue;
            if (x >= cols)
            {
                x = 0;
                newline();
            }
            lastCharacter = ch < 128 ? static_cast<char>(ch) : '?';
            lines[y][x++] = lastCharacter;
        }
    }

  private:
    enum State
    {
        Normal,
        Escape,
        Csi,
        Charset,
        Osc,
        OscEscape
    } state = Normal;
    int x = 0, y = 0, savedX = 0, savedY = 0, top = 0, bottom = rows - 1;
    std::string params;
    char lastCharacter = ' ';
    void scrollUp()
    {
        for (int r = top; r < bottom; ++r)
            lines[r] = lines[r + 1];
        lines[bottom].assign(cols, ' ');
    }
    void scrollDown()
    {
        for (int r = bottom; r > top; --r)
            lines[r] = lines[r - 1];
        lines[top].assign(cols, ' ');
    }
    void newline()
    {
        if (y == bottom)
            scrollUp();
        else
            y = std::min(rows - 1, y + 1);
    }
    void csi(char command)
    {
        bool privateMode = params.starts_with('?');
        if (privateMode)
            params.erase(0, 1);
        std::vector<int> p{0};
        for (char ch : params)
        {
            if (ch == ';')
                p.push_back(0);
            else if (ch >= '0' && ch <= '9')
                p.back() = std::min(10000, p.back() * 10 + ch - '0');
        }
        auto value = [&](size_t i, int fallback)
        { return i < p.size() && p[i] ? p[i] : fallback; };
        int n = std::min(value(0, 1), cols);
        switch (command)
        {
        case 'b':
            for (int i = 0; i < n; ++i)
            {
                if (x >= cols)
                {
                    x = 0;
                    newline();
                }
                lines[y][x++] = lastCharacter;
            }
            break;
        case 'H':
        case 'f':
            y = std::clamp(value(0, 1) - 1, 0, rows - 1);
            x = std::clamp(value(1, 1) - 1, 0, cols - 1);
            break;
        case 'A':
            y = std::max(0, y - n);
            break;
        case 'B':
        case 'e':
            y = std::min(rows - 1, y + n);
            break;
        case 'C':
        case 'a':
            x = std::min(cols - 1, x + n);
            break;
        case 'D':
            x = std::max(0, x - n);
            break;
        case 'G':
        case '`':
            x = std::clamp(n - 1, 0, cols - 1);
            break;
        case 'd':
            y = std::clamp(n - 1, 0, rows - 1);
            break;
        case 'J':
            if (p[0] == 2 || p[0] == 3)
                clear();
            else if (p[0] == 0)
            {
                lines[y].replace(std::min(x, cols), cols - std::min(x, cols), cols - std::min(x, cols), ' ');
                for (int r = y + 1; r < rows; ++r)
                    lines[r].assign(cols, ' ');
            }
            else
            {
                for (int r = 0; r < y; ++r)
                    lines[r].assign(cols, ' ');
                lines[y].replace(0, std::min(x + 1, cols), std::min(x + 1, cols), ' ');
            }
            break;
        case 'K':
        {
            int start = p[0] == 0 ? std::min(x, cols) : 0;
            int end = p[0] == 1 ? std::min(x + 1, cols) : cols;
            lines[y].replace(start, end - start, end - start, ' ');
            break;
        }
        case 'X':
        {
            int count = std::min(n, cols - std::min(x, cols));
            lines[y].replace(std::min(x, cols), count, count, ' ');
            break;
        }
        case 'P':
            lines[y].erase(std::min(x, cols), n);
            lines[y].resize(cols, ' ');
            break;
        case '@':
            lines[y].insert(std::min(x, cols), n, ' ');
            lines[y].resize(cols);
            break;
        case 'L':
            for (int i = 0; i < n; ++i)
            {
                for (int r = bottom; r > y; --r)
                    lines[r] = lines[r - 1];
                lines[y].assign(cols, ' ');
            }
            break;
        case 'M':
            for (int i = 0; i < n; ++i)
            {
                for (int r = y; r < bottom; ++r)
                    lines[r] = lines[r + 1];
                lines[bottom].assign(cols, ' ');
            }
            break;
        case 'S':
            for (int i = 0; i < n; ++i)
                scrollUp();
            break;
        case 'T':
            for (int i = 0; i < n; ++i)
                scrollDown();
            break;
        case 'r':
            top = std::clamp(value(0, 1) - 1, 0, rows - 1);
            bottom = std::clamp(value(1, rows) - 1, top, rows - 1);
            x = y = 0;
            break;
        case 's':
            savedX = x;
            savedY = y;
            break;
        case 'u':
            x = savedX;
            y = savedY;
            break;
        case 'h':
            if (privateMode && (p[0] == 1049 || p[0] == 1047))
            {
                clear();
                x = y = 0;
            }
            break;
        default:
            break; // colors, cursor visibility and terminal modes do not change text.
        }
    }
};
