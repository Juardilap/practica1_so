
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>


#include "protocol.h"
#include "ipc_utils.h"
#include "dataset.h"


// Funcion para permitir la busqueda de data_search() sin la necesida de conocer los detalles de la fifo


static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}



static int cliente_desconectado = 0;

static int fifo_emit_response(const Response *resp, void *ctx)
{
    int fd = *(int *)ctx;

    if (cliente_desconectado) {
        return -1;
    }

    if (write_full(fd, (void *)resp, sizeof(*resp))
        != (ssize_t)sizeof(*resp)) {

        if (errno == EPIPE) {
            fprintf(stderr,
                    "El cliente se desconectó durante la respuesta.\n");
        } else {
            perror("Error enviando respuesta por FIFO");
        }

        cliente_desconectado = 1;
    return -1;
        }

        return 0;
}



static int create_fifo(const char *path, int *created)
{

    *created = 0;

    if (mkfifo(path, 0660) == 0){
        *created = 1;
        printf("FIFO creada %s\n", path);
        return 0;
    }

    if (errno != EEXIST) {
        perror("mkfifo");
        return -1;
    }

    /* Si ya existe, comprobar que sea una FIFO. */
    struct stat st;

    if (lstat(path, &st) == -1) {
        perror("lstat");
        return -1;
    }

    if (!S_ISFIFO(st.st_mode)) {
        fprintf(stderr, "%s existe, pero no es una FIFO\n", path);
        errno = EINVAL;
        return -1;
    }

    printf("Reutilizando FIFO existente: %s\n", path);
    return 0;
}


int main(void)
{
    int fd_req = -1;
    int fd_resp = -1;
    int exit_status = EXIT_FAILURE;

    int created_req = 0;
    int created_resp = 0;

    /* Evitar que una desconexión del cliente mate al servidor. */


    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    /* No usar SA_RESTART: queremos poder detectar interrupciones. */
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        return EXIT_FAILURE;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction SIGTERM");
        return EXIT_FAILURE;
    }

    /* SIGPIPE se ignora para detectar los errores de escritura. */
    struct sigaction pipe_action;
    memset(&pipe_action, 0, sizeof(pipe_action));

    pipe_action.sa_handler = SIG_IGN;
    sigemptyset(&pipe_action.sa_mask);

    if (sigaction(SIGPIPE, &pipe_action, NULL) == -1) {
        perror("sigaction SIGPIPE");
        return EXIT_FAILURE;
    }


    if (create_fifo(FIFO_REQ, &created_req) == -1) {
        goto cleanup;
    }

    if (create_fifo(FIFO_RESP, &created_resp) == -1) {
        goto cleanup;
    }

        printf("Servidor FIFO: esperando cliente...\n");
    fflush(stdout);

    fd_req = open(FIFO_REQ, O_RDONLY);

    if (fd_req == -1) {
        if (stop_requested || errno == EINTR) {
            fprintf(stderr,
                    "\nTerminación solicitada; limpiando recursos...\n");
            exit_status = EXIT_SUCCESS;
        } else {
            perror("open FIFO_REQ");
        }
        goto cleanup;
    }

    fd_resp = open(FIFO_RESP, O_WRONLY);

    if (fd_resp == -1) {
        if (stop_requested || errno == EINTR) {
            fprintf(stderr,
                    "\nTerminación solicitada; limpiando recursos...\n");
            exit_status = EXIT_SUCCESS;
        } else {
            perror("open FIFO_RESP");
        }
        goto cleanup;
    }

    printf("Cliente conectado.\n");


    for (;;) {
        Request req = {0};

        ssize_t n = read_full(fd_req, &req, sizeof(req));

        if (stop_requested) {
            fprintf(stderr, "\nTerminación solicitada; limpiando recursos...\n");
            exit_status = EXIT_SUCCESS;
            break;
        }

        if (n == 0) {
            fprintf(stderr, "El cliente cerró la conexión.\n");
            break;
        }

        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("read_full");
            break;
        }

        /* Validar que la petición contenga una cadena terminada en '\0'. */
        if (memchr(req.value, '\0', sizeof(req.value)) == NULL) {
            Response error;
            error.type = RESP_ERROR;
            snprintf(error.text, sizeof(error.text),
                     "La petición contiene un valor inválido.");

            if (write_full(fd_resp, &error, sizeof(error)) < 0) {
                perror("Error enviando respuesta");
                break;
            }

            continue;
        }

        /* El cliente solicita terminar la comunicación. */
        if (req.operation == OP_EXIT) {
            Response bye = {0};
            bye.type = RESP_BYE;

            snprintf(bye.text, sizeof(bye.text),
                     "Servidor finalizando.");

            if (write_full(fd_resp, &bye, sizeof(bye)) !=
                (ssize_t)sizeof(bye)) {
                perror("Error enviando RESP_BYE");
            break;
                }

                exit_status = EXIT_SUCCESS;
                break;
        }

        /* Validar el tipo de búsqueda. */
        if (req.operation != OP_CITY &&
            req.operation != OP_COUNTRY &&
            req.operation != OP_ISO2) {
            Response error;
        error.type = RESP_ERROR;
        snprintf(error.text, sizeof(error.text),
                 "Tipo de búsqueda desconocido.");

        write_full(fd_resp, &error, sizeof(error));
        continue;
            }

            cliente_desconectado = 0;
            /* Buscar en el CSV y transmitir cada coincidencia por el FIFO. */

            int resultados = search_dataset(
                DATASET_DEFAULT_PATH,
                &req,
                fifo_emit_response,
                &fd_resp
            );

            if (cliente_desconectado) {
                fprintf(stderr, "Se perdió la conexión con el cliente.\n");
                break;
            }

            if (resultados < 0) {
                fprintf(stderr, "La búsqueda terminó con un error.\n");
            }

        printf("Petición procesada: %s\n", req.value);
    }

    cleanup:
    if (fd_req != -1 && close(fd_req) == -1) {
        perror("close FIFO_REQ");
    }

    if (fd_resp != -1 && close(fd_resp) == -1) {
        perror("close FIFO_RESP");
    }

    /* Eliminar únicamente las FIFOs creadas por esta ejecución. */
    if (created_req) {
        if (unlink(FIFO_REQ) == -1 && errno != ENOENT) {
            perror("unlink FIFO_REQ");
            exit_status = EXIT_FAILURE;
        }
    }

    if (created_resp) {
        if (unlink(FIFO_RESP) == -1 && errno != ENOENT) {
            perror("unlink FIFO_RESP");
            exit_status = EXIT_FAILURE;
        }
    }

    printf("Servidor FIFO finalizado.\n");

    return exit_status;
}

