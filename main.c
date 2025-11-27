// shivishell_modern.c
// Modern single-file shivishell (Windows) - compact, corrected version

#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#ifndef HPCON
typedef void* HPCON;
#endif

// ---------------------------- Config ----------------------------
#define MAX_INPUT 4096
#define MAX_ARGS 256
#define MAX_SEGMENTS 128
#define HISTORY_SIZE 2048
#define PATH_HASH_SIZE 8192  /* power of two recommended */
#define ARENA_SIZE (64*1024)

// ---------------------------- Utilities ----------------------------
static void *xmalloc(size_t n) { void *p = malloc(n); if (!p) { fprintf(stderr, "Out of memory\n"); ExitProcess(1); } return p; }
static char *xstrdup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = xmalloc(n); memcpy(p, s, n); return p; }
static void xfree(void *p) { if (p) free(p); }

typedef struct { char *buf; size_t used; size_t cap; } Arena;
static Arena *arena_create(size_t cap) { Arena *a = xmalloc(sizeof(Arena)); a->buf = xmalloc(cap); a->used = 0; a->cap = cap; return a; }
static void arena_reset(Arena *a) { if (!a) return; a->used = 0; }
static void arena_free(Arena *a) { if (!a) return; free(a->buf); free(a); }
static void *arena_alloc(Arena *a, size_t n) { if (a->used + n > a->cap) { size_t need = a->used + n; size_t grow = a->cap * 2; while (grow < need) grow *= 2; a->buf = realloc(a->buf, grow); a->cap = grow; } void *p = a->buf + a->used; a->used += n; return p; }
static char *arena_strdup(Arena *a, const char *s) { size_t n = strlen(s) + 1; char *p = (char*)arena_alloc(a, n); memcpy(p, s, n); return p; }
static void str_to_lower_inplace(char *s) { for (; *s; ++s) if (*s >= 'A' && *s <= 'Z') *s += 'a' - 'A'; }

// ---------------------------- KV map (simple chained hash) ----------------------------
typedef struct KV { char *k; char *v; struct KV *next; } KV;
static KV *vars = NULL;
static KV *aliases = NULL;

static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char*)s; *p; ++p) { h ^= *p; h *= 16777619u; }
    return h;
}
static char *kv_get(KV **table, size_t size, const char *k) {
    if (!k) return NULL;
    uint32_t h = fnv1a(k);
    KV *e = table[h & (size-1)];
    while (e) { if (strcmp(e->k, k) == 0) return e->v; e = e->next; }
    return NULL;
}
static void kv_set(KV **table, size_t size, const char *k, const char *v) {
    if (!k) return;
    uint32_t h = fnv1a(k);
    KV **bp = &table[h & (size-1)];
    for (KV *e = *bp; e; e = e->next) if (strcmp(e->k, k) == 0) { free(e->v); e->v = xstrdup(v ? v : ""); return; }
    KV *n = xmalloc(sizeof(KV)); n->k = xstrdup(k); n->v = xstrdup(v ? v : ""); n->next = *bp; *bp = n;
}
static void kv_unset(KV **table, size_t size, const char *k) {
    if (!k) return;
    uint32_t h = fnv1a(k);
    KV **bp = &table[h & (size-1)];
    KV *prev = NULL;
    for (KV *e = *bp; e; prev = e, e = e->next) {
        if (strcmp(e->k, k) == 0) {
            if (prev) prev->next = e->next; else *bp = e->next;
            free(e->k); free(e->v); free(e);
            return;
        }
    }
}

// ---------------------------- PATH cache ----------------------------
typedef struct PathEntry { char *name; char *full; struct PathEntry *next; } PathEntry;
static PathEntry *path_table[PATH_HASH_SIZE];

static inline uint32_t path_hash_fn(const char *s) { return fnv1a(s) & (PATH_HASH_SIZE - 1); }

static void free_path_table(void) {
    for (size_t i = 0; i < PATH_HASH_SIZE; ++i) {
        PathEntry *e = path_table[i];
        while (e) { PathEntry *n = e->next; free(e->name); free(e->full); free(e); e = n; }
        path_table[i] = NULL;
    }
}

