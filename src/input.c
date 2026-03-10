/*
  input.c - fast line editor with arrows & history
  Cross-platform: Win32 Console / POSIX termios
*/

#include "input.h"
#include "history.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  static HANDLE g_hIn = NULL, g_hOut = NULL;
  static DWORD g_oldModeIn = 0, g_oldModeOut = 0;
  static CONSOLE_CURSOR_INFO g_oldCursorInfo;
#else
  #include <unistd.h>
  #include <termios.h>
  #include <limits.h>
  static struct termios g_oldtio;
#endif

static char linebuf[MAX_INPUT];
static int inited = 0;

/* Read 1 byte from stdin (cross-platform) */
static int read_stdin_byte(char *out) {
#if defined(_WIN32) || defined(_WIN64)
    DWORD n = 0;
    if (!ReadFile(g_hIn, out, 1, &n, NULL) || n == 0) return -1;
    return 1;
#else
    ssize_t r = read(STDIN_FILENO, out, 1);
    if (r <= 0) return -1;
    return 1;
#endif
}

/* Init terminal / console */
int input_init(void) {
    if (inited) return 0;

#if defined(_WIN32) || defined(_WIN64)
    g_hIn = GetStdHandle(STD_INPUT_HANDLE);
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_hIn == INVALID_HANDLE_VALUE || g_hOut == INVALID_HANDLE_VALUE) return -1;

    DWORD mIn = 0, mOut = 0;
    if (!GetConsoleMode(g_hIn, &mIn) || !GetConsoleMode(g_hOut, &mOut)) return -1;

    g_oldModeIn  = mIn;
    g_oldModeOut = mOut;

    /* Enable VT output + disable auto return */
    mOut |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(g_hOut, mOut);

    /* Disable cooked mode and echo, but keep processed input */
    mIn &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    mIn |=  ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(g_hIn, mIn);

    /* Hide cursor */
    if (GetConsoleCursorInfo(g_hOut, &g_oldCursorInfo)) {
        CONSOLE_CURSOR_INFO ci = g_oldCursorInfo;
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(g_hOut, &ci);
    }
#else
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &g_oldtio) < 0) return -1;

    t = g_oldtio;
    t.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    t.c_iflag &= ~(IXON | ICRNL);
    t.c_oflag &= ~(OPOST);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    fputs("\x1b[?25l", stdout); fflush(stdout);
#endif

    inited = 1;
    return 0;
}

/* Restore original terminal mode */
void input_restore(void) {
    if (!inited) return;

#if defined(_WIN32) || defined(_WIN64)
    SetConsoleMode(g_hIn,  g_oldModeIn);
    SetConsoleMode(g_hOut, g_oldModeOut);
    SetConsoleCursorInfo(g_hOut, &g_oldCursorInfo);
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_oldtio);
    fputs("\x1b[?25h", stdout); fflush(stdout);
#endif

    inited = 0;
}

/* Redraw prompt + buffer */
static void redraw_line(const char *prompt, char *buf, int len, int cursor) {
    fputs("\r", stdout);
    fputs(prompt, stdout);

    if (len > 0) fwrite(buf, 1, len, stdout);

    fputs("\x1b[K", stdout); /* clear right side */

    int prompt_len = (int)strlen(prompt);
    int pos = prompt_len + cursor;

    char esc[64];
    snprintf(esc, sizeof(esc), "\r\x1b[%dC", (pos >= 0 ? pos : 0));
    fputs(esc, stdout);

    fflush(stdout);
}

/* Decode arrow keys and special sequences */
static int read_key(void) {
    char c;
    if (read_stdin_byte(&c) <= 0) return 0;

    if (c == '\r' || c == '\n') return '\n';
    if (c == 3) return 3;       /* Ctrl-C */
    if (c == 8 || c == 127) return 127; /* Backspace */

    if (c != 27) return (unsigned char)c;

    /* Escape sequence */
    char s1 = 0, s2 = 0;
    if (read_stdin_byte(&s1) <= 0) return 0;
    if (read_stdin_byte(&s2) <= 0) return 0;

    if (s1 == '[') {
        if (s2 == 'A') return 1004; /* Up */
        if (s2 == 'B') return 1005; /* Down */
        if (s2 == 'C') return 1006; /* Right */
        if (s2 == 'D') return 1007; /* Left */
        if (s2 == 'H') return 1000; /* Home */
        if (s2 == 'F') return 1001; /* End */
    }

    return 0;
}

