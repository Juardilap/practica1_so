#include "shm_utils.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/pidfd.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static key_t session_key(void) {
#ifdef SHM_TEST_BUILD
    const char *value = getenv("SO_TEST_SHM_KEY");
    if (value) {
        char *end;
        long key = strtol(value, &end, 0);
        if (*value && !*end && key > 0 && key <= 0x7fffffff) return (key_t)key;
    }
    errno = EINVAL;
    return (key_t)-1;
#else
    return SHM_KEY;
#endif
}

static long long now_ms(void) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) < 0) return -1;
    return (long long)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static int process_start(pid_t pid, unsigned long long *start) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char *line = NULL;
    size_t cap = 0;
    int result = -1, saved = EPROTO;
    if (getline(&line, &cap, file) >= 0) {
        char *end = strrchr(line, ')'); /* comm can itself contain spaces/parens. */
        if (end && end[1] == ' ') {
            char *save, *token = strtok_r(end + 2, " ", &save);
            for (int field = 3; token; ++field, token = strtok_r(NULL, " ", &save)) {
                if (field == 22) {
                    char *tail;
                    errno = 0;
                    *start = strtoull(token, &tail, 10);
                    if (!errno && tail != token && !*tail) result = 0;
                    break;
                }
            }
        }
    } else saved = errno ? errno : EIO;
    free(line);
    if (fclose(file) != 0) return -1;
    if (result < 0) errno = saved;
    return result;
}

static int peer_alive(int fd) {
    struct pollfd peer = { .fd = fd, .events = POLLIN };
    int result;
    do { result = poll(&peer, 1, 0); } while (result < 0 && errno == EINTR);
    if (result < 0) return -1;
    if (result) { errno = EPIPE; return -1; }
    return 0;
}

static int open_peer(pid_t pid, unsigned long long expected) {
    unsigned long long before, after;
    if (pid <= 0 || process_start(pid, &before) < 0) return -1;
    if (before != expected) { errno = ESTALE; return -1; }
    int fd = pidfd_open(pid, 0);
    if (fd < 0) return -1;
    if (process_start(pid, &after) < 0 || after != before || peer_alive(fd) < 0) {
        int reason = errno ? errno : ESTALE;
        if (close(fd) < 0) perror("close pidfd");
        errno = reason;
        return -1;
    }
    return fd;
}

static int client_lock(void) {
    int fd = open(SHM_CLIENT_LOCK, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0660);
    if (fd < 0) return -1;
    struct stat info;
    int reason = 0;
    if (fstat(fd, &info) < 0) reason = errno;
    else if (!S_ISREG(info.st_mode) || info.st_uid != geteuid()) reason = EINVAL;
    else if (flock(fd, LOCK_EX | LOCK_NB) < 0) reason = errno;
    if (reason) {
        if (close(fd) < 0) perror("close client lock");
        errno = reason;
        return -1;
    }
    return fd;
}

int shm_install_signals(void (*handler)(int)) {
    struct sigaction action = { .sa_handler = handler };
    if (sigemptyset(&action.sa_mask) < 0 || sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) return -1;
    action.sa_handler = SIG_IGN;
    return sigaction(SIGPIPE, &action, NULL);
}

int shm_server_open(ShmSession *session) {
    session->server = 1;
    key_t key = session_key();
    if (key == (key_t)-1) return -1;
    session->id = shmget(key, sizeof(SharedBlock), IPC_CREAT | IPC_EXCL | 0660);
    if (session->id < 0) return -1; /* Existing sessions are never reinitialized. */
    void *mapping = shmat(session->id, NULL, 0);
    if (mapping == (void *)-1) return -1;
    session->block = mapping;
    SharedBlock *block = session->block;
    if (!atomic_is_lock_free(&block->initialized) || !atomic_is_lock_free(&block->state)) {
        errno = ENOTSUP; return -1;
    }
    block->magic = SHM_MAGIC;
    block->version = SHM_VERSION;
    block->block_size = sizeof(*block);
    block->server_pid = getpid();
    if (process_start(getpid(), &block->server_start) < 0) return -1;
    sem_t *sems[] = { &block->req_ready, &block->resp_ready, &block->resp_consumed };
    for (int i = 0; i < 3; ++i) {
        if (sem_init(sems[i], 1, 0) < 0) return -1;
        ++session->sem_count;
        if (*session->stop) { errno = ECANCELED; return -1; }
    }
    atomic_store_explicit(&block->state, STATE_IDLE, memory_order_release);
    atomic_store_explicit(&block->initialized, 1, memory_order_release);
    printf("Servidor SHM listo. SHMID: %d\n", session->id);
    return fflush(stdout) == EOF ? -1 : 0;
}

