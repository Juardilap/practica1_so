#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <semaphore.h>
#include <sys/types.h>

/* =========================================================================
 * Constantes de configuracion de IPC y buffers
 * ========================================================================= */
#define VALUE_SIZE 128
#define TEXT_SIZE  512

#define FIFO_REQ   "/tmp/so_p1_req.fifo"
#define FIFO_RESP  "/tmp/so_p1_resp.fifo"
#define SHM_KEY    0x503141

/* =========================================================================
 * Operaciones solicitadas por el cliente
 * ========================================================================= */
typedef enum {
    OP_CITY = 1,        /* Busqueda por nombre ASCII de ciudad (city_ascii) */
    OP_COUNTRY = 2,     /* Busqueda por nombre de pais (country) */
    OP_ISO2 = 3,        /* Busqueda por codigo ISO de 2 letras (iso2) */
    OP_EXIT = 4         /* Orden de terminacion controlada de procesos */
} Operation;

/* =========================================================================
 * Tipos de respuesta emitidos por el servidor
 * ========================================================================= */
typedef enum {
    RESP_ROW = 1,       /* Fila coincidente formateada */
    RESP_END = 2,       /* Fin de la secuencia de registros para una consulta */
    RESP_NOT_FOUND = 3, /* Cero coincidencias en todo el dataset */
    RESP_ERROR = 4,     /* Error de procesamiento, archivo no encontrado, etc. */
    RESP_BYE = 5        /* Confirmacion de apagado y despedida controlada */
} ResponseType;

/* =========================================================================
 * Estructuras de datos transmitidas por el canal IPC
 * ========================================================================= */
typedef struct {
    int operation;              /* Una de las constantes Operation */
    char value[VALUE_SIZE];     /* Criterio de busqueda, ej. "Bogota", "Colombia" */
} Request;

typedef struct {
    int type;                   /* Una de las constantes ResponseType */
    char text[TEXT_SIZE];       /* Texto formateado para visualizacion del usuario */
} Response;

/* =========================================================================
 * Estructura de Memoria Compartida System V (SharedBlock)
 * Coordinada con 3 semaforos POSIX sin nombre (process-shared)
 * ========================================================================= */
typedef enum {
    STATE_IDLE = 0,
    STATE_REQUEST_READY,
    STATE_RESPONSE_READY,
    STATE_CLOSING
} CommState;

typedef struct {
    sem_t req_ready;            /* Cliente publica solicitud -> Servidor despierta */
    sem_t resp_ready;           /* Servidor publica fila -> Cliente despierta */
    sem_t resp_consumed;        /* Cliente confirma lectura -> Servidor continua */
    int state;                  /* Estado de ciclo de vida (CommState) */
    Request request;            /* Buffer de solicitud activa */
    Response response;          /* Buffer de respuesta activa en streaming */
} SharedBlock;

#endif /* PROTOCOL_H */