static void build_path_cache(void) {
    free_path_table();
    char *pathenv = getenv("PATH");
    if (!pathenv) return;
    char *copy = _strdup(pathenv);
    if (!copy) return;
    char *token = strtok(copy, ";");
    while (token) {
        char pattern[MAX_PATH];
        snprintf(pattern, sizeof(pattern), "%s\\*.exe", token);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                char full[MAX_PATH];
                snprintf(full, sizeof(full), "%s\\%s", token, fd.cFileName);
                char name_noext[MAX_PATH];
                strncpy(name_noext, fd.cFileName, sizeof(name_noext)-1); name_noext[sizeof(name_noext)-1] = '\0';
                char *dot = strrchr(name_noext, '.'); if (dot) *dot = '\0';
                char lower[MAX_PATH]; strncpy(lower, name_noext, sizeof(lower)); str_to_lower_inplace(lower);
                uint32_t idx = path_hash_fn(lower);
                int seen = 0;
                for (PathEntry *p = path_table[idx]; p; p = p->next) if (strcmp(p->name, lower) == 0) { seen = 1; break; }
                if (!seen) {
                    PathEntry *pe = xmalloc(sizeof(PathEntry));
                    pe->name = xstrdup(lower);
                    pe->full = xstrdup(full);
                    pe->next = path_table[idx];
                    path_table[idx] = pe;
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        token = strtok(NULL, ";");
    }
    free(copy);
}

static const char *lookup_path(const char *name) {
    if (!name) return NULL;
    for (const char *p = name; *p; ++p) if (*p == '\\' || *p == '/' || *p == '.') return NULL;
    char lower[MAX_PATH]; strncpy(lower, name, sizeof(lower)-1); lower[sizeof(lower)-1] = '\0'; str_to_lower_inplace(lower);
    uint32_t idx = path_hash_fn(lower);
    for (PathEntry *e = path_table[idx]; e; e = e->next) if (strcmp(e->name, lower) == 0) return e->full;
    return NULL;
}

// ---------------------------- Terminal I/O ----------------------------
static HANDLE g_hStdin = NULL, g_hStdout = NULL;
static DWORD g_oldMode = 0;

static void term_init(void) {
    g_hStdin = GetStdHandle(STD_INPUT_HANDLE);
    g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_hStdin == INVALID_HANDLE_VALUE || g_hStdout == INVALID_HANDLE_VALUE) { fprintf(stderr, "Console handles failed\n"); ExitProcess(1); }
    DWORD mode = 0; if (GetConsoleMode(g_hStdout, &mode)) SetConsoleMode(g_hStdout, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    DWORD inmode; if (GetConsoleMode(g_hStdin, &inmode)) { g_oldMode = inmode; DWORD newmode = inmode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT); newmode |= (ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT); SetConsoleMode(g_hStdin, newmode); }
}
static void term_restore(void) { if (g_oldMode) SetConsoleMode(g_hStdin, g_oldMode); }

static void write_utf8_to_console(const char *utf8) {
    if (!utf8) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) return;
    wchar_t *w = (wchar_t*)alloca((wlen + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen + 1);
    DWORD written = 0;
    WriteConsoleW(g_hStdout, w, wlen - 1, &written, NULL);
}

// ---------------------------- History ----------------------------
static char history[HISTORY_SIZE][MAX_INPUT];
static int hist_head = 0, hist_count = 0;

