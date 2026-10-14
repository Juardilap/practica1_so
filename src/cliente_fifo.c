#include "console_input.h"
#include "ipc_utils.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int receive_responses(int fd, int exiting) {
    for (;;) {
        Response response;
        ssize_t n = read_full(fd, &response, sizeof(response));
        if (n <= 0) {
            if (!n) errno = EPIPE;
            perror("recibir respuesta"); return -1;
        }
        if (!memchr(response.text, '\0', sizeof(response.text))) {
            fprintf(stderr, "Respuesta sin terminador.\n"); return -1;
        }
        if (response.type == RESP_ERROR) {
            fprintf(stderr, "%s\n", response.text); return -1;
        }
        if (exiting) {
            if (response.type == RESP_BYE) { puts(response.text); return 0; }
        } else {
            if (response.type == RESP_ROW || response.type == RESP_NOT_FOUND) {
                puts(response.text); continue;
            }
            if (response.type == RESP_END) { puts(response.text); return 0; }
        }
        fprintf(stderr, "Tipo de respuesta inesperado: %d\n", response.type);
        return -1;
    }
}

int main(void) {
    struct sigaction action = { .sa_handler = SIG_IGN };
    if (sigemptyset(&action.sa_mask) < 0 || sigaction(SIGPIPE, &action, NULL) < 0) {
        perror("SIGPIPE"); return 1;
    }
    int status = 1;
    int req_fd = open(FIFO_REQ, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (req_fd < 0) { perror("open FIFO_REQ"); return 1; }
    int resp_fd = open(FIFO_RESP, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (resp_fd < 0) { perror("open FIFO_RESP"); goto cleanup; }
    ConsoleInput input = {0};
    for (;;) {
        Request request;
        if (console_request(&input, resp_fd, NULL, NULL, &request) < 0) {
            perror("entrada/conexion"); break;
        }
        if (write_full(req_fd, &request, sizeof(request)) < 0) { perror("enviar solicitud"); break; }
        if (receive_responses(resp_fd, request.operation == OP_EXIT) < 0) break;
        if (request.operation == OP_EXIT) { status = 0; break; }
    }
cleanup:
    if (resp_fd >= 0 && close(resp_fd) < 0) { perror("close FIFO_RESP"); status = 1; }
    if (close(req_fd) < 0) { perror("close FIFO_REQ"); status = 1; }
    return status;
}
