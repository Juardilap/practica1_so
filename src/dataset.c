#include "dataset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

/* =========================================================================
 * Parser CSV robusto in-place
 * ========================================================================= */
int parse_csv_line(char *line, CityRecord *record) {
    if (!line || !record) {
        return -1;
    }

    /* Limpiar retornos de carro (\r) y saltos de linea (\n) al final */
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }

    int col = 0;
    char *p = line;

    while (col < COL_COUNT && *p != '\0') {
        char *start = NULL;

        if (*p == '"') {
            p++; /* Omitir comilla de apertura */
            start = p;

            while (*p != '\0') {
                if (*p == '"') {
                    if (*(p + 1) == '"') {
                        /* Comilla escapada "" dentro del texto -> desplazar caracteres */
                        char *src = p + 1;
                        char *dst = p;
                        while (*src != '\0') {
                            *dst++ = *src++;
                        }
                        *dst = '\0';
                        p++;
                    } else {
                        /* Comilla de cierre del campo */
                        *p = '\0';
                        p++;
                        break;
                    }
                } else {
                    p++;
                }
            }

            /* Si despues de la comilla de cierre hay una coma, avanzarla */
            if (*p == ',') {
                p++;
            }
        } else {
            /* Campo sin comillas */
            start = p;
            while (*p != '\0' && *p != ',') {
                p++;
            }
            if (*p == ',') {
                *p = '\0';
                p++;
            }
        }

        record->fields[col++] = start;
    }

    /* Rellenar cualquier columna faltante con cadena vacia */
    while (col < COL_COUNT) {
        record->fields[col++] = "";
    }

    return col;
}

/* =========================================================================
 * Busqueda secuencial en disco con getline() (O(1) memoria residente)
 * ========================================================================= */
int search_dataset(const char *path, const Request *req, emit_response_fn emit, void *ctx) {
    if (!path || !req || !emit) {
        return -1;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        Response err_resp;
        err_resp.type = RESP_ERROR;
        snprintf(err_resp.text, sizeof(err_resp.text),
                 "Error al abrir dataset '%s': %s", path, strerror(errno));
        emit(&err_resp, ctx);
        return -1;
    }

    char *line = NULL;
    size_t capacity = 0;
    ssize_t nread;

    /* Omitir la primera linea (encabezados del CSV) */
    if ((nread = getline(&line, &capacity, fp)) == -1) {
        free(line);
        fclose(fp);
        Response err_resp;
        err_resp.type = RESP_ERROR;
        snprintf(err_resp.text, sizeof(err_resp.text),
                 "El archivo '%s' esta vacio o no se pudo leer el encabezado.", path);
        emit(&err_resp, ctx);
        return -1;
    }

    int match_count = 0;
    CityRecord record;

    while ((nread = getline(&line, &capacity, fp)) != -1) {
        if (parse_csv_line(line, &record) < COL_COUNT) {
            continue; /* Ignorar lineas corruptas o incompletas */
        }

        int matches = 0;
        switch (req->operation) {
            case OP_CITY:
                if (strcasecmp(record.fields[COL_CITY_ASCII], req->value) == 0) {
                    matches = 1;
                }
                break;
            case OP_COUNTRY:
                if (strcasecmp(record.fields[COL_COUNTRY], req->value) == 0) {
                    matches = 1;
                }
                break;
            case OP_ISO2:
                if (strcasecmp(record.fields[COL_ISO2], req->value) == 0) {
                    matches = 1;
                }
                break;
            default:
                break;
        }

        if (matches) {
            Response row_resp;
            row_resp.type = RESP_ROW;
            snprintf(row_resp.text, sizeof(row_resp.text),
                     "Ciudad: %-20s | Pais: %-15s | ISO2: %-2s | Region: %-20s | Poblacion: %s",
                     record.fields[COL_CITY_ASCII],
                     record.fields[COL_COUNTRY],
                     record.fields[COL_ISO2],
                     record.fields[COL_ADMIN],
                     record.fields[COL_POPULATION]);

            match_count++;
            if (emit(&row_resp, ctx) < 0) {
                /* Receptor cerro la conexion anticipadamente (ej. EPIPE) */
                break;
            }
        }
    }

    free(line);
    fclose(fp);

    /* Si no hubo ninguna coincidencia en todo el dataset, notificar */
    if (match_count == 0) {
        Response nf_resp;
        nf_resp.type = RESP_NOT_FOUND;
        snprintf(nf_resp.text, sizeof(nf_resp.text),
                 "No se encontraron registros para la busqueda: '%s'", req->value);
        emit(&nf_resp, ctx);
    }

    /* Siempre emitir RESP_END para indicar el fin ordenado de la secuencia */
    Response end_resp;
    end_resp.type = RESP_END;
    snprintf(end_resp.text, sizeof(end_resp.text),
             "Busqueda finalizada. Total coincidencias encontradas: %d", match_count);
    emit(&end_resp, ctx);

    return match_count;
}
