#include <string.h>
#include "parse.h"

/* split pipeline in-place, respect simple quotes */
int split_pipeline_inplace(char *line, char **segments, int maxseg) {
    int seg = 0;
    char *p = line;
    char *start = line;
    char quote = 0;
    for (;;) {
        char c = *p;
        if (!c || (!quote && c == '|')) {
            *p = '\0';
            /* trim start */
            char *s = start; while (*s == ' ' || *s == '\t') ++s;
            /* trim end */
            char *e = p - 1; while (e >= s && (*e == ' ' || *e == '\t')) *e-- = '\0';
            if (*s && seg < maxseg) segments[seg++] = s;
            if (!c) break;
            start = p + 1;
        } else if (!quote && (c == '\'' || c == '"')) {
            quote = c;
        } else if (quote && c == quote) {
            quote = 0;
        }
        ++p;
    }
    return seg;
}

/* parse args in-place, handle quotes */
int parse_args_inplace(char *s, char **argv, int maxargs) {
    int argc = 0;
    char *p = s;
    char quote = 0;
    char tokbuf[MAX_INPUT];
    int ti = 0;
    while (*p && argc < maxargs - 1) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            quote = *p++;
            char *start = p;
            while (*p && *p != quote) ++p;
            if (*p == quote) { *p = '\0'; ++p; }
            argv[argc++] = start;
        } else {
            char *start = p;
            while (*p && *p != ' ' && *p != '\t') ++p;
            if (*p) { *p = '\0'; ++p; }
            argv[argc++] = start;
        }
    }
    argv[argc] = NULL;
    return argc;
}
