#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

#define SHELL_MAX_LINE        64
#define SHELL_MAX_ARGS        16
#define SHELL_MAX_PATH        64
#define SHELL_LS_MAX_ENTRIES  8
#define SHELL_CAT_BUF_SIZE    256
#define SHELL_REMOTE_MAX_CMD  64

#define SHELL_PROMPT          "> "
#define SHELL_BACKSPACE_SEQ       "\b \b"
#define SHELL_BACKSPACE_SEQ_LEN   3

#define SHELL_ENABLE_FS   1
#define SHELL_ENABLE_VIM  1

void shell_main(void);
void shell_on_message(const char *msg, int len);

int  shell_readline(char *buf, int max);
int  shell_parse(char *line, char **argv, int max);
void shell_exec(int argc, char **argv);

#endif
