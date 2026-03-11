#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(_WIN64)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <unistd.h>
#endif

#include "input.h"
#include "parse.h"
#include "exec.h"
#include "history.h"

static void start_in_home_dir(void) {
#if defined(_WIN32) || defined(_WIN64)
    const char *home = getenv("USERPROFILE");
    if (!home || !*home) home = getenv("HOME");
    if (home && *home) {
        SetCurrentDirectoryA(home);
    }
#else
    const char *home = getenv("HOME");
    if (home && *home) {
        chdir(home);
    }
#endif
}

int main(void) {
    char line[MAX_INPUT];
    char *segments[MAX_SEGMENTS];

    start_in_home_dir();

    if (input_init() != 0) {
        fprintf(stderr, "Failed to initialize input subsystem\n");
        return 1;
    }
    atexit(input_restore);

    history_load();

    for (;;) {
        if (input_readline(line, sizeof(line)) != 0) {
            break; /* EOF or Ctrl-C */
        }
        if (!line[0]) continue;
        if (strcmp(line, "exit") == 0) break;

        char line_copy[MAX_INPUT];
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';

        int segcount = split_pipeline_inplace(line, segments, MAX_SEGMENTS);
        if (segcount <= 0) continue;

        exec_pipeline(segments, segcount);
        history_add(line_copy);
    }

    history_save();
    return 0;
}
