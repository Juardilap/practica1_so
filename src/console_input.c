#include "console_input.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* 1=line, 0=EOF, -1=discarded invalid/oversize line, -2=I/O error. */
static int read_line(ConsoleInput *input, int peer_fd, connection_check_fn check,
                     void *ctx, char *line, size_t size) {
    size_t used = 0;
    int invalid = 0;
    for (;;) {
        if (check && check(ctx) < 0) return -2;
        if (input->next == input->end) {
            if (input->eof) {
                line[used] = '\0';
                return invalid ? -1 : used ? 1 : 0;
            }
            struct pollfd fds[2] = {
                { .fd = STDIN_FILENO, .events = POLLIN },
                { .fd = peer_fd, .events = POLLIN }
            };
            int ready = poll(fds, 2, check ? 200 : -1);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) return -2;
            if (!ready) continue;
            if (fds[1].revents) { errno = EPIPE; return -2; }
            if (fds[0].revents & (POLLERR | POLLNVAL)) { errno = EIO; return -2; }
            ssize_t n = read(STDIN_FILENO, input->data, sizeof(input->data));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) return -2;
            input->next = 0;
            input->end = (size_t)n;
            input->eof = n == 0;
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

int console_request(ConsoleInput *input, int peer_fd, connection_check_fn check,
                    void *ctx, Request *request) {
    for (;;) {
        *request = (Request){0};
        char choice[32];
        printf("\n1. Buscar ciudad\n2. Buscar pais\n3. Buscar ISO2\n4. Salir\nOpcion: ");
        if (fflush(stdout) == EOF) return -1;
        int result = read_line(input, peer_fd, check, ctx, choice, sizeof(choice));
        if (result == -2) return -1;
        if (result == -1) { fprintf(stderr, "Entrada demasiado larga o invalida.\n"); continue; }
        if (!result) request->operation = OP_EXIT;
        else {
            char *end;
            errno = 0;
            long operation = strtol(choice, &end, 10);
            while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
            if (errno || end == choice || *end || operation < OP_CITY || operation > OP_EXIT) {
                fprintf(stderr, "Opcion invalida.\n"); continue;
            }
            request->operation = (int)operation;
        }
        if (request->operation != OP_EXIT) {
            printf("Valor de busqueda: ");
            if (fflush(stdout) == EOF) return -1;
            result = read_line(input, peer_fd, check, ctx, request->value, sizeof(request->value));
            if (result == -2) return -1;
            if (result == -1) { fprintf(stderr, "Entrada demasiado larga o invalida.\n"); continue; }
            if (!result) request->operation = OP_EXIT;
        }
        return 0;
    }
}
