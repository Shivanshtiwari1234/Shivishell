// main.c
// ShiviShell - single-file, all-in-one shell
// Features:
//  - Fast minimal-redraw input engine (ReadConsoleInput, diff redraw)
//  - History (in-memory + persisted to %USERPROFILE%\.shivishell_history)
//  - Simple TAB autocomplete (CWD files/dirs + PATH .exe single-match)
//  - Quoting-aware parsing for pipelines (basic single/double quotes)
//  - Built-ins: cd, pwd, cls, echo, dir, type, del, mkdir, rmdir, copy, move, exit
//  - PATH caching at startup for fast executable lookup
//  - Pipeline support (cmd1 | cmd2 | ...)
//  - Prompt format: (venv) [username] C:\current\path>
//  - MinGW/MSVC-friendly; no alloca usage
//
// Compile (MinGW):
//    gcc main.c -o shivi.exe
// Compile (MSVC):
//    cl main.c
//

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>

#define MAX_INPUT 2048
#define MAX_ARGS 128
#define MAX_SEGMENTS 64
#define HISTORY_SIZE 1024
#define PROMPT_COLOR 11
#define NORMAL_COLOR 7
#define HISTORY_FILENAME ".shivishell_history"

// ----------------------------- Utilities -----------------------------
static HANDLE g_hStdin = NULL;
static HANDLE g_hStdout = NULL;
static DWORD  g_oldMode = 0;

static void set_color(WORD color) { SetConsoleTextAttribute(g_hStdout, color); }

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}

static char *lowercase_dup(const char *s) {
    if (!s) return NULL;
    char *p = strdup_safe(s);
    if (!p) return NULL;
    for (char *c = p; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
    return p;
}

// ----------------------------- History -----------------------------
static char history[HISTORY_SIZE][MAX_INPUT];
static int hist_count = 0;

static char *get_history_path(void) {
    char *user = getenv("USERPROFILE");
    if (!user) user = getenv("HOME");
    if (!user) return strdup_safe(HISTORY_FILENAME);
    size_t n = strlen(user) + 1 + strlen(HISTORY_FILENAME) + 1;
    char *p = (char*)malloc(n);
    if (!p) return strdup_safe(HISTORY_FILENAME);
    snprintf(p, n, "%s\\%s", user, HISTORY_FILENAME);
    return p;
}

static void load_history(void) {
    char *path = get_history_path();
    FILE *f = fopen(path, "r");
    if (!f) { free(path); return; }
    char line[MAX_INPUT];
    hist_count = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;
        strncpy(history[hist_count++], line, MAX_INPUT - 1);
        if (hist_count >= HISTORY_SIZE) break;
    }
    fclose(f);
    free(path);
}

static void save_history(void) {
    char *path = get_history_path();
    FILE *f = fopen(path, "w");
    if (!f) { free(path); return; }
    for (int i = 0; i < hist_count; ++i) {
        fputs(history[i], f);
        fputc('\n', f);
    }
    fclose(f);
    free(path);
}

static void add_history(const char *line) {
    if (!line || line[0] == '\0') return;
    if (hist_count > 0 && strcmp(history[hist_count - 1], line) == 0) return;
    if (hist_count < HISTORY_SIZE) {
        strncpy(history[hist_count++], line, MAX_INPUT - 1);
    } else {
        // rotate
        memmove(history, history + 1, (HISTORY_SIZE - 1) * MAX_INPUT);
        strncpy(history[HISTORY_SIZE - 1], line, MAX_INPUT - 1);
    }
}

// ----------------------------- PATH cache -----------------------------
typedef struct PathEntry {
    char *name;        // lowercased without .exe
    char *fullpath;    // full path including .exe
    struct PathEntry *next;
} PathEntry;
static PathEntry *path_table = NULL;

static void free_path_table(void) {
    PathEntry *e = path_table;
    while (e) {
        PathEntry *n = e->next;
        free(e->name);
        free(e->fullpath);
        free(e);
        e = n;
    }
    path_table = NULL;
}

