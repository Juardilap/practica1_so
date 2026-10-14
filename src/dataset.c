#include "dataset.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int parse_csv_line(char *line, CityRecord *record) {
    if (!line || !record) { errno = EINVAL; return -1; }
    memset(record, 0, sizeof(*record));
    size_t len = strlen(line);
    if (len && line[len - 1] == '\n') {
        line[--len] = '\0';
        if (len && line[len - 1] == '\r') line[--len] = '\0';
    }
    char *in = line, *out = line;
    for (int col = 0; col < COL_COUNT; ++col) {
        record->fields[col] = out;
        if (*in == '"') {
            ++in;
            for (;;) {
                if (!*in || *in == '\r' || *in == '\n') goto malformed;
                if (*in == '"') {
                    ++in;
                    if (*in != '"') break;
                }
                *out++ = *in++;
            }
            if (*in && *in != ',') goto malformed;
        } else {
            while (*in && *in != ',') {
                if (*in == '"' || *in == '\r' || *in == '\n') goto malformed;
                *out++ = *in++;
            }
        }
        int more = (*in == ',');
        if (more) ++in; /* Preserve delimiter before the in-place NUL. */
        *out++ = '\0';
        if (more != (col < COL_COUNT - 1)) goto malformed;
    }
    return COL_COUNT;
malformed:
    memset(record, 0, sizeof(*record));
    errno = EINVAL;
    return -1;
}

static int send_response(emit_response_fn emit, void *ctx, const Response *r) {
    errno = 0;
    if (emit(r, ctx) < 0) {
        if (!errno) errno = EIO;
        return -1;
    }
    return 0;
}

static int report_error(emit_response_fn emit, void *ctx, int reason,
                        const char *message) {
    Response r = { .type = RESP_ERROR };
    snprintf(r.text, sizeof(r.text), "%s", message);
    if (send_response(emit, ctx, &r) < 0) return -1;
    errno = reason;
    return -1;
}

int search_dataset(const char *path, const Request *req,
                   emit_response_fn emit, void *ctx) {
    static const char *const header[COL_COUNT] = {
        "city", "city_ascii", "lat", "lng", "country", "iso2", "iso3",
        "admin_name", "capital", "population", "id"
    };
    if (!path || !req || !emit) { errno = EINVAL; return -1; }
    if (!memchr(req->value, '\0', sizeof(req->value)) ||
        req->operation < OP_CITY || req->operation > OP_ISO2)
        return report_error(emit, ctx, EINVAL, "Solicitud de busqueda invalida.");
    FILE *fp = fopen(path, "r");
    char error[TEXT_SIZE] = {0};
    if (!fp) {
        int reason = errno;
        snprintf(error, sizeof(error), "Error al abrir dataset: %s", strerror(reason));
        return report_error(emit, ctx, reason, error);
    }
    char *line = NULL;
    size_t capacity = 0, line_number = 0;
    int matches = 0, reason = 0, emit_failed = 0;
    int column = req->operation == OP_CITY ? COL_CITY_ASCII :
                 req->operation == OP_COUNTRY ? COL_COUNTRY : COL_ISO2;
    for (;;) {
        errno = 0;
        ssize_t size = getline(&line, &capacity, fp);
        if (size < 0) {
            if (ferror(fp) || !feof(fp)) {
                reason = errno ? errno : EIO;
                snprintf(error, sizeof(error), "Error leyendo dataset tras linea %zu: %s",
                         line_number, strerror(reason));
            } else if (!line_number) {
                reason = EINVAL;
                snprintf(error, sizeof(error), "Dataset vacio: falta encabezado en linea 1.");
            }
            break;
        }
        ++line_number;
        CityRecord record;
        if ((size_t)size != strlen(line) || parse_csv_line(line, &record) != COL_COUNT) {
            reason = EINVAL;
            snprintf(error, sizeof(error), "CSV malformado en linea %zu: se requieren once columnas validas.", line_number);
            break;
        }
        if (line_number == 1) {
            for (int i = 0; i < COL_COUNT; ++i) {
                if (strcmp(record.fields[i], header[i])) {
                    reason = EINVAL;
                    snprintf(error, sizeof(error), "Encabezado incorrecto en linea 1, columna %d.", i + 1);
                    break;
                }
            }
            if (reason) break;
            continue;
        }
        if (strcasecmp(record.fields[column], req->value)) continue;
        Response row = { .type = RESP_ROW };
        int written = snprintf(row.text, sizeof(row.text),
            "Ciudad: %-20s | Pais: %-15s | ISO2: %-2s | Region: %-20s | Poblacion: %s",
            record.fields[COL_CITY_ASCII], record.fields[COL_COUNTRY],
            record.fields[COL_ISO2], record.fields[COL_ADMIN], record.fields[COL_POPULATION]);
        if (written < 0 || (size_t)written >= sizeof(row.text) || matches == INT_MAX) {
            reason = EOVERFLOW;
            snprintf(error, sizeof(error), "Respuesta demasiado grande en linea %zu.", line_number);
            break;
        }
        if (send_response(emit, ctx, &row) < 0) {
            reason = errno;
            emit_failed = 1;
            break;
        }
        ++matches;
    }
    free(line);
    if (fclose(fp) != 0 && !reason) {
        reason = errno ? errno : EIO;
        snprintf(error, sizeof(error), "Error cerrando dataset: %s", strerror(reason));
    }
    if (emit_failed) { errno = reason; return -1; }
    if (reason) return report_error(emit, ctx, reason, error);
    if (!matches) {
        Response missing = { .type = RESP_NOT_FOUND };
        snprintf(missing.text, sizeof(missing.text), "No se encontraron registros para la busqueda: '%s'", req->value);
        if (send_response(emit, ctx, &missing) < 0) return -1;
    }
    Response end = { .type = RESP_END };
    snprintf(end.text, sizeof(end.text), "Busqueda finalizada. Total coincidencias encontradas: %d", matches);
    if (send_response(emit, ctx, &end) < 0) return -1;
    return matches;
}