static char *get_history_path(void) {
    char *user = getenv("USERPROFILE");
    if (!user) user = getenv("HOME");
    if (!user) return xstrdup(".shivishell_history");
    size_t n = strlen(user) + 1 + strlen(".shivishell_history") + 1;
    char *p = xmalloc(n);
    snprintf(p, n, "%s\\%s", user, ".shivishell_history");
    return p;
}
static void load_history(void) {
    char *path = get_history_path();
    FILE *f = fopen(path, "r");
    if (!f) { free(path); return; }
    char line[MAX_INPUT];
    hist_head = 0; hist_count = 0;
    while (fgets(line, sizeof(line), f) && hist_count < HISTORY_SIZE) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) { strncpy(history[hist_count++], line, MAX_INPUT - 1); history[hist_count-1][MAX_INPUT-1] = '\0'; }
    }
    fclose(f); free(path);
}
static void save_history(void) {
    char *path = get_history_path();
    FILE *f = fopen(path, "w");
    if (!f) { free(path); return; }
    for (int i = 0; i < hist_count; ++i) {
        int idx = (hist_head + i) % HISTORY_SIZE;
        fputs(history[idx], f);
        fputc('\n', f);
    }
    fclose(f); free(path);
}
static void add_history(const char *line) {
    if (!line || !line[0]) return;
    if (hist_count > 0) {
        int last_idx = (hist_head + hist_count - 1) % HISTORY_SIZE;
        if (strcmp(history[last_idx], line) == 0) return;
    }
    if (hist_count < HISTORY_SIZE) {
        strncpy(history[(hist_head + hist_count) % HISTORY_SIZE], line, MAX_INPUT - 1);
        history[(hist_head + hist_count) % HISTORY_SIZE][MAX_INPUT-1] = '\0';
        hist_count++;
    } else {
        strncpy(history[hist_head], line, MAX_INPUT - 1);
        history[hist_head][MAX_INPUT-1] = '\0';
        hist_head = (hist_head + 1) % HISTORY_SIZE;
    }
}

// ---------------------------- Prompt ----------------------------
static char promptbuf[1024];
static void build_prompt(void) {
    char cwd[MAX_PATH];
    if (!_getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");
    char *ps1 = kv_get(&vars, PATH_HASH_SIZE, "PS1");
    if (!ps1) ps1 = "ShiviShell> ";
    snprintf(promptbuf, sizeof(promptbuf), "%s", ps1);
}
static void print_prompt(void) { build_prompt(); write_utf8_to_console(promptbuf); }

// ---------------------------- Parsing helpers ----------------------------

typedef struct { char *argv[MAX_ARGS]; int argc; char *infile; char *outfile; int append; int background; } CmdLine;

static void parse_segment(Arena *a, const char *seg, CmdLine *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    char temp[MAX_INPUT];
    strncpy(temp, seg, sizeof(temp)-1); temp[sizeof(temp)-1] = '\0';

    int n = (int)strlen(temp);
    int i = 0, wi = 0;
    char out[MAX_INPUT]; out[0] = '\0';

    while (i < n) {
        if (temp[i] == '\'' || temp[i] == '"') {
            char q = temp[i++];
            while (i < n && temp[i] != q) out[wi++] = temp[i++];
            if (i < n && temp[i] == q) i++;
        } else if (temp[i] == '>') {
            int append = 0;
            if (i+1 < n && temp[i+1] == '>') { append = 1; i++; }
            i++;
            while (i < n && (temp[i] == ' ' || temp[i] == '\t')) i++;
            int s = i;
            while (i < n && temp[i] != ' ' && temp[i] != '\t') i++;
            int L = i - s;
            if (L > 0) { char tok[MAX_INPUT]; strncpy(tok, temp + s, L); tok[L] = '\0'; cmd->outfile = arena_strdup(a, tok); cmd->append = append; }
        } else if (temp[i] == '<') {
            i++;
            while (i < n && (temp[i] == ' ' || temp[i] == '\t')) i++;
            int s = i;
            while (i < n && temp[i] != ' ' && temp[i] != '\t') i++;
            int L = i - s;
            if (L > 0) { char tok[MAX_INPUT]; strncpy(tok, temp + s, L); tok[L] = '\0'; cmd->infile = arena_strdup(a, tok); }
        } else if (temp[i] == '&' && i == n - 1) {
            cmd->background = 1; i++;
        } else {
            out[wi++] = temp[i++];
        }
    }
    out[wi] = '\0';

    // split args preserving quoted tokens
    int len = (int)strlen(out);
    int ti = 0; char tok[MAX_INPUT]; int argc = 0; char quote = 0;
    for (int j = 0; j <= len; ++j) {
        char c = out[j];
        if (quote) {
            if (c == quote) quote = 0;
            else if (c != '\0') tok[ti++] = c;
        } else {
            if (c == '\'' || c == '"') quote = c;
            else if (c == ' ' || c == '\t' || c == '\0') {
                if (ti > 0) { tok[ti] = '\0'; cmd->argv[argc++] = arena_strdup(a, tok); ti = 0; }
            } else tok[ti++] = c;
        }
    }
    cmd->argc = argc; cmd->argv[argc] = NULL;
}

// ---------------------------- Substitution & expansion ----------------------------

static void run_substitution(Arena *a, const char *cmd, char *out, size_t outcap) {
    // run via cmd.exe /C
    char shellcmd[MAX_INPUT];
    snprintf(shellcmd, sizeof(shellcmd), "cmd /C %s", cmd);
    FILE *f = _popen(shellcmd, "r");
    if (!f) { out[0] = '\0'; return; }
    size_t off = 0;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        size_t L = strlen(buf);
        if (off + L >= outcap - 1) break;
        memcpy(out + off, buf, L); off += L;
    }
    out[off] = '\0';
    _pclose(f);
    while (off > 0 && (out[off-1] == '\n' || out[off-1] == '\r')) out[--off] = '\0';
}