static void add_path_entry(const char *name_lower, const char *fullpath) {
    PathEntry *e = (PathEntry*)malloc(sizeof(PathEntry));
    e->name = strdup_safe(name_lower);
    e->fullpath = strdup_safe(fullpath);
    e->next = path_table;
    path_table = e;
}

static void build_path_cache(void) {
    free_path_table();
    char *pathenv = getenv("PATH");
    if (!pathenv) return;
    char *copy = _strdup(pathenv);
    if (!copy) return;
    char *token = strtok(copy, ";");
    while (token) {
        char pattern[MAX_INPUT];
        snprintf(pattern, sizeof(pattern), "%s\\*.exe", token);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                char full[MAX_INPUT];
                snprintf(full, sizeof(full), "%s\\%s", token, fd.cFileName);
                char name_noext[MAX_INPUT];
                strncpy(name_noext, fd.cFileName, sizeof(name_noext)-1);
                name_noext[sizeof(name_noext)-1] = '\0';
                char *dot = strrchr(name_noext, '.');
                if (dot) *dot = '\0';
                char *lower = lowercase_dup(name_noext);
                int seen = 0;
                for (PathEntry *p = path_table; p; p = p->next) {
                    if (strcmp(p->name, lower) == 0) { seen = 1; break; }
                }
                if (!seen) add_path_entry(lower, full);
                free(lower);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        token = strtok(NULL, ";");
    }
    free(copy);
}

static const char *lookup_path(const char *name) {
    if (!name) return NULL;
    if (strchr(name, '\\') || strchr(name, '/') || strchr(name, '.')) return NULL;
    char *lower = lowercase_dup(name);
    if (!lower) return NULL;
    for (PathEntry *p = path_table; p; p = p->next) {
        if (strcmp(p->name, lower) == 0) { free(lower); return p->fullpath; }
    }
    free(lower);
    return NULL;
}

// ----------------------------- Prompt builder -----------------------------
static void build_prompt(char *out, size_t out_size) {
    // cwd
    char cwd[MAX_PATH];
    if (!GetCurrentDirectoryA(MAX_PATH, cwd)) strcpy(cwd, "?");

    // username detection
    char username[512];
    DWORD unlen = sizeof(username);
    GetUserNameA(username, &unlen);

    char userprefix[MAX_PATH];
    snprintf(userprefix, sizeof(userprefix), "C:\\Users\\%s", username);

    char user_segment[320] = "";
    if (_strnicmp(cwd, userprefix, (int)strlen(userprefix)) == 0) {
        snprintf(user_segment, sizeof(user_segment), "[%s] ", username);
    }

    // venv detection
    char *venv = getenv("VIRTUAL_ENV");
    char venv_segment[320] = "";
    if (venv && *venv) {
        const char *p = strrchr(venv, '\\');
        const char *name = p ? p + 1 : venv;
        snprintf(venv_segment, sizeof(venv_segment), "(%s) ", name);
    }

    snprintf(out, out_size, "%s%s%s> ", venv_segment, user_segment, cwd);
}

// cached prompt length
static int prompt_len_cached = -1;
static void recompute_prompt_len(void) { prompt_len_cached = -1; }
static int get_prompt_len(void) {
    if (prompt_len_cached >= 0) return prompt_len_cached;
    char tmp[1024];
    build_prompt(tmp, sizeof(tmp));
    prompt_len_cached = (int)strlen(tmp);
    return prompt_len_cached;
}

static void print_prompt(void) {
    char p[1024];
    build_prompt(p, sizeof(p));
    set_color(PROMPT_COLOR);
    fputs(p, stdout);
    set_color(NORMAL_COLOR);
    fflush(stdout);
}

// ----------------------------- Fast redraw engine -----------------------------
static char last_line[MAX_INPUT];
static int last_len = 0;

static void fast_write(const char *s, DWORD n) {
    DWORD written = 0;
    WriteConsoleA(g_hStdout, s, n, &written, NULL);
}

