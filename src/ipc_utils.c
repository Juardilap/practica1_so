#include "ipc_utils.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <unistd.h>

int ipc_wait_cancelable(int fd, short events, int timeout_ms,
                        const volatile sig_atomic_t *stop) {
    sigset_t blocked, original;
    if (!stop || timeout_ms < -1) { errno = EINVAL; return -1; }
    if (sigemptyset(&blocked) < 0 || sigaddset(&blocked, SIGINT) < 0 ||
        sigaddset(&blocked, SIGTERM) < 0 ||
        sigprocmask(SIG_BLOCK, &blocked, &original) < 0) return -1;
    struct pollfd pfd = { .fd = fd, .events = events };
    struct timespec timeout = { .tv_sec = timeout_ms / 1000,
                               .tv_nsec = (timeout_ms % 1000) * 1000000L };
    int result;
    for (;;) {
        if (*stop) { errno = ECANCELED; result = -1; break; }
        result = ppoll(&pfd, 1, timeout_ms < 0 ? NULL : &timeout, &original);
        if (result < 0 && errno == EINTR) continue;
        if (result > 0 && (pfd.revents & POLLNVAL)) { errno = EBADF; result = -1; }
        break;
    }
    int saved = errno;
    if (sigprocmask(SIG_SETMASK, &original, NULL) < 0) return -1;
    if (*stop) { errno = ECANCELED; return -1; }
    errno = saved;
    return result;
}

static ssize_t transfer(int fd, void *buffer, size_t count, int writing,
                         const volatile sig_atomic_t *stop) {
    if (count > (size_t)SSIZE_MAX) { errno = EINVAL; return -1; }
    size_t done = 0;
    while (done < count) {
        if (stop && ipc_wait_cancelable(fd, writing ? POLLOUT : POLLIN, -1, stop) < 0)
            return -1;
        ssize_t n = writing ? write(fd, (char *)buffer + done, count - done) :
                              read(fd, (char *)buffer + done, count - done);
        if (n > 0) { done += (size_t)n; continue; }
        if (n == 0) {
            if (!writing && !done) return 0;
            errno = writing ? EIO : EPROTO;
            return -1;
        }
        if (errno == EINTR || (stop && (errno == EAGAIN || errno == EWOULDBLOCK)))
            continue;
        return -1;
    }
    if (stop && *stop) { errno = ECANCELED; return -1; }
    return (ssize_t)done;
}

ssize_t read_full(int fd, void *buffer, size_t count) {
    return transfer(fd, buffer, count, 0, NULL);
}
ssize_t write_full(int fd, const void *buffer, size_t count) {
    return transfer(fd, (void *)buffer, count, 1, NULL);
}
ssize_t read_full_cancelable(int fd, void *buffer, size_t count,
                             const volatile sig_atomic_t *stop) {
    return transfer(fd, buffer, count, 0, stop);
}
ssize_t write_full_cancelable(int fd, const void *buffer, size_t count,
                              const volatile sig_atomic_t *stop) {
    return transfer(fd, (void *)buffer, count, 1, stop);
}