static void expand_vars_and_subs(Arena *a, const char *input, char *out, size_t outcap) {
    size_t oi = 0;
    for (size_t i = 0; input[i] && oi + 1 < outcap; ++i) {
        char c = input[i];
        if (c == '\'') {
            // single-quoted literal
            out[oi++] = c;
            i++;
            while (input[i] && input[i] != '\'') { if (oi + 1 < outcap) out[oi++] = input[i++]; else break; }
            if (input[i] == '\'') { if (oi + 1 < outcap) out[oi++] = input[i]; }
        } else if (c == '"') {
            // double quotes: allow expansions
            out[oi++] = c; i++;
            while (input[i] && input[i] != '"' && oi + 1 < outcap) {
                if (input[i] == '$' && input[i+1] == '(') {
                    int j = (int)i + 2, depth = 1;
                    while (input[j] && depth > 0) { if (input[j] == '(') depth++; else if (input[j] == ')') depth--; j++; }
                    int len = j - ((int)i + 2) - 1;
                    char subcmd[1024] = {0}, subout[2048] = {0};
                    if (len > 0) { strncpy(subcmd, input + i + 2, len); run_substitution(a, subcmd, subout, sizeof(subout)); size_t L = strlen(subout); if (oi + L < outcap) { memcpy(out + oi, subout, L); oi += L; } }
                    i = j - 1;
                    continue;
                } else if (input[i] == '$') {
                    int j = (int)i + 1;
                    if (input[j] == '{') {
                        j++;
                        int k = j;
                        while (input[k] && input[k] != '}') k++;
                        int L = k - j;
                        if (L > 0 && L < 128) {
                            char name[128]; strncpy(name, input + j, L); name[L] = '\0';
                            char *val = kv_get(&vars, PATH_HASH_SIZE, name); if (!val) val = getenv(name);
                            if (val) { size_t L2 = strlen(val); if (oi + L2 < outcap) { memcpy(out + oi, val, L2); oi += L2; } }
                        }
                        i = k;
                        if (input[i] == '}') continue;
                    } else {
                        int k = j;
                        while (input[k] && ((input[k] >= 'A' && input[k] <= 'Z') || (input[k] >= 'a' && input[k] <= 'z') || (input[k] >= '0' && input[k] <= '9') || input[k] == '_')) k++;
                        int L = k - j;
                        if (L > 0 && L < 128) {
                            char name[128]; strncpy(name, input + j, L); name[L] = '\0';
                            char *val = kv_get(&vars, PATH_HASH_SIZE, name); if (!val) val = getenv(name);
                            if (val) { size_t L2 = strlen(val); if (oi + L2 < outcap) { memcpy(out + oi, val, L2); oi += L2; } }
                        }
                        i = k - 1;
                        continue;
                    }
                }
                out[oi++] = input[i++];
            }
            if (input[i] == '"') out[oi++] = input[i];
        } else if (c == '$' && input[i+1] == '(') {
            // command substitution outside quotes
            int j = (int)i + 2, depth = 1;
            while (input[j] && depth > 0) { if (input[j] == '(') depth++; else if (input[j] == ')') depth--; j++; }
            int len = j - ((int)i + 2) - 1;
            char subcmd[1024] = {0}, subout[2048] = {0};
            if (len > 0) { strncpy(subcmd, input + i + 2, len); run_substitution(a, subcmd, subout, sizeof(subout)); size_t L = strlen(subout); if (oi + L < outcap) { memcpy(out + oi, subout, L); oi += L; } }
            i = j - 1;
        } else if (c == '$') {
            int j = (int)i + 1;
            if (input[j] == '{') {
                j++;
                int k = j;
                while (input[k] && input[k] != '}') k++;
                int L = k - j;
                if (L > 0 && L < 128) {
                    char name[128]; strncpy(name, input + j, L); name[L] = '\0';
                    char *val = kv_get(&vars, PATH_HASH_SIZE, name); if (!val) val = getenv(name);
                    if (val) { size_t L2 = strlen(val); if (oi + L2 < outcap) { memcpy(out + oi, val, L2); oi += L2; } }
                }
                i = k;
                if (input[i] == '}') continue;
            } else {
                int k = j;
                while (input[k] && ((input[k] >= 'A' && input[k] <= 'Z') || (input[k] >= 'a' && input[k] <= 'z') || (input[k] >= '0' && input[k] <= '9') || input[k] == '_')) k++;
                int L = k - j;
                if (L > 0 && L < 128) {
                    char name[128]; strncpy(name, input + j, L); name[L] = '\0';
                    char *val = kv_get(&vars, PATH_HASH_SIZE, name); if (!val) val = getenv(name);
                    if (val) { size_t L2 = strlen(val); if (oi + L2 < outcap) { memcpy(out + oi, val, L2); oi += L2; } }
                }
                i = k - 1;
                continue;
            }
        } else {
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
}

// ---------------------------- Execution primitives ----------------------------

static int spawn_process(char *cmdline, HANDLE hin, HANDLE hout, int background, PROCESS_INFORMATION *outpi) {
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = hin ? hin : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hout ? hout : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = si.hStdOutput;
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (!ok) return -1;
    if (outpi) *outpi = pi;
    else { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return 0;
}

static int run_command_segment(CmdLine *cmd, Arena *a); /* forward */

static int builtin_execute(CmdLine *cmd) {
    return run_command_segment(cmd, NULL);
}

static int run_command_segment(CmdLine *cmd, Arena *a) {
    if (cmd->argc == 0) return 0;

    // Builtins
    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (cmd->argc >= 2) { if (_chdir(cmd->argv[1]) != 0) fprintf(stderr, "cd: failed\n"); }
        else fprintf(stderr, "cd: missing operand\n");
        return 0;
    }
    if (strcmp(cmd->argv[0], "pwd") == 0) { char cwd[MAX_PATH]; if (_getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd); return 0; }
    if (strcmp(cmd->argv[0], "exit") == 0) return 2;
    if (strcmp(cmd->argv[0], "export") == 0) {
        if (cmd->argc >= 2) {
            char *eq = strchr(cmd->argv[1], '=');
            if (eq) { *eq = '\0'; kv_set(&vars, PATH_HASH_SIZE, cmd->argv[1], eq + 1); }
            else kv_set(&vars, PATH_HASH_SIZE, cmd->argv[1], "");
        } else fprintf(stderr, "export: missing\n");
        return 0;
    }
    if (strcmp(cmd->argv[0], "unset") == 0) { if (cmd->argc >= 2) kv_unset(&vars, PATH_HASH_SIZE, cmd->argv[1]); return 0; }
    if (strcmp(cmd->argv[0], "alias") == 0) {
        if (cmd->argc == 1) {
            // print aliases
            for (KV *e = aliases; e; e = e->next) printf("alias %s='%s'\n", e->k, e->v);
            return 0;
        }
        if (cmd->argc >= 2) {
            char *eq = strchr(cmd->argv[1], '=');
            if (eq) {
                *eq = '\0';
                char *val = eq + 1;
                if (val[0] == '\'' || val[0] == '"') {
                    size_t L = strlen(val);
                    if (L > 1 && val[L - 1] == val[0]) { val[L - 1] = '\0'; val++; }
                }
                kv_set(&aliases, PATH_HASH_SIZE, cmd->argv[1], val);
            }
            return 0;
        }
        return 0;
    }
    if (strcmp(cmd->argv[0], "unalias") == 0) { if (cmd->argc >= 2) kv_unset(&aliases, PATH_HASH_SIZE, cmd->argv[1]); return 0; }
    if (strcmp(cmd->argv[0], "history") == 0) {
        for (int i = 0; i < hist_count; ++i) {
            int idx = (hist_head + i) % HISTORY_SIZE;
            printf("%4d  %s\n", i + 1, history[idx]);
        }
        return 0;
    }
    if (strcmp(cmd->argv[0], "clear") == 0) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
            DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
            COORD home = {0, 0}; DWORD written;
            FillConsoleOutputCharacterA(GetStdHandle(STD_OUTPUT_HANDLE), ' ', cells, home, &written);
            FillConsoleOutputAttribute(GetStdHandle(STD_OUTPUT_HANDLE), csbi.wAttributes, cells, home, &written);
            SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), home);
        } else system("cls");
        return 0;
    }

    // External commands: expand alias or path
    char *alias_val = kv_get(&aliases, PATH_HASH_SIZE, cmd->argv[0]);
    char cmdline[MAX_INPUT];
    if (alias_val) {
        snprintf(cmdline, sizeof(cmdline), "%s", alias_val);
        for (int i = 1; i < cmd->argc; ++i) { strncat(cmdline, " ", sizeof(cmdline) - strlen(cmdline) - 1); strncat(cmdline, cmd->argv[i], sizeof(cmdline) - strlen(cmdline) - 1); }
    } else {
        const char *full = lookup_path(cmd->argv[0]);
        if (full) {
            snprintf(cmdline, sizeof(cmdline), "\"%s\"", full);
            for (int i = 1; i < cmd->argc; ++i) { strncat(cmdline, " ", sizeof(cmdline) - strlen(cmdline) - 1); strncat(cmdline, cmd->argv[i], sizeof(cmdline) - strlen(cmdline) - 1); }
        } else {
            strncpy(cmdline, cmd->argv[0], sizeof(cmdline) - 1); cmdline[sizeof(cmdline) - 1] = '\0';
            for (int i = 1; i < cmd->argc; ++i) { strncat(cmdline, " ", sizeof(cmdline) - strlen(cmdline) - 1); strncat(cmdline, cmd->argv[i], sizeof(cmdline) - strlen(cmdline) - 1); }
        }
    }

    HANDLE hin = NULL, hout = NULL;
    if (cmd->infile) {
        hin = CreateFileA(cmd->infile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hin == INVALID_HANDLE_VALUE) { fprintf(stderr, "open %s failed\n", cmd->infile); hin = NULL; }
    }
    if (cmd->outfile) {
        DWORD disp = cmd->append ? OPEN_ALWAYS : CREATE_ALWAYS;
        hout = CreateFileA(cmd->outfile, GENERIC_WRITE, 0, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hout == INVALID_HANDLE_VALUE) { fprintf(stderr, "open %s failed\n", cmd->outfile); hout = NULL; }
        else if (cmd->append) SetFilePointer(hout, 0, NULL, FILE_END);
    }

    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    int rc = spawn_process(cmdline, hin, hout, cmd->background, &pi);
    if (hin) CloseHandle(hin);
    if (hout) CloseHandle(hout);
    if (rc != 0) { fprintf(stderr, "failed to start: %s\n", cmdline); return -1; }

    if (cmd->background) {
        // background: just report pid and continue
        printf("[bg] pid %u started\n", (unsigned)pi.dwProcessId);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess); // not tracked in this simple implementation
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitcode = 0; GetExitCodeProcess(pi.hProcess, &exitcode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return 0;
}

