#include "dataset.h"
#include "ipc_utils.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;
static void request_stop(int signo) { (void)signo; stop_requested = 1; }

typedef struct {
    const char *path;
    dev_t device;
    ino_t inode;
    int verified;
} OwnedFifo;

static int prepare_fifo(OwnedFifo *fifo) {
    if (mkfifo(fifo->path, 0660) < 0 && errno != EEXIST) return -1;
    struct stat st;
    if (lstat(fifo->path, &st) < 0) return -1;
    if (!S_ISFIFO(st.st_mode) || st.st_uid != geteuid()) { errno = EINVAL; return -1; }
    fifo->device = st.st_dev;
    fifo->inode = st.st_ino;
    fifo->verified = 1;
    return 0;
}

static int verify_fd(int fd, const OwnedFifo *fifo) {
    struct stat st;
    if (fstat(fd, &st) < 0) return -1;
    if (!S_ISFIFO(st.st_mode) || st.st_dev != fifo->device || st.st_ino != fifo->inode) {
        errno = ESTALE;
        return -1;
    }
    return 0;
}

static int remove_fifo(const OwnedFifo *fifo) {
    if (!fifo->verified) return 0;
    struct stat st;
    if (lstat(fifo->path, &st) < 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISFIFO(st.st_mode) || st.st_dev != fifo->device || st.st_ino != fifo->inode) {
        fprintf(stderr, "No se elimina recurso reemplazado: %s\n", fifo->path);
        errno = ESTALE;
        return -1;
    }
    return unlink(fifo->path);
}

static int emit_fifo(const Response *response, void *ctx) {
    int fd = *(int *)ctx;
    return write_full_cancelable(fd, response, sizeof(*response), &stop_requested) < 0 ? -1 : 0;
}

static int close_checked(int fd) {
    if (fd < 0) return 0;
    /* On Linux close releases fd even on EINTR; retry could close a reused fd. */
    if (close(fd) < 0) { perror("close"); return -1; }
    return 0;
}

int main(void) {
    int lock_fd = -1, req_fd = -1, resp_fd = -1, status = 1;
    OwnedFifo request = { .path = FIFO_REQ }, response = { .path = FIFO_RESP };
    struct sigaction action = {0};
    if (sigemptyset(&action.sa_mask) < 0) { perror("sigemptyset"); return 1; }
    action.sa_handler = request_stop;
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction"); return 1;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) < 0) { perror("SIGPIPE"); return 1; }

    /* Keep the lock inode after shutdown: unlinking it would permit two locks. */
    lock_fd = open(FIFO_LOCK, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0660);
    if (lock_fd < 0) { perror("open lock"); goto cleanup; }
    struct stat lock_stat;
    if (fstat(lock_fd, &lock_stat) < 0) { perror("fstat lock"); goto cleanup; }
    if (!S_ISREG(lock_stat.st_mode) || lock_stat.st_uid != geteuid()) {
        fprintf(stderr, "Archivo de bloqueo no valido.\n"); goto cleanup;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        perror("Sesion FIFO ocupada: flock"); goto cleanup;
    }
    if (stop_requested) { status = 0; goto cleanup; }
    if (prepare_fifo(&request) < 0 || prepare_fifo(&response) < 0) {
        perror("crear/verificar FIFO"); goto cleanup;
    }
    /* Same connection order as the client. Nonblocking opens + ppoll make
     * startup cancellable without a signal/check/open race. */
    req_fd = open(FIFO_REQ, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (req_fd < 0 || verify_fd(req_fd, &request) < 0) { perror("open FIFO_REQ"); goto cleanup; }
    for (;;) {
        if (stop_requested) { status = 0; goto cleanup; }
        resp_fd = open(FIFO_RESP, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (resp_fd >= 0) break;
        if (errno != ENXIO && errno != EINTR) { perror("open FIFO_RESP"); goto cleanup; }
        if (ipc_wait_cancelable(-1, 0, 50, &stop_requested) < 0) goto io_error;
    }
    if (verify_fd(resp_fd, &response) < 0) { perror("verificar FIFO_RESP"); goto cleanup; }

    while (!stop_requested) {
        Request req;
        ssize_t n = read_full_cancelable(req_fd, &req, sizeof(req), &stop_requested);
        if (n < 0) goto io_error;
        if (!n) { fprintf(stderr, "Cliente desconectado sin OP_EXIT.\n"); goto cleanup; }
        if (stop_requested) break;
        if (req.operation == OP_EXIT) {
            Response bye = { .type = RESP_BYE, .text = "Sesion finalizada." };
            if (emit_fifo(&bye, &resp_fd) < 0) goto io_error;
            status = 0;
            goto cleanup;
        }
        /* Invalid requests and dataset errors are terminal for this session. */
        if (search_dataset(DATASET_DEFAULT_PATH, &req, emit_fifo, &resp_fd) < 0)
            goto io_error;
    }
    status = 0;
    goto cleanup;
io_error:
    if (errno == ECANCELED || stop_requested) status = 0;
    else if (errno == EPIPE) fprintf(stderr, "Cliente desconectado al enviar respuesta.\n");
    else perror("Sesion FIFO");
cleanup:
    if (close_checked(resp_fd) < 0) status = 1;
    if (close_checked(req_fd) < 0) status = 1;
    if (remove_fifo(&response) < 0) { perror("limpiar FIFO_RESP"); status = 1; }
    if (remove_fifo(&request) < 0) { perror("limpiar FIFO_REQ"); status = 1; }
    if (close_checked(lock_fd) < 0) status = 1;
    return status;
}