static void redraw_diff(const char *newbuf, int newlen, int cursor_pos) {
    recompute_prompt_len();
    int p_len = get_prompt_len();

    int common = 0;
    int minlen = (last_len < newlen) ? last_len : newlen;
    while (common < minlen && last_line[common] == newbuf[common]) ++common;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hStdout, &csbi);
    int row = csbi.dwCursorPosition.Y;
    COORD pos = { (SHORT)(p_len + common), (SHORT)row };
    SetConsoleCursorPosition(g_hStdout, pos);

    if (newlen > common) fast_write(newbuf + common, (DWORD)(newlen - common));

    if (last_len > newlen) {
        int leftover = last_len - newlen;
        static char spacebuf[1024];
        if (leftover > (int)sizeof(spacebuf)) {
            memset(spacebuf, ' ', sizeof(spacebuf));
            int rem = leftover;
            while (rem > 0) {
                int w = (rem > (int)sizeof(spacebuf)) ? (int)sizeof(spacebuf) : rem;
                fast_write(spacebuf, (DWORD)w);
                rem -= w;
            }
        } else {
            memset(spacebuf, ' ', leftover);
            fast_write(spacebuf, (DWORD)leftover);
        }
    }

    int copylen = (newlen < MAX_INPUT - 1) ? newlen : MAX_INPUT - 1;
    memcpy(last_line, newbuf, copylen);
    last_line[copylen] = '\0';
    last_len = copylen;

    pos.X = (SHORT)(p_len + cursor_pos);
    SetConsoleCursorPosition(g_hStdout, pos);
    fflush(stdout);
}

// ----------------------------- Console raw mode -----------------------------
static void enable_raw_mode(void) {
    if (!g_hStdin) g_hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (!g_hStdout) g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(g_hStdin, &mode)) return;
    g_oldMode = mode;
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    mode |= (ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT);
    SetConsoleMode(g_hStdin, mode);
}

static void disable_raw_mode(void) {
    if (g_oldMode) SetConsoleMode(g_hStdin, g_oldMode);
}

// ----------------------------- Tab completion (Option A: simple) -----------------------------
static void tab_complete_buffer(char *buf, int *len, int *cursor) {
    int i = *cursor - 1;
    while (i >= 0 && buf[i] != ' ' && buf[i] != '\t') --i;
    int start = i + 1;
    int pref_len = *cursor - start;
    char prefix[MAX_INPUT];
    if (pref_len < 0) pref_len = 0;
    strncpy(prefix, buf + start, pref_len);
    prefix[pref_len] = '\0';

    char matches[512][MAX_INPUT];
    int mcount = 0;

    // CWD matches
    char cwd[MAX_PATH];
    if (_getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, ".");
    char pat[MAX_INPUT];
    snprintf(pat, sizeof(pat), "%s\\%s*", cwd, prefix);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            snprintf(matches[mcount++], MAX_INPUT, "%s", fd.cFileName);
            if (mcount >= 511) break;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    // PATH cache matches (show exe names)
    for (PathEntry *p = path_table; p && mcount < 511; p = p->next) {
        if (strncmp(p->name, prefix, strlen(prefix)) == 0) {
            snprintf(matches[mcount++], MAX_INPUT, "%s.exe", p->name);
        }
    }

    if (mcount == 0) return;
    if (mcount == 1) {
        const char *m = matches[0];
        int addlen = (int)strlen(m) - pref_len;
        if (addlen > 0) {
            if (*len + addlen >= MAX_INPUT - 1) addlen = (MAX_INPUT - 2) - *len;
            memmove(buf + *cursor + addlen, buf + *cursor, (*len) - *cursor + 1);
            memcpy(buf + start, m, strlen(m));
            *len += addlen;
            *cursor = start + (int)strlen(m);
        }
        char full[MAX_INPUT];
        snprintf(full, sizeof(full), "%s\\%s", cwd, m);
        DWORD attr = GetFileAttributesA(full);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            if (*cursor < MAX_INPUT - 2 && buf[*cursor - 1] != '\\') {
                memmove(buf + *cursor + 1, buf + *cursor, (*len) - *cursor + 1);
                buf[*cursor] = '\\';
                (*cursor)++; (*len)++;
            }
        }
    } else {
        putchar('\n');
        for (int k = 0; k < mcount; ++k) {
            printf("%s  ", matches[k]);
            if ((k % 4) == 3) putchar('\n');
        }
        putchar('\n');
    }
}