// ---------------------------- Pipeline execution ----------------------------

static int execute_pipeline(char **segments, int seg_count, Arena *a) {
    if (seg_count == 0) return 0;
    HANDLE prevRead = NULL;
    for (int i = 0; i < seg_count; ++i) {
        CmdLine cmd;
        parse_segment(a, segments[i], &cmd);

        HANDLE readPipe = NULL, writePipe = NULL;
        if (i < seg_count - 1) {
            SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
            if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) { if (prevRead) CloseHandle(prevRead); return -1; }
        }

        // If builtin and not piping to another command, run inline
        if (cmd.argc > 0 && (strcmp(cmd.argv[0], "cd") == 0 || strcmp(cmd.argv[0], "pwd") == 0 || strcmp(cmd.argv[0], "exit") == 0 || strcmp(cmd.argv[0], "export") == 0 || strcmp(cmd.argv[0], "unset") == 0 || strcmp(cmd.argv[0], "alias") == 0 || strcmp(cmd.argv[0], "unalias") == 0 || strcmp(cmd.argv[0], "history") == 0 || strcmp(cmd.argv[0], "clear") == 0)) {
            int r = builtin_execute(&cmd);
            if (writePipe) CloseHandle(writePipe);
            if (prevRead) CloseHandle(prevRead);
            prevRead = readPipe;
            if (r == 2) return 2;
            continue;
        }

        int r = run_command_segment(&cmd, a);
        if (readPipe) CloseHandle(readPipe);
        if (writePipe) CloseHandle(writePipe);
        if (prevRead) CloseHandle(prevRead);
        prevRead = readPipe;
        if (r == 2) return 2;
    }
    if (prevRead) CloseHandle(prevRead);
    return 0;
}

