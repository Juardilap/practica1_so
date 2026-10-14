#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

/* Complete transfers retry EINTR without discarding bytes already transferred.
 * Read: 0 at clean EOF, -1/EPROTO at partial EOF. Write: -1/EIO on no progress. */
ssize_t read_full(int fd, void *buffer, size_t count);
ssize_t write_full(int fd, const void *buffer, size_t count);

/* Single-threaded SIGINT/SIGTERM cancellation; fd MUST be O_NONBLOCK.
 * Handlers only set *stop. Cancellation: -1, errno=ECANCELED.
 * A cancelled/failed partial message must not be retried: close the session. */
ssize_t read_full_cancelable(int fd, void *buffer, size_t count,
                             const volatile sig_atomic_t *stop);
ssize_t write_full_cancelable(int fd, const void *buffer, size_t count,
                              const volatile sig_atomic_t *stop);
/* Atomic signal check + ppoll: 1 ready, 0 timeout, -1 error/cancelled.
 * fd=-1 supports a cancellable timed delay. timeout_ms=-1 waits indefinitely. */
int ipc_wait_cancelable(int fd, short events, int timeout_ms,
                        const volatile sig_atomic_t *stop);
#endif
