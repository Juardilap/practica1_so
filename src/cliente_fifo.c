
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

#include "protocol.h"
#include "ipc_utils.h"

static int read_line(const char *prompt, char *buffer, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, (int)size, stdin) == NULL)
        return 0;

    char *newline = strchr(buffer, '\n');

    if (newline != NULL) {
        *newline = '\0';
        return 1;
    }

    /* La línea podría exceder el buffer: descartar lo restante. */
    int ch;

    while ((ch = getchar()) != '\n' && ch != EOF)
        ;

    fprintf(stderr, "Entrada demasiado larga.\n");
    return -1;
}

static int receive_responses(int fd_resp)
{
    for (;;) {
        Response resp = {0};

        ssize_t n = read_full(fd_resp, &resp, sizeof(resp));

        if (n == 0) {
            fprintf(stderr, "El servidor cerró la conexión.\n");
            return -1;
        }

        if (n == -1) {
            perror("read_full");
            return -1;
        }

        /* Validar que el texto tenga terminador nulo. */
        if (memchr(resp.text, '\0', sizeof(resp.text)) == NULL) {
            fprintf(stderr, "Respuesta con texto inválido.\n");
            return -1;
        }

        switch (resp.type) {
            case RESP_ROW:
                printf("%s\n", resp.text);
                break;

            case RESP_NOT_FOUND:
                printf("%s\n", resp.text);
                /* La búsqueda aún puede enviar RESP_END. */
                break;

            case RESP_END:
                printf("%s\n", resp.text);
                return 0;

            case RESP_ERROR:
                fprintf(stderr, "%s\n", resp.text);
                /* Puede ser un error sin RESP_END. */
                return 0;

            case RESP_BYE:
                printf("%s\n", resp.text);
                return 1;

            default:
                fprintf(stderr, "Tipo de respuesta desconocido: %d\n",
                        (int)resp.type);
                return -1;
        }
    }
}
static int esperar_entrada(int fd_resp)
{
    struct pollfd fds[2];

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = fd_resp;
    fds[1].events = POLLIN;

    for (;;) {
        int resultado = poll(fds, 2, -1);

        if (resultado == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("poll");
            return -1;
        }

        /* El servidor cerró su extremo de la FIFO. */
        if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            fprintf(stderr, "\nEl servidor se desconectó.\n");
            return -1;
        }

        /* La entrada del teclado está disponible. */
        if (fds[0].revents & POLLIN) {
            return 0;
        }

        if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            fprintf(stderr, "\nLa entrada estándar se cerró.\n");
            return -1;
        }
    }
}


int main(void)
{
    int fd_req = -1;
    int fd_resp = -1;
    int exit_status = EXIT_FAILURE;

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return EXIT_FAILURE;
    }

    printf("Cliente FIFO: conectando...\n");

    fd_req = open(FIFO_REQ, O_WRONLY);
    if (fd_req == -1) {
        perror("open FIFO_REQ");
        goto cleanup;
    }

    fd_resp = open(FIFO_RESP, O_RDONLY);
    if (fd_resp == -1) {
        perror("open FIFO_RESP");
        goto cleanup;
    }

    printf("Conectado al servidor.\n");

    for (;;) {
        char input[256];



        printf("\n========== WORLD CITIES ==========\n");
        printf("1. Buscar ciudad\n");
        printf("2. Buscar país\n");
        printf("3. Buscar código ISO2\n");
        printf("4. Salir\n");

        if (esperar_entrada(fd_resp) == -1) {
            break;
        }

        int result = read_line("Seleccione una opción: ",
                               input, sizeof(input));

        if (result == 0) {
            fprintf(stderr, "Fin de la entrada estándar.\n");
            break;
        }

        if (result == -1)
            continue;

        Request req = {0};

        if (strcmp(input, "1") == 0) {
            req.operation = OP_CITY;
        } else if (strcmp(input, "2") == 0) {
            req.operation = OP_COUNTRY;
        } else if (strcmp(input, "3") == 0) {
            req.operation = OP_ISO2;
        } else if (strcmp(input, "4") == 0) {
            req.operation = OP_EXIT;
        } else {
            printf("Opción inválida.\n");
            continue;
        }

        if (req.operation != OP_EXIT) {
            result = read_line("Ingrese el término de búsqueda: ",
                               input, sizeof(input));

            if (result == 0)
                break;

            if (result == -1)
                continue;

            if (input[0] == '\0') {
                printf("La búsqueda no puede estar vacía.\n");
                continue;
            }

            if (strlen(input) >= sizeof(req.value)) {
                fprintf(stderr, "El término es demasiado largo.\n");
                continue;
            }

            memcpy(req.value, input, strlen(input) + 1);
        }

        if (write_full(fd_req, &req, sizeof(req)) !=
            (ssize_t)sizeof(req)) {
            perror("write_full");
        break;
            }

            int response_status = receive_responses(fd_resp);

            if (response_status == -1)
                break;

        if (req.operation == OP_EXIT) {
            if (response_status == 1)
                exit_status = EXIT_SUCCESS;

            break;
        }

        if (response_status == 1) {
            fprintf(stderr, "El servidor terminó la sesión.\n");
            break;
        }
    }

    cleanup:
    if (fd_req != -1 && close(fd_req) == -1)
        perror("close FIFO_REQ");

    if (fd_resp != -1 && close(fd_resp) == -1)
        perror("close FIFO_RESP");

    printf("Cliente finalizado.\n");

    return exit_status;
}
