#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>
#include "lttit_config.h"


#define SHELL_PROMPT          "> "
#define SHELL_BACKSPACE_SEQ       "\b \b"
#define SHELL_BACKSPACE_SEQ_LEN   3


void shell_main(void);
void shell_on_message(const char *msg, int len);

int  shell_readline(char *buf, int max);
int  shell_parse(char *line, char **argv, int max);
void shell_exec(int argc, char **argv);

#endif
