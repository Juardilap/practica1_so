//
// Created by Jheyrer on 13/10/2026.
//


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>

#include "protocol.h"

static int wait_sem(sem_t *sem)
{
    int result;

    do {
        result = sem_wait(sem);
    } while (result == -1 && errno == EINTR);

    return result;
}

static void mostrar_menu(void)
{
    printf("\n=== CLIENTE MEMORIA COMPARTIDA (SHM) ===\n");
    printf("1. Buscar por Ciudad ASCII (ej. Bogota)\n");
    printf("2. Buscar por País (ej. Colombia)\n");
    printf("3. Buscar por Código ISO2 (ej. CO)\n");
    printf("4. Salir\n");
    printf("Seleccione una opción: ");
}

static int leer_texto(const char *mensaje, char *buffer, size_t size)
{
    printf("%s", mensaje);
    fflush(stdout);

    if (fgets(buffer, size, stdin) == NULL) {
        return -1;
    }

    size_t len = strlen(buffer);

    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[--len] = '\0';
    } else {
        int ch;

        while ((ch = getchar()) != '\n' && ch != EOF) {
            /* Descartar el resto de una línea demasiado larga. */
        }
    }

    if (len > 0 && buffer[len - 1] == '\r') {
        buffer[len - 1] = '\0';
    }

    return 0;
}

int main(void)
{
    int shmid = shmget(SHM_KEY, sizeof(SharedBlock), 0660);

    if (shmid == -1) {
        perror("shmget: ¿Está el servidor SHM encendido?");
        return EXIT_FAILURE;
    }

    SharedBlock *shm = shmat(shmid, NULL, 0);

    if (shm == (void *)-1) {
        perror("shmat");
        return EXIT_FAILURE;
    }

    char buffer[128];
    int salir = 0;

    while (!salir) {
        int opcion;

        mostrar_menu();

        if (scanf("%d", &opcion) != 1) {
            int ch;

            printf("Entrada inválida.\n");

            while ((ch = getchar()) != '\n' && ch != EOF) {
                /* Limpiar la entrada. */
            }

            if (feof(stdin)) {
                break;
            }

            continue;
        }

        int ch;

        while ((ch = getchar()) != '\n' && ch != EOF) {
            /* Consumir caracteres restantes. */
        }

        if (opcion == 1 || opcion == 2 || opcion == 3) {
            memset(&shm->request, 0, sizeof(shm->request));

            if (opcion == 1) {
                shm->request.operation = OP_CITY;

                if (leer_texto("Ingrese el nombre de la ciudad: ",
                               buffer, sizeof(buffer)) == -1) {
                    break;
                }
            } else if (opcion == 2) {
                shm->request.operation = OP_COUNTRY;

                if (leer_texto("Ingrese el nombre del país: ",
                               buffer, sizeof(buffer)) == -1) {
                    break;
                }
            } else {
                shm->request.operation = OP_ISO2;

                if (leer_texto("Ingrese el código ISO2 (ej. CO): ",
                               buffer, sizeof(buffer)) == -1) {
                    break;
                }
            }

            snprintf(shm->request.value,
                     sizeof(shm->request.value),
                     "%s", buffer);

        } else if (opcion == 4) {
            memset(&shm->request, 0, sizeof(shm->request));
            shm->request.operation = OP_EXIT;
            salir = 1;

        } else {
            printf("Opción no válida.\n");
            continue;
        }

        if (sem_post(&shm->req_ready) == -1) {
            perror("sem_post req_ready");
            break;
        }

        if (salir) {
            if (wait_sem(&shm->resp_ready) == -1) {
                perror("sem_wait resp_ready");
                break;
            }

            if (shm->response.type == RESP_BYE) {
                printf("[Cliente] Salida confirmada por el servidor.\n");
            } else {
                printf("[Cliente] Respuesta inesperada al salir.\n");
            }

            if (sem_post(&shm->resp_consumed) == -1) {
                perror("sem_post resp_consumed");
            }

            break;
        }

        printf("\n--- Resultados de la Búsqueda ---\n");

        for (;;) {
            if (wait_sem(&shm->resp_ready) == -1) {
                perror("sem_wait resp_ready");
                salir = 1;
                break;
            }

            int tipo = shm->response.type;

            switch (tipo) {
                case RESP_ROW:
                    printf("%s\n", shm->response.text);
                    break;

                case RESP_NOT_FOUND:
                    printf("%s\n", shm->response.text);
                    /*
                     * No terminar todavía:
                     * dataset.c envía RESP_END después.
                     */
                    break;

                case RESP_END:
                    printf("%s\n", shm->response.text);
                    printf("--- Fin de la transmisión ---\n");
                    break;

                case RESP_ERROR:
                    fprintf(stderr, "[Error del servidor] %s\n",
                            shm->response.text);
                    break;

                case RESP_BYE:
                    printf("[Cliente] El servidor ha terminado.\n");
                    break;

                default:
                    fprintf(stderr,
                            "[Cliente] Tipo de respuesta desconocido: %d\n",
                            tipo);
                    break;
            }

            /*
             * Confirmar cada mensaje antes de esperar el siguiente.
             */
            if (sem_post(&shm->resp_consumed) == -1) {
                perror("sem_post resp_consumed");
                salir = 1;
                break;
            }

            /*
             * RESP_NOT_FOUND no es terminal en este protocolo.
             * RESP_END y RESP_ERROR sí terminan la búsqueda.
             */
            if (tipo == RESP_END ||
                tipo == RESP_ERROR ||
                tipo == RESP_BYE ||
                (tipo != RESP_ROW && tipo != RESP_NOT_FOUND)) {
                break;
            }
        }
    }

    if (shmdt(shm) == -1) {
        perror("shmdt");
        return EXIT_FAILURE;
    }

    printf("[Cliente] Cliente cerrado.\n");
    return EXIT_SUCCESS;
}