// ----------------------------- Fast input line -----------------------------
static int fast_input_line(char *outbuf, int maxlen) {
    char buf[MAX_INPUT];
    int len = 0, cursor = 0;
    buf[0] = '\0';
    last_len = 0; last_line[0] = '\0';
    int hist_index = -1;

    redraw_diff(buf, len, cursor);

    INPUT_RECORD rec;
    DWORD read = 0;
    while (1) {
        if (!ReadConsoleInputA(g_hStdin, &rec, 1, &read)) return -1;
        if (rec.EventType != KEY_EVENT) continue;
        KEY_EVENT_RECORD ke = rec.Event.KeyEvent;
        if (!ke.bKeyDown) continue;

        WORD vk = ke.wVirtualKeyCode;
        char ch = ke.uChar.AsciiChar;

        if (vk == VK_RETURN) {
            buf[len] = '\0';
            int cp = (len < maxlen - 1) ? len : maxlen - 1;
            memcpy(outbuf, buf, cp);
            outbuf[cp] = '\0';
            putchar('\n');
            return 0;
        }

        if (vk == VK_TAB) {
            tab_complete_buffer(buf, &len, &cursor);
            redraw_diff(buf, len, cursor);
            continue;
        }

        if (vk == VK_BACK) {
            if (cursor > 0) { memmove(buf + cursor - 1, buf + cursor, len - cursor + 1); cursor--; len--; }
            redraw_diff(buf, len, cursor);
            continue;
        }

        if (vk == VK_DELETE) {
            if (cursor < len) { memmove(buf + cursor, buf + cursor + 1, len - cursor); len--; }
            redraw_diff(buf, len, cursor);
            continue;
        }

        if (vk == VK_LEFT) { if (cursor > 0) cursor--; redraw_diff(buf, len, cursor); continue; }
        if (vk == VK_RIGHT) { if (cursor < len) cursor++; redraw_diff(buf, len, cursor); continue; }
        if (vk == VK_HOME) { cursor = 0; redraw_diff(buf, len, cursor); continue; }
        if (vk == VK_END)  { cursor = len; redraw_diff(buf, len, cursor); continue; }

        if (vk == VK_UP || vk == VK_DOWN) {
            if (hist_count == 0) { /* none */ }
            else {
                if (vk == VK_UP) {
                    if (hist_index == -1) hist_index = hist_count - 1;
                    else if (hist_index > 0) hist_index--;
                } else {
                    if (hist_index == -1) { /* none */ }
                    else { hist_index++; if (hist_index >= hist_count) hist_index = -1; }
                }
                if (hist_index == -1) { len = 0; cursor = 0; buf[0] = '\0'; }
                else { strncpy(buf, history[hist_index], MAX_INPUT - 1); buf[MAX_INPUT - 1] = '\0'; len = (int)strlen(buf); cursor = len; }
            }
            redraw_diff(buf, len, cursor);
            continue;
        }

        if (ch >= 32 && ch <= 126) {
            if (len + 1 < maxlen - 1) {
                memmove(buf + cursor + 1, buf + cursor, len - cursor + 1);
                buf[cursor] = ch;
                cursor++; len++;
            }
            redraw_diff(buf, len, cursor);
            continue;
        }

        // ignore other keys
    }
    return -1;
}

