#ifndef SHIVI_INPUT_H
#define SHIVI_INPUT_H

#include "config.h"

/* Initialize/restore terminal (raw mode). Return 0 on success. */
int input_init(void);
void input_restore(void);

/* Read a line into outbuf (maxlen). Returns 0 on success or Ctrl-C (empty). */
int input_readline(char *outbuf, int maxlen);

/* Optional helpers */
void input_hide_cursor(void);
void input_show_cursor(void);

#endif /* SHIVI_INPUT_H */
