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

typedef struct {
    char data[4096];
    size_t next, end;
    int eof;
} Input;

/* 1=line, 0=EOF, -1=discarded invalid/oversize line, -2=I/O error.
 * Both prompts share this buffer; no stdio input buffering is involved. */
static int read_line(Input *input, int response_fd, char *line, size_t size) {
    size_t used = 0;
    int invalid = 0;
    for (;;) {
        if (input->next == input->end) {
            if (input->eof) {
                line[used] = '\0';
                return invalid ? -1 : used ? 1 : 0;
            }
            struct pollfd fds[2] = {
                { .fd = STDIN_FILENO, .events = POLLIN },
                { .fd = response_fd, .events = POLLIN }
            };
            int ready;
            do { ready = poll(fds, 2, -1); } while (ready < 0 && errno == EINTR);
            if (ready < 0) return -2;
            if (fds[1].revents) { errno = EPIPE; return -2; }
            if (fds[0].revents & (POLLERR | POLLNVAL)) { errno = EIO; return -2; }
            ssize_t n;
            do { n = read(STDIN_FILENO, input->data, sizeof(input->data)); }
            while (n < 0 && errno == EINTR);
            if (n < 0) return -2;
            input->next = 0;
            input->end = (size_t)n;
            input->eof = (n == 0);
            continue;
        }
        char c = input->data[input->next++];
        if (c == '\n') {
            if (used && line[used - 1] == '\r') --used;
            line[used] = '\0';
            return invalid ? -1 : 1;
        }
        if (!c || used == size - 1) invalid = 1;
        if (!invalid) line[used++] = c;
    }
}

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
    Input input = {0};
    for (;;) {
        Request request = {0};
        char choice[32];
        printf("\n1. Buscar ciudad\n2. Buscar pais\n3. Buscar ISO2\n4. Salir\nOpcion: ");
        fflush(stdout);
        int result = read_line(&input, resp_fd, choice, sizeof(choice));
        if (result == -2) { perror("entrada/conexion"); break; }
        if (result == -1) { fprintf(stderr, "Entrada demasiado larga o invalida.\n"); continue; }
        if (!result) request.operation = OP_EXIT;
        else {
            char *end;
            errno = 0;
            long operation = strtol(choice, &end, 10);
            while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
            if (errno || end == choice || *end || operation < OP_CITY || operation > OP_EXIT) {
                fprintf(stderr, "Opcion invalida.\n"); continue;
            }
            request.operation = (int)operation;
        }
        if (request.operation != OP_EXIT) {
            printf("Valor de busqueda: ");
            fflush(stdout);
            result = read_line(&input, resp_fd, request.value, sizeof(request.value));
            if (result == -2) { perror("entrada/conexion"); break; }
            if (result == -1) { fprintf(stderr, "Entrada demasiado larga o invalida.\n"); continue; }
            if (!result) request.operation = OP_EXIT;
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
