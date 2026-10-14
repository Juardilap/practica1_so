#ifndef SHM_UTILS_H
#define SHM_UTILS_H
#include "protocol.h"
#include <signal.h>

typedef struct {
    SharedBlock *block;
    int id, peer_fd, lock_fd, server, sem_count;
    volatile sig_atomic_t *stop;
} ShmSession;
#define SHM_SESSION_INIT(flag) { .id=-1, .peer_fd=-1, .lock_fd=-1, .stop=(flag) }
/* Initialize once with SHM_SESSION_INIT. All functions return 0 or -1/errno.
 * Call shm_session_close even after a partially failed open. Only the creating
 * server removes its segment; a client never removes a segment on disconnect. */
int shm_install_signals(void (*handler)(int));
int shm_server_open(ShmSession *session);
int shm_client_open(ShmSession *session);
int shm_check(void *session);
/* Monotonic 200 ms waits check stop/peer life, without a query deadline.
 * Consumes queued terminal ACKs before checking whether the sender exited. */
int shm_wait(ShmSession *session, sem_t *sem);
int shm_emit(const Response *response, void *session);
int shm_session_close(ShmSession *session);
#endif
