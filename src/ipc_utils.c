#include "ipc_utils.h"

#include <errno.h>
#include <unistd.h>

// funciones que repiten read() o write() cuando la transferencia es parcial y reintentan cuando la llamada se interrumpe por EINTR.



ssize_t read_full(int fd, void *buffer, size_t count)
{
    size_t total = 0;
    char *ptr = buffer;

    while (total < count) {
        ssize_t n = read(fd, ptr + total, count - total);

        if (n > 0) {
            total += (size_t)n;
            continue;
        }

        if (n == 0) {
            if (total == 0)
                return 0;

            errno = EPROTO;
            return -1;
        }

        if (errno == EINTR)
            return -1;

        return -1;
    }

    return (ssize_t)total;
}



ssize_t write_full(int fd, const void *buffer, size_t count)
{
    size_t total = 0;
    const char *ptr = buffer;

    while (total < count) {
        ssize_t n = write(fd, ptr + total, count - total);

        if (n > 0) {
            total += (size_t)n;
            continue;
        }

        if (n == 0) {
            errno = EIO;
            return -1;
        }

        if (errno == EINTR)
            return -1;

        return -1;
    }

    return (ssize_t)total;
}