// ----------------------------- Parsing (quotes + pipelines) -----------------------------
static int split_pipeline(const char *line, char **segments, int maxseg) {
    int len = (int)strlen(line);
    int seg = 0;
    char tmp[MAX_INPUT];
    int ti = 0;
    char quote = 0;
    for (int i = 0; i <= len && seg < maxseg; ++i) {
        char c = line[i];
        if (quote) {
            if (c == quote) quote = 0;
            else tmp[ti++] = c;
        } else {
            if (c == '\'' || c == '"') { quote = c; }
            else if (c == '|' || c == '\0') {
                tmp[ti] = '\0';
                // trim
                int s = 0; while (tmp[s] && (tmp[s] == ' ' || tmp[s] == '\t')) s++;
                int e = (int)strlen(tmp) - 1; while (e >= 0 && (tmp[e] == ' ' || tmp[e] == '\t')) tmp[e--] = '\0';
                if (strlen(tmp + s) > 0) segments[seg++] = strdup_safe(tmp + s);
                ti = 0;
                if (c == '\0') break;
            } else tmp[ti++] = c;
        }
    }
    return seg;
}

// parse args with quotes; returns argc; caller must free argv entries
static int parse_args_quoted(const char *input, char **argv, int maxargs) {
    int len = (int)strlen(input);
    char tok[MAX_INPUT];
    int ti = 0;
    int argc = 0;
    char quote = 0;
    for (int i = 0; i <= len && argc < maxargs - 1; ++i) {
        char c = input[i];
        if (quote) {
            if (c == quote) quote = 0;
            else if (c == '\0') { /*unterminated*/ }
            else tok[ti++] = c;
        } else {
            if (c == '\'' || c == '"') quote = c;
            else if (c == ' ' || c == '\t' || c == '\0') {
                if (ti > 0) { tok[ti] = '\0'; argv[argc++] = strdup_safe(tok); ti = 0; }
            } else tok[ti++] = c;
        }
    }
    if (ti > 0 && argc < maxargs - 1) { tok[ti] = '\0'; argv[argc++] = strdup_safe(tok); }
    argv[argc] = NULL;
    return argc;
}

static void free_argv(char **argv, int argc) {
    for (int i = 0; i < argc; ++i) free(argv[i]);
}