int shm_client_open(ShmSession *session) {
    session->lock_fd = client_lock();
    if (session->lock_fd < 0) return -1;
    key_t key = session_key();
    if (key == (key_t)-1) return -1;
    session->id = shmget(key, 1, 0660);
    if (session->id < 0) return -1;
    struct shmid_ds info;
    if (shmctl(session->id, IPC_STAT, &info) < 0) return -1;
    if (info.shm_segsz != sizeof(SharedBlock) || info.shm_perm.uid != geteuid()) {
        errno = EPROTO; return -1;
    }
    unsigned long long creator_start;
    if (process_start(info.shm_cpid, &creator_start) < 0) return -1;
    session->peer_fd = open_peer(info.shm_cpid, creator_start);
    if (session->peer_fd < 0) return -1;
    void *mapping = shmat(session->id, NULL, 0);
    if (mapping == (void *)-1) return -1;
    session->block = mapping;
    SharedBlock *block = session->block;
    long long begin = now_ms();
    if (begin < 0) return -1;
    while (!atomic_load_explicit(&block->initialized, memory_order_acquire)) {
        if (*session->stop) { errno = ECANCELED; return -1; }
        if (atomic_load_explicit(&block->state, memory_order_acquire) == STATE_CLOSING) {
            errno = ECONNRESET; return -1;
        }
        if (peer_alive(session->peer_fd) < 0) return -1;
        long long now = now_ms();
        if (now < 0) return -1;
        if (now - begin >= 5000) { errno = ETIMEDOUT; return -1; }
        if (poll(NULL, 0, 50) < 0 && errno != EINTR) return -1;
    }
    if (block->magic != SHM_MAGIC || block->version != SHM_VERSION ||
        block->block_size != sizeof(*block) || block->server_pid != info.shm_cpid ||
        block->server_start != creator_start ||
        atomic_load_explicit(&block->client_registered, memory_order_acquire)) {
        errno = EPROTO; return -1;
    }
    if (shm_check(session) < 0) return -1;
    block->client_pid = getpid();
    if (process_start(getpid(), &block->client_start) < 0) return -1;
    atomic_store_explicit(&block->client_registered, 1, memory_order_release);
    return 0;
}

int shm_check(void *ctx) {
    ShmSession *session = ctx;
    if (*session->stop) { errno = ECANCELED; return -1; }
    if (atomic_load_explicit(&session->block->state, memory_order_acquire) == STATE_CLOSING) {
        errno = ECONNRESET; return -1;
    }
    if (session->server && session->peer_fd < 0 &&
        atomic_load_explicit(&session->block->client_registered, memory_order_acquire)) {
        session->peer_fd = open_peer(session->block->client_pid, session->block->client_start);
        if (session->peer_fd < 0) return -1;
    }
    if (session->peer_fd >= 0 && peer_alive(session->peer_fd) < 0) return -1;
    if (session->server && session->peer_fd >= 0) {
        struct shmid_ds info;
        if (shmctl(session->id, IPC_STAT, &info) < 0) return -1;
        if (info.shm_nattch < 2) { errno = EPIPE; return -1; }
    }
    return 0;
}

int shm_wait(ShmSession *session, sem_t *sem) {
    for (;;) {
        /* A final ACK can already be queued when its sender detaches/exits. */
        if (sem_trywait(sem) == 0) return 0;
        if (errno != EAGAIN && errno != EINTR) return -1;
        if (shm_check(session) < 0) return -1;
        struct timespec deadline;
        if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0) return -1;
        deadline.tv_nsec += 200000000;
        if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
        if (sem_clockwait(sem, CLOCK_MONOTONIC, &deadline) == 0) return 0;
        if (errno != EINTR && errno != ETIMEDOUT) return -1;
    }
}

int shm_emit(const Response *response, void *ctx) {
    ShmSession *session = ctx;
    if (shm_check(session) < 0) return -1;
    session->block->response = *response;
    atomic_store_explicit(&session->block->state, STATE_RESPONSE_READY, memory_order_release);
    if (sem_post(&session->block->resp_ready) < 0) return -1;
    return shm_wait(session, &session->block->resp_consumed);
}

int shm_session_close(ShmSession *session) {
    int result = 0;
    if (session->server && session->id >= 0) {
        if (session->block)
            atomic_store_explicit(&session->block->state, STATE_CLOSING, memory_order_release);
        if (shmctl(session->id, IPC_RMID, NULL) < 0) { perror("shmctl IPC_RMID"); result = -1; }
        if (session->block && session->sem_count) {
            int alone = 0;
            long long begin = now_ms();
            while (begin >= 0) {
                if (session->lock_fd < 0) session->lock_fd = client_lock();
                if (session->lock_fd < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                    perror("lock cleanup"); break;
                }
                struct shmid_ds info;
                if (shmctl(session->id, IPC_STAT, &info) < 0) { perror("shmctl IPC_STAT"); break; }
                if (session->lock_fd >= 0 && info.shm_nattch == 1) { alone = 1; break; }
                long long now = now_ms();
                if (now < 0 || now - begin >= 2000) break;
                if (poll(NULL, 0, 20) < 0 && errno != EINTR) break;
            }
            if (alone) {
                sem_t *sems[] = { &session->block->req_ready, &session->block->resp_ready,
                                 &session->block->resp_consumed };
                for (int i = session->sem_count - 1; i >= 0; --i)
                    if (sem_destroy(sems[i]) < 0) { perror("sem_destroy"); result = -1; }
            } else {
                fprintf(stderr, "Semaforos aun asociados: destruccion omitida; segmento marcado IPC_RMID.\n");
                result = -1;
            }
        }
    }
    if (session->block && shmdt(session->block) < 0) { perror("shmdt"); result = -1; }
    session->block = NULL;
    if (session->peer_fd >= 0 && close(session->peer_fd) < 0) { perror("close pidfd"); result = -1; }
    if (session->lock_fd >= 0 && close(session->lock_fd) < 0) { perror("close lock"); result = -1; }
    session->peer_fd = session->lock_fd = -1;
    return result;
}
