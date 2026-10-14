//
// Created by Jheyrer on 13/10/2026.
//

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>

#include "protocol.h"
#include "dataset.h"

static int wait_sem(sem_t *sem)
{
    int result;

    do {
        result = sem_wait(sem);
    } while (result == -1 && errno == EINTR);

    return result;
}

/*
 * Publicar una respuesta y esperar su confirmación.
 * Este callback transmite tanto las filas como los mensajes finales.
 */
static int emit_row_shm(const Response *resp, void *ctx)
{
    SharedBlock *shm = (SharedBlock *)ctx;

    shm->response = *resp;

    if (sem_post(&shm->resp_ready) == -1) {
        perror("sem_post resp_ready");
        return -1;
    }

    if (wait_sem(&shm->resp_consumed) == -1) {
        perror("sem_wait resp_consumed");
        return -1;
    }

    return 0;
}

int main(void)
{
    int shmid = shmget(
        SHM_KEY,
        sizeof(SharedBlock),
        IPC_CREAT | IPC_EXCL | 0660
    );

    if (shmid == -1) {
        if (errno == EEXIST) {
            fprintf(stderr,
                    "Ya existe un segmento SHM.\n"
                    "Comprueba si hay otro servidor activo o "
                    "si quedó un segmento abandonado.\n");
        } else {
            perror("shmget");
        }

        return EXIT_FAILURE;
    }

    SharedBlock *shm = shmat(shmid, NULL, 0);

    if (shm == (void *)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        return EXIT_FAILURE;
    }

    /*
     * Inicializar los semáforos del segmento recién creado.
     */
    if (sem_init(&shm->req_ready, 1, 0) == -1) {
        perror("sem_init req_ready");
        goto error_shm;
    }

    if (sem_init(&shm->resp_ready, 1, 0) == -1) {
        perror("sem_init resp_ready");
        sem_destroy(&shm->req_ready);
        goto error_shm;
    }

    if (sem_init(&shm->resp_consumed, 1, 0) == -1) {
        perror("sem_init resp_consumed");
        sem_destroy(&shm->req_ready);
        sem_destroy(&shm->resp_ready);
        goto error_shm;
    }

    printf("[Servidor SHM] Iniciado. SHMID: %d\n", shmid);
    printf("[Servidor SHM] Esperando peticiones...\n");

    for (;;) {
        if (wait_sem(&shm->req_ready) == -1) {
            perror("sem_wait req_ready");
            break;
        }

        if (shm->request.operation == OP_EXIT) {
            memset(&shm->response, 0, sizeof(shm->response));
            shm->response.type = RESP_BYE;

            if (sem_post(&shm->resp_ready) == -1) {
                perror("sem_post resp_ready");
                break;
            }

            if (wait_sem(&shm->resp_consumed) == -1) {
                perror("sem_wait resp_consumed");
                break;
            }

            printf("[Servidor SHM] Solicitud de salida recibida.\n");
            break;
        }

        printf("[Servidor SHM] Procesando búsqueda: %s\n",
               shm->request.value);

        /*
         * IMPORTANTE:
         * search_dataset() ya envía RESP_ROW, RESP_NOT_FOUND,
         * RESP_END y RESP_ERROR mediante emit_row_shm().
         *
         * No enviar otra respuesta después de esta llamada.
         */
        int result = search_dataset(
            "data/worldcities.csv",
            &shm->request,
            emit_row_shm,
            shm
        );

        if (result < 0) {
            fprintf(stderr,
                    "[Servidor SHM] La búsqueda terminó con error.\n");
        } else {
            printf("[Servidor SHM] Búsqueda finalizada: %d coincidencias.\n",
                   result);
        }
    }

    printf("[Servidor SHM] Liberando recursos...\n");

    sem_destroy(&shm->req_ready);
    sem_destroy(&shm->resp_ready);
    sem_destroy(&shm->resp_consumed);

    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);

    printf("[Servidor SHM] Cerrado correctamente.\n");
    return EXIT_SUCCESS;

error_shm:
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    return EXIT_FAILURE;
}
