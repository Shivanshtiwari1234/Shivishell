#ifndef HISTORY_H
#define HISTORY_H

#include "config.h"

extern char history_buf[HISTORY_SIZE][MAX_INPUT];
extern int history_count;

void history_load(void);
void history_save(void);
void history_add(const char *line);

#endif
