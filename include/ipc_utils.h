#ifndef IPC_UTILS
#define IPC_UTILS

#include <stddef.h>
#include <sys/types.h>


ssize_t read_full(int fd, void *buffer , size_t count);
ssize_t write_full(int fd, const void * buffer, size_t count);

#endif
