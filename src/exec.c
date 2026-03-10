/* exec.c - execution layer for Shivishell */

#include "exec.h"
#include "parse.h"
#include "input.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int join_pipeline(char **segments, int seg_count, char *out, size_t outlen) {
    size_t pos = 0;
    for (int i = 0; i < seg_count; ++i) {
        const char *seg = segments[i];
        if (!seg || !*seg) continue;
        size_t slen = strlen(seg);
        if (i > 0) {
            if (pos + 3 >= outlen) return -1;
            out[pos++] = ' ';
            out[pos++] = '|';
            out[pos++] = ' ';
        }
        if (pos + slen + 1 > outlen) return -1;
        memcpy(out + pos, seg, slen);
        pos += slen;
    }
    out[pos] = '\0';
    return 0;
}

static int handle_cd_builtin(const char *segment) {
    char buf[MAX_INPUT];
    strncpy(buf, segment, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *argv[MAX_ARGS];
    int argc = parse_args_inplace(buf, argv, MAX_ARGS);
    if (argc <= 0) return 0;

    if (strcmp(argv[0], "cd") != 0) return 0;

    const char *path = (argc >= 2) ? argv[1] : NULL;

#if defined(_WIN32) || defined(_WIN64)
    char target[MAX_PATH];
    if (!path || !*path) {
        DWORD n = GetEnvironmentVariableA("USERPROFILE", target, (DWORD)sizeof(target));
        if (n == 0 || n >= sizeof(target)) return 0;
        path = target;
    }
    if (!SetCurrentDirectoryA(path)) {
        fprintf(stderr, "cd: failed to change directory\n");
        return -1;
    }
#else
    if (!path || !*path) path = getenv("HOME");
    if (!path || chdir(path) != 0) {
        perror("cd");
        return -1;
    }
#endif
    return 1;
}

static int get_exe_dir(char *out, size_t outlen) {
#if defined(_WIN32) || defined(_WIN64)
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)outlen);
    if (n == 0 || n >= outlen) return -1;
    char *last = strrchr(out, '\\');
    if (!last) last = strrchr(out, '/');
    if (!last) return -1;
    *last = '\0';
#else
    ssize_t n = readlink("/proc/self/exe", out, outlen - 1);
    if (n > 0 && n < (ssize_t)outlen) {
        out[n] = '\0';
        char *last = strrchr(out, '/');
        if (!last) return -1;
        *last = '\0';
        return 0;
    }
    if (!getcwd(out, outlen)) return -1;
#endif
    return 0;
}

static int get_commands_dir(char *out, size_t outlen) {
    char exe_dir[PATH_MAX];
    if (get_exe_dir(exe_dir, sizeof(exe_dir)) != 0) return -1;
#if defined(_WIN32) || defined(_WIN64)
    if (snprintf(out, outlen, "%s\\commands", exe_dir) >= (int)outlen) return -1;
#else
    if (snprintf(out, outlen, "%s/commands", exe_dir) >= (int)outlen) return -1;
#endif
    return 0;
}

static int commands_dir_exists(const char *dir) {
#if defined(_WIN32) || defined(_WIN64)
    DWORD attr = GetFileAttributesA(dir);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(dir, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
#endif
}

static int command_name_invalid(const char *cmd) {
    if (!cmd || !*cmd) return 1;
#if defined(_WIN32) || defined(_WIN64)
    if (strpbrk(cmd, "\\/")) return 1;
    if (strchr(cmd, ':')) return 1;
#else
    if (strchr(cmd, '/')) return 1;
#endif
    return 0;
}

static int file_exists(const char *path) {
#if defined(_WIN32) || defined(_WIN64)
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    return access(path, X_OK) == 0;
#endif
}

static int command_exists_in_dir(const char *commands_dir, const char *cmd) {
    if (command_name_invalid(cmd)) return 0;

#if defined(_WIN32) || defined(_WIN64)
    const char *exts[] = { "", ".exe", ".bat", ".cmd", ".com" };
    char path[MAX_PATH];
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        if (snprintf(path, sizeof(path), "%s\\%s%s", commands_dir, cmd, exts[i]) >= (int)sizeof(path))
            continue;
        if (file_exists(path)) return 1;
    }
    return 0;
#else
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", commands_dir, cmd) >= (int)sizeof(path)) return 0;
    return file_exists(path);
#endif
}

static char *save_path_env(void) {
    const char *p = getenv("PATH");
    if (!p) return NULL;
#if defined(_WIN32) || defined(_WIN64)
    return _strdup(p);
#else
    return strdup(p);
#endif
}

static void set_path_env(const char *value) {
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("PATH", value ? value : "");
#else
    if (value) setenv("PATH", value, 1);
    else unsetenv("PATH");
#endif
}

static int run_command(const char *cmdline) {
#if defined(_WIN32) || defined(_WIN64)
    char cmdexe[MAX_PATH];
    UINT n = GetSystemDirectoryA(cmdexe, (UINT)sizeof(cmdexe));
    if (n == 0 || n >= sizeof(cmdexe)) {
        fprintf(stderr, "failed to locate cmd.exe\n");
        return -1;
    }

    char runbuf[4096];
    if (snprintf(runbuf, sizeof(runbuf), "\"%s\\cmd.exe\" /C %s", cmdexe, cmdline) >= (int)sizeof(runbuf)) {
        fprintf(stderr, "command too long\n");
        return -1;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    BOOL ok = CreateProcessA(
        NULL,
        runbuf,
        NULL, NULL,
        TRUE,
        CREATE_DEFAULT_ERROR_MODE,
        NULL,
        NULL,
        &si,
        &pi
    );
    if (!ok) {
        fprintf(stderr, "'%s' is not recognized as an internal or external command.\n", cmdline);
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exit_code;
#else
    int rc = system(cmdline);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return rc;
#endif
}

int exec_pipeline(char **segments, int seg_count) {
    if (!segments || seg_count <= 0) return 0;

    if (seg_count == 1 && segments[0]) {
        int cd_result = handle_cd_builtin(segments[0]);
        if (cd_result != 0) return (cd_result < 0) ? -1 : 0;
    }

    char commands_dir[PATH_MAX];
    commands_dir[0] = '\0';
    if (get_commands_dir(commands_dir, sizeof(commands_dir)) != 0 || !commands_dir_exists(commands_dir)) {
        if (commands_dir[0]) {
            fprintf(stderr, "commands directory not found: %s\n", commands_dir);
        } else {
            fprintf(stderr, "commands directory not found\n");
        }
        return -1;
    }

    for (int i = 0; i < seg_count; ++i) {
        if (!segments[i] || !*segments[i]) continue;
        char segbuf[MAX_INPUT];
        strncpy(segbuf, segments[i], sizeof(segbuf) - 1);
        segbuf[sizeof(segbuf) - 1] = '\0';

        char *argv[MAX_ARGS];
        int argc = parse_args_inplace(segbuf, argv, MAX_ARGS);
        if (argc <= 0) continue;

        if (!command_exists_in_dir(commands_dir, argv[0])) {
            fprintf(stderr, "command not available: %s\n", argv[0]);
            return -1;
        }
    }

    char cmdline[MAX_INPUT * 2];
    if (join_pipeline(segments, seg_count, cmdline, sizeof(cmdline)) != 0) {
        fprintf(stderr, "command too long\n");
        return -1;
    }

    char *old_path = save_path_env();
    set_path_env(commands_dir);

    input_restore();
    int rc = run_command(cmdline);
    input_init();

    if (old_path) {
        set_path_env(old_path);
        free(old_path);
    } else {
        set_path_env(NULL);
    }

    return rc;
}
