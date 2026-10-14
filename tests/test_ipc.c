#include "ipc_utils.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t interrupts, stop;
static int inject_pending;
int __real_ppoll(struct pollfd *fds, nfds_t count, const struct timespec *timeout,
                 const sigset_t *mask);
int __wrap_ppoll(struct pollfd *fds, nfds_t count, const struct timespec *timeout,
                 const sigset_t *mask) {
    if (inject_pending) {
        inject_pending = 0;
        assert(raise(SIGTERM) == 0);
        assert(!stop); /* Blocked by ipc_wait_cancelable, not delivered yet. */
    }
    return __real_ppoll(fds, count, timeout, mask);
}
static void count_signal(int signo) { (void)signo; ++interrupts; }
static void cancel(int signo) { (void)signo; stop = 1; }
static void child_ok(pid_t pid) {
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL);
    assert(flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

int main(void) {
    alarm(15); /* A regression cannot hang the suite. */
    struct sigaction sa = { .sa_handler = count_signal };
    assert(sigemptyset(&sa.sa_mask) == 0 && sigaction(SIGUSR1, &sa, NULL) == 0);
    sa.sa_handler = cancel;
    assert(sigaction(SIGTERM, &sa, NULL) == 0 && sigaction(SIGINT, &sa, NULL) == 0);
    assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
    int p[2];
    char small[8] = {0};
    assert(pipe(p) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(p[0]);
        assert(write(p[1], "abc", 3) == 3);
        usleep(30000);
        assert(kill(getppid(), SIGUSR1) == 0);
        usleep(30000);
        assert(write(p[1], "defgh", 5) == 5);
        close(p[1]); _exit(0);
    }
    close(p[1]);
    assert(read_full(p[0], small, sizeof(small)) == 8);
    assert(!memcmp(small, "abcdefgh", 8) && interrupts > 0);
    assert(read_full(p[0], small, sizeof(small)) == 0);
    close(p[0]); child_ok(child);

    assert(pipe(p) == 0);
    assert(write(p[1], "abc", 3) == 3 && close(p[1]) == 0);
    assert(read_full(p[0], small, sizeof(small)) == -1 && errno == EPROTO);
    close(p[0]);

    size_t size = 262144;
    char *data = malloc(size);
    assert(data);
    for (size_t i = 0; i < size; ++i) data[i] = (char)(i % 127);
    assert(pipe(p) == 0);
    interrupts = 0;
    child = fork();
    assert(child >= 0);
    if (!child) {
        close(p[1]);
        usleep(30000);
        assert(kill(getppid(), SIGUSR1) == 0);
        usleep(30000);
        assert(kill(getppid(), SIGUSR1) == 0);
        usleep(30000);
        char chunk[257];
        size_t received = 0;
        ssize_t n;
        while ((n = read(p[0], chunk, sizeof(chunk))) > 0) {
            for (ssize_t i = 0; i < n; ++i)
                assert(chunk[i] == (char)((received + (size_t)i) % 127));
            received += (size_t)n;
        }
        assert(n == 0 && received == size);
        close(p[0]); _exit(0);
    }
    close(p[0]);
    assert(write_full(p[1], data, size) == (ssize_t)size && interrupts > 0);
    close(p[1]); child_ok(child);
    free(data);

    assert(pipe(p) == 0);
    close(p[0]);
    assert(write_full(p[1], "a", 1) == -1 && errno == EPIPE);
    close(p[1]);

    /* Both cancellation signals interrupt an empty read and a full write. */
    int signals[] = { SIGINT, SIGTERM };
    for (int s = 0; s < 2; ++s) {
        for (int writing = 0; writing < 2; ++writing) {
            assert(pipe(p) == 0);
            nonblock(p[0]); nonblock(p[1]);
            if (writing) {
                char fill[4096] = {0};
                while (write(p[1], fill, sizeof(fill)) > 0) {}
                assert(errno == EAGAIN);
            } else {
                /* Must retain progress while waiting for the rest of a frame. */
                assert(write(p[1], "abc", 3) == 3);
            }
            stop = 0;
            child = fork();
            assert(child >= 0);
            if (!child) {
                usleep(30000);
                assert(kill(getppid(), signals[s]) == 0);
                _exit(0);
            }
            ssize_t result = writing ? write_full_cancelable(p[1], "z", 1, &stop) :
                                      read_full_cancelable(p[0], small, sizeof(small), &stop);
            assert(result == -1 && errno == ECANCELED);
            close(p[0]); close(p[1]); child_ok(child);
        }
    }
    /* Already requested and pending-between-mask-and-wait cancellation. */
    stop = 1;
    assert(ipc_wait_cancelable(-1, 0, -1, &stop) == -1 && errno == ECANCELED);
    stop = 0;
    inject_pending = 1;
    assert(ipc_wait_cancelable(-1, 0, -1, &stop) == -1 && errno == ECANCELED);
    sigset_t mask;
    assert(sigprocmask(SIG_SETMASK, NULL, &mask) == 0);
    assert(sigismember(&mask, SIGTERM) == 0 && sigismember(&mask, SIGINT) == 0);
    stop = 0;
    assert(ipc_wait_cancelable(-1, 0, 1, &stop) == 0);
    assert(pipe(p) == 0);
    nonblock(p[0]); nonblock(p[1]);
    assert(write_full_cancelable(p[1], "xyz", 3, &stop) == 3);
    assert(read_full_cancelable(p[0], small, 3, &stop) == 3 && !memcmp(small, "xyz", 3));
    close(p[0]); close(p[1]);
    alarm(0);
    puts("PASS IPC: fragmentacion, EOF parcial, EINTR con progreso, EPIPE y cancelacion.");
    return 0;
}