// ---------------------------- REPL ----------------------------

static void repl_loop(void) {
    Arena *a = arena_create(ARENA_SIZE);
    char line[MAX_INPUT];

    while (1) {
        print_prompt();
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;
        add_history(line);

        char expanded[MAX_INPUT * 2];
        expand_vars_and_subs(a, line, expanded, sizeof(expanded));

        char *segments[MAX_SEGMENTS];
        int seg_count = 0;
        int si = 0; char segtmp[MAX_INPUT]; char quote = 0;
        for (int p = 0;; ++p) {
            char c = expanded[p];
            if (c == '\0' || (c == '|' && quote == 0)) {
                segtmp[si] = '\0';
                int st = 0; while (segtmp[st] && (segtmp[st] == ' ' || segtmp[st] == '\t')) st++;
                int e = (int)strlen(segtmp) - 1; while (e >= 0 && (segtmp[e] == ' ' || segtmp[e] == '\t')) segtmp[e--] = '\0';
                if (strlen(segtmp + st) > 0) segments[seg_count++] = arena_strdup(a, segtmp + st);
                si = 0;
                if (c == '\0') break;
                continue;
            }
            if (c == '\'' || c == '"') { if (quote == 0) quote = c; else if (quote == c) quote = 0; segtmp[si++] = c; }
            else segtmp[si++] = c;
        }

        int res = execute_pipeline(segments, seg_count, a);
        if (res == 2) break;
        arena_reset(a);
    }

    arena_free(a);
}

