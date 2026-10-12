#ifndef DATASET_H
#define DATASET_H

#include "protocol.h"

#define DATASET_DEFAULT_PATH "data/worldcities.csv"

/* =========================================================================
 * Indices de columnas del dataset worldcities.csv (11 columnas oficiales)
 * Encabezado verificado:
 * "city","city_ascii","lat","lng","country","iso2","iso3","admin_name","capital","population","id"
 * ========================================================================= */
typedef enum {
    COL_CITY = 0,       /* Nombre Unicode de la ciudad */
    COL_CITY_ASCII = 1, /* Nombre ASCII (criterio principal de ciudad) */
    COL_LAT = 2,        /* Latitud geografica */
    COL_LNG = 3,        /* Longitud geografica */
    COL_COUNTRY = 4,    /* Nombre del pais (criterio de busqueda) */
    COL_ISO2 = 5,       /* Codigo ISO de 2 letras (criterio de busqueda) */
    COL_ISO3 = 6,       /* Codigo ISO de 3 letras */
    COL_ADMIN = 7,      /* Region administrativa (departamento/provincia) */
    COL_CAPITAL = 8,    /* Tipo de capital (primary, admin, etc.) */
    COL_POPULATION = 9, /* Poblacion estimada */
    COL_ID = 10,        /* Identificador unico del registro */
    COL_COUNT = 11      /* Total de columnas esperadas */
} CityColumn;

/* Registro parsed que mantiene punteros a los 11 campos */
typedef struct {
    char *fields[COL_COUNT];
} CityRecord;

/*
 * Prototipo de callback para emitir respuestas desacopladas del medio IPC.
 * - 'resp': puntero a la estructura Response a emitir.
 * - 'ctx': contexto opcional (ej. descriptor de FIFO o puntero a SharedBlock).
 * Retorna 0 si fue exitoso, o un valor negativo si la conexion se interrumpio (ej. EPIPE).
 */
typedef int (*emit_response_fn)(const Response *resp, void *ctx);

/*
 * Parsea una linea CSV in-place respetando comillas dobles, comas internas y \r\n.
 * Modifica la cadena 'line' insertando terminadores nulos ('\0').
 * Retorna el numero de campos extraidos (debe ser COL_COUNT), o -1 en caso de error.
 */
int parse_csv_line(char *line, CityRecord *record);

/*
 * Realiza la busqueda secuencial streaming sobre el dataset en disco.
 * - 'path': ruta al archivo CSV (ej. "data/worldcities.csv").
 * - 'req': solicitud recibida con la operacion y el criterio buscado.
 * - 'emit': funcion callback que se invoca por cada coincidencia encontrada.
 * - 'ctx': contexto pasado al callback.
 * Retorna el total de coincidencias encontradas, o -1 en caso de error de archivo.
 */
int search_dataset(const char *path, const Request *req, emit_response_fn emit, void *ctx);

#endif /* DATASET_H */
