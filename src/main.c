#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "input.h"
#include "parse.h"
#include "exec.h"
#include "history.h"

int main(void) {
    char line[MAX_INPUT];
    char *segments[MAX_SEGMENTS];

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
