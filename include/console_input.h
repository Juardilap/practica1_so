#ifndef CONSOLE_INPUT_H
#define CONSOLE_INPUT_H
#include "protocol.h"
typedef struct { char data[4096]; size_t next, end; int eof; } ConsoleInput;
typedef int (*connection_check_fn)(void *ctx);
/* Both prompts retain unread bytes; EOF requests OP_EXIT. peer_fd signals death
 * or unexpected FIFO activity. check runs also during idle stdin waits. */
int console_request(ConsoleInput *input, int peer_fd, connection_check_fn check,
                    void *ctx, Request *request);
#endif