// ----------------------------- Builtins & execution -----------------------------
static int builtin_exec(int argc, char **argv, HANDLE hIn, HANDLE hOut) {
    // returns: 0 handled, 2 -> exit requested, -1 not builtin
    if (argc == 0) return 0;
    const char *cmd = argv[0];

    // prepare FILE* for hOut/hIn if necessary
    FILE *outf = NULL, *inf = NULL;
    int used_out = 0, used_in = 0;
    if (hOut && hOut != GetStdHandle(STD_OUTPUT_HANDLE)) {
        HANDLE dupOut = NULL;
        if (DuplicateHandle(GetCurrentProcess(), hOut, GetCurrentProcess(), &dupOut, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            int fd = _open_osfhandle((intptr_t)dupOut, _O_TEXT);
            if (fd != -1) { outf = _fdopen(fd, "w"); setvbuf(outf, NULL, _IONBF, 0); used_out = 1; }
            else CloseHandle(dupOut);
        }
    } else outf = stdout;

    if (hIn && hIn != GetStdHandle(STD_INPUT_HANDLE)) {
        HANDLE dupIn = NULL;
        if (DuplicateHandle(GetCurrentProcess(), hIn, GetCurrentProcess(), &dupIn, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            int fd = _open_osfhandle((intptr_t)dupIn, _O_TEXT);
            if (fd != -1) { inf = _fdopen(fd, "r"); setvbuf(inf, NULL, _IONBF, 0); used_in = 1; }
            else CloseHandle(dupIn);
        }
    } else inf = stdin;

    int res = -1;

    if (_stricmp(cmd, "cd") == 0) {
        if (argc >= 2) { if (!SetCurrentDirectoryA(argv[1])) fprintf(outf, "cd: cannot access '%s'\n", argv[1]); }
        else fprintf(outf, "cd: missing operand\n");
        res = 0;
    } else if (_stricmp(cmd, "pwd") == 0) {
        char cwd[MAX_PATH]; if (_getcwd(cwd, sizeof(cwd))) fprintf(outf, "%s\n", cwd);
        res = 0;
    } else if (_stricmp(cmd, "cls") == 0) {
        if (outf == stdout) {
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            if (GetConsoleScreenBufferInfo(g_hStdout, &csbi)) {
                DWORD cells = csbi.dwSize.X * csbi.dwSize.Y; COORD home = {0,0}; DWORD written;
                FillConsoleOutputCharacterA(g_hStdout, ' ', cells, home, &written);
                FillConsoleOutputAttribute(g_hStdout, csbi.wAttributes, cells, home, &written);
                SetConsoleCursorPosition(g_hStdout, home);
            } else system("cls");
        }
        res = 0;
    } else if (_stricmp(cmd, "echo") == 0) {
        for (int i = 1; i < argc; ++i) { if (i > 1) fputc(' ', outf); fputs(argv[i], outf); }
        fputc('\n', outf);
        res = 0;
    } else if (_stricmp(cmd, "dir") == 0) {
        if (outf == stdout) system("dir");
        else {
            char cmdline[MAX_INPUT]; snprintf(cmdline, sizeof(cmdline), "cmd /c dir");
            STARTUPINFOA si; PROCESS_INFORMATION pi; ZeroMemory(&si,sizeof(si)); ZeroMemory(&pi,sizeof(pi));
            si.cb = sizeof(si); si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdOutput = hOut; si.hStdError = hOut; si.hStdInput = hIn ? hIn : GetStdHandle(STD_INPUT_HANDLE);
            if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) fprintf(outf, "dir: failed\n");
            else { WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
        }
        res = 0;
    } else if (_stricmp(cmd, "type") == 0) {
        if (argc < 2) fprintf(outf, "type: missing file\n");
        else {
            FILE *f = fopen(argv[1], "rb");
            if (!f) fprintf(outf, "type: cannot open '%s'\n", argv[1]);
            else { char rb[8192]; size_t r; while ((r = fread(rb,1,sizeof(rb),f))>0) fwrite(rb,1,r,outf); fclose(f); }
        }
        res = 0;
    } else if (_stricmp(cmd, "del") == 0) {
        if (argc < 2) fprintf(outf, "del: missing file\n"); else if (!DeleteFileA(argv[1])) fprintf(outf, "del: failed to delete '%s'\n", argv[1]);
        res = 0;
    } else if (_stricmp(cmd, "mkdir") == 0) {
        if (argc < 2) fprintf(outf, "mkdir: missing name\n"); else if (_mkdir(argv[1]) != 0) fprintf(outf, "mkdir: failed to create '%s'\n", argv[1]);
        res = 0;
    } else if (_stricmp(cmd, "rmdir") == 0) {
        if (argc < 2) fprintf(outf, "rmdir: missing name\n"); else if (_rmdir(argv[1]) != 0) fprintf(outf, "rmdir: failed to remove '%s'\n", argv[1]);
        res = 0;
    } else if (_stricmp(cmd, "copy") == 0) {
        if (argc < 3) fprintf(outf, "copy: missing args\n"); else if (!CopyFileA(argv[1], argv[2], FALSE)) fprintf(outf, "copy: failed\n");
        res = 0;
    } else if (_stricmp(cmd, "move") == 0) {
        if (argc < 3) fprintf(outf, "move: missing args\n"); else if (!MoveFileA(argv[1], argv[2])) fprintf(outf, "move: failed\n");
        res = 0;
    } else if (_stricmp(cmd, "exit") == 0) {
        res = 2; // signal exit
    } else res = -1; // not builtin

    if (used_out && outf) fclose(outf);
    if (used_in && inf) fclose(inf);
    return res;
}

static HANDLE duplicate_inheritable(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return NULL;
    HANDLE dup = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) return h;
    return dup;
}

static int execute_segment(const char *segment, HANDLE hIn, HANDLE hOut) {
    char *argv[MAX_ARGS];
    int argc = parse_args_quoted(segment, argv, MAX_ARGS);
    if (argc == 0) return 0;

    int bres = builtin_exec(argc, argv, hIn, hOut);
    if (bres == 2) { free_argv(argv, argc); return 2; }
    if (bres != -1) { free_argv(argv, argc); return 0; }

    const char *full = lookup_path(argv[0]);
    char cmdline[MAX_INPUT];
    if (full) {
        const char *rest = segment + (int)strlen(argv[0]);
        while (*rest == ' ') ++rest;
        if (*rest) snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", full, rest);
        else snprintf(cmdline, sizeof(cmdline), "\"%s\"", full);
    } else {
        strncpy(cmdline, segment, sizeof(cmdline) - 1); cmdline[sizeof(cmdline) - 1] = '\0';
    }

    STARTUPINFOA si; PROCESS_INFORMATION pi; ZeroMemory(&si,sizeof(si)); ZeroMemory(&pi,sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = hIn ? duplicate_inheritable(hIn) : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hOut ? duplicate_inheritable(hOut) : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = si.hStdOutput;

    BOOL created = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (si.hStdInput && si.hStdInput != GetStdHandle(STD_INPUT_HANDLE) && si.hStdInput != hIn) CloseHandle(si.hStdInput);
    if (si.hStdOutput && si.hStdOutput != GetStdHandle(STD_OUTPUT_HANDLE) && si.hStdOutput != hOut) CloseHandle(si.hStdOutput);

    if (!created) {
        char fallback[MAX_INPUT];
        snprintf(fallback, sizeof(fallback), "cmd /c %s", segment);
        ZeroMemory(&si,sizeof(si)); ZeroMemory(&pi,sizeof(pi));
        si.cb = sizeof(si); si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = hIn ? duplicate_inheritable(hIn) : GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = hOut ? duplicate_inheritable(hOut) : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = si.hStdOutput;
        created = CreateProcessA(NULL, fallback, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
        if (si.hStdInput && si.hStdInput != GetStdHandle(STD_INPUT_HANDLE) && si.hStdInput != hIn) CloseHandle(si.hStdInput);
        if (si.hStdOutput && si.hStdOutput != GetStdHandle(STD_OUTPUT_HANDLE) && si.hStdOutput != hOut) CloseHandle(si.hStdOutput);
        if (!created) { free_argv(argv, argc); return -1; }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    free_argv(argv, argc);
    return 0;
}

static int execute_pipeline(char **segments, int seg_count) {
    if (seg_count == 0) return 0;
    HANDLE prevRead = NULL;
    for (int i = 0; i < seg_count; ++i) {
        HANDLE readPipe = NULL, writePipe = NULL;
        if (i < seg_count - 1) {
            SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
            if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
                if (prevRead) CloseHandle(prevRead);
                return -1;
            }
        }
        HANDLE inHandle = prevRead ? prevRead : GetStdHandle(STD_INPUT_HANDLE);
        HANDLE outHandle = (i < seg_count - 1) ? writePipe : GetStdHandle(STD_OUTPUT_HANDLE);

        int r = execute_segment(segments[i], inHandle, outHandle);
        if (r == 2) { if (prevRead) CloseHandle(prevRead); if (readPipe) CloseHandle(readPipe); if (writePipe) CloseHandle(writePipe); return 2; }

        if (writePipe) CloseHandle(writePipe);
        if (prevRead) CloseHandle(prevRead);
        prevRead = readPipe;
    }
    if (prevRead) CloseHandle(prevRead);
    return 0;
}

// ----------------------------- REPL -----------------------------
int main(void) {
    g_hStdin = GetStdHandle(STD_INPUT_HANDLE);
    g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_hStdin == INVALID_HANDLE_VALUE || g_hStdout == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to get console handles\n");
        return 1;
    }

    build_path_cache();
    load_history();
    enable_raw_mode();

    char line[MAX_INPUT];

    while (1) {
        recompute_prompt_len();
        print_prompt();

        if (fast_input_line(line, MAX_INPUT) != 0) break;
        if (line[0] == '\0') continue;
        add_history(line);

        char *segments[MAX_SEGMENTS];
        int seg_count = split_pipeline(line, segments, MAX_SEGMENTS);
        if (seg_count <= 0) continue;

        int exec_res = execute_pipeline(segments, seg_count);
        for (int i = 0; i < seg_count; ++i) free(segments[i]);

        if (exec_res == 2) break;
        putchar('\n');
    }

    disable_raw_mode();
    save_history();
    free_path_table();
    return 0;
}
