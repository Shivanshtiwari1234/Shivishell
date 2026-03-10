#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "history.h"

char history_buf[HISTORY_SIZE][MAX_INPUT];
int history_count = 0;

static const char *get_history_path(void) {
    static char path[512];
#if defined(_WIN32) || defined(_WIN64)
    char *u = getenv("USERPROFILE");
    if (!u) u = getenv("HOME");
    if (!u) { strcpy(path, HISTORY_FILE); return path; }
    snprintf(path, sizeof(path), "%s\\%s", u, HISTORY_FILE);
#else
    const char *home = getenv("HOME");
    if (!home) { strcpy(path, HISTORY_FILE); return path; }
    snprintf(path, sizeof(path), "%s/%s", home, HISTORY_FILE);
#endif
    return path;
}

void history_load(void) {
    const char *p = get_history_path();
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[MAX_INPUT];
    history_count = 0;
    while (fgets(line, sizeof(line), f) && history_count < HISTORY_SIZE) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) strncpy(history_buf[history_count++], line, MAX_INPUT-1);
    }
    fclose(f);
}

void history_save(void) {
    const char *p = get_history_path();
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int i = 0; i < history_count; ++i) {
        fputs(history_buf[i], f);
        fputc('\n', f);
    }
    fclose(f);
}

void history_add(const char *line) {
    if (!line || !line[0]) return;
    if (history_count > 0 && strcmp(history_buf[history_count-1], line) == 0) return;
    if (history_count < HISTORY_SIZE) {
        strncpy(history_buf[history_count++], line, MAX_INPUT-1);
    } else {
        memmove(history_buf, history_buf+1, (HISTORY_SIZE-1) * MAX_INPUT);
        strncpy(history_buf[HISTORY_SIZE-1], line, MAX_INPUT-1);
    }
}
