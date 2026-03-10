#ifndef SHIVI_PARSE_H
#define SHIVI_PARSE_H

#include "config.h"

/* In-place split pipeline (replaces '|' with '\0'), returns count and fills segments[] */
int split_pipeline_inplace(char *line, char **segments, int maxseg);

/* Parse args in-place from a command segment, fills argv (caller frees nothing because in-place) */
int parse_args_inplace(char *s, char **argv, int maxargs);

#endif /* SHIVI_PARSE_H */