/* Read one interactive line */
int input_readline(char *outbuf, int maxlen) {
    char prompt[256];

#if defined(_WIN32) || defined(_WIN64)
    char user[128] = "user";
    DWORD unlen = sizeof(user);
    GetUserNameA(user, &unlen);

    char cwd[MAX_PATH];
    if (!GetCurrentDirectoryA(MAX_PATH, cwd)) strcpy(cwd, "?");

    snprintf(prompt, sizeof(prompt), "[%s] %s> ", user, cwd);
#else
    const char *user = getenv("USER");
    if (!user) user = "user";

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");

    snprintf(prompt, sizeof(prompt), "[%s] %s> ", user, cwd);
#endif

    /* Print prompt ONCE */
    fputs(prompt, stdout);
    fflush(stdout);

    int len = 0;
    int cursor = 0;
    int hist_idx = -1;

    linebuf[0] = 0;

    for (;;) {
        int k = read_key();
        if (!k) continue;

        /* Enter */
        if (k == '\n') {
            putchar('\n');
            linebuf[len] = '\0';

            /* IMPORTANT: DO NOT auto history_add here! */

            if (maxlen > 0) {
                strncpy(outbuf, linebuf, maxlen - 1);
                outbuf[maxlen - 1] = '\0';
            }
            return 0;
        }

        /* Ctrl-C */
        if (k == 3) {
            putchar('\n');
            outbuf[0] = '\0';
            return -1;
        }

        /* Backspace */
        if (k == 127) {
            if (cursor > 0) {
                memmove(linebuf + cursor - 1,
                        linebuf + cursor,
                        len - cursor + 1);
                cursor--;
                len--;
                redraw_line(prompt, linebuf, len, cursor);
            }
            continue;
        }

        /* History Up */
        if (k == 1004) {
            if (!history_count) continue;

            if (hist_idx == -1) hist_idx = history_count - 1;
            else if (hist_idx > 0) hist_idx--;

            strncpy(linebuf, history_buf[hist_idx], MAX_INPUT - 1);
            linebuf[MAX_INPUT - 1] = 0;

            len = cursor = (int)strlen(linebuf);
            redraw_line(prompt, linebuf, len, cursor);
            continue;
        }

        /* History Down */
        if (k == 1005) {
            if (!history_count) continue;

            if (hist_idx == -1) continue;
            hist_idx++;

            if (hist_idx >= history_count) {
                hist_idx = -1;
                linebuf[0] = 0;
                len = cursor = 0;
            } else {
                strncpy(linebuf, history_buf[hist_idx], MAX_INPUT - 1);
                linebuf[MAX_INPUT - 1] = 0;
                len = cursor = (int)strlen(linebuf);
            }

            redraw_line(prompt, linebuf, len, cursor);
            continue;
        }

        /* Left ← */
        if (k == 1007 && cursor > 0) {
            cursor--;
            redraw_line(prompt, linebuf, len, cursor);
            continue;
        }

        /* Right → */
        if (k == 1006 && cursor < len) {
            cursor++;
            redraw_line(prompt, linebuf, len, cursor);
            continue;
        }

        /* Printables */
        if (k >= 32 && k <= 126 && len < MAX_INPUT - 1) {
            memmove(linebuf + cursor + 1,
                    linebuf + cursor,
                    len - cursor + 1);
            linebuf[cursor] = (char)k;
            cursor++;
            len++;
            redraw_line(prompt, linebuf, len, cursor);
            continue;
        }
    }
}

/* Explicit cursor control */
void input_hide_cursor(void) {
#if defined(_WIN32) || defined(_WIN64)
    if (g_hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_CURSOR_INFO ci;
        GetConsoleCursorInfo(g_hOut, &ci);
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(g_hOut, &ci);
    }
#else
    fputs("\x1b[?25l", stdout); fflush(stdout);
#endif
}

void input_show_cursor(void) {
#if defined(_WIN32) || defined(_WIN64)
    if (g_hOut != INVALID_HANDLE_VALUE)
        SetConsoleCursorInfo(g_hOut, &g_oldCursorInfo);
#else
    fputs("\x1b[?25h", stdout); fflush(stdout);
#endif
}