// ---------------------------- Init & main ----------------------------
int main(int argc, char **argv) {
    // set stdin/stdout to binary mode for predictable behaviour
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);

    term_init();
    build_path_cache();
    load_history();
    kv_set(&vars, PATH_HASH_SIZE, "PS1", "ShiviShell> ");

    // read rcfile (~/.shivishellrc)
    char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (home) {
        char rc[4096];
        snprintf(rc, sizeof(rc), "%s\\.shivishellrc", home);
        FILE *f = fopen(rc, "r");
        if (f) {
            char line[1024];
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = '\0';
                if (line[0] == '#' || line[0] == '\0') continue;
                if (strstr(line, "=") && strstr(line, "alias ") == NULL) {
                    char *eq = strchr(line, '=');
                    if (eq) { *eq = '\0'; kv_set(&vars, PATH_HASH_SIZE, line, eq + 1); }
                } else if (strncmp(line, "alias ", 6) == 0) {
                    char *a = line + 6;
                    char *eq = strchr(a, '=');
                    if (eq) { *eq = '\0'; char *val = eq + 1; if ((val[0] == '\'' || val[0] == '"') && val[strlen(val)-1] == val[0]) { val[strlen(val)-1] = '\0'; val++; } kv_set(&aliases, PATH_HASH_SIZE, a, val); }
                }
            }
            fclose(f);
        }
    }

    repl_loop();

    save_history();
    free_path_table();
    term_restore();
    return 0;
}
