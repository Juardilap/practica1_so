#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "dataset.h"
#include "protocol.h"

/* Estructura para capturar resultados del callback en la prueba */
typedef struct {
    int row_count;
    int end_count;
    int not_found_count;
    int error_count;
} TestCollector;

static int test_emitter(const Response *resp, void *ctx) {
    TestCollector *collector = (TestCollector *)ctx;

    switch (resp->type) {
        case RESP_ROW:
            collector->row_count++;
            if (collector->row_count <= 3) {
                printf("    [ROW %d] %s\n", collector->row_count, resp->text);
            } else if (collector->row_count == 4) {
                printf("    ... (mostrando solo primeras 3 filas para brevedad) ...\n");
            }
            break;
        case RESP_END:
            collector->end_count++;
            printf("    [END] %s\n", resp->text);
            break;
        case RESP_NOT_FOUND:
            collector->not_found_count++;
            printf("    [NOT_FOUND] %s\n", resp->text);
            break;
        case RESP_ERROR:
            collector->error_count++;
            printf("    [ERROR] %s\n", resp->text);
            break;
        default:
            printf("    [UNKNOWN TYPE %d] %s\n", resp->type, resp->text);
            break;
    }
    return 0;
}

static void run_test(const char *test_name, int op, const char *value, int expected_rows, int expected_not_found) {
    printf("\n==================================================\n");
    printf("TEST: %s (Op: %d, Criterio: '%s')\n", test_name, op, value);
    printf("==================================================\n");

    Request req;
    req.operation = op;
    strncpy(req.value, value, sizeof(req.value) - 1);
    req.value[sizeof(req.value) - 1] = '\0';

    TestCollector collector = {0, 0, 0, 0};

    int matches = search_dataset(DATASET_DEFAULT_PATH, &req, test_emitter, &collector);

    printf("--> Coincidencias retornadas: %d (Filas emitidas: %d, Fin: %d, NoFound: %d, Errores: %d)\n",
           matches, collector.row_count, collector.end_count, collector.not_found_count, collector.error_count);
    fflush(stdout);

    assert(collector.error_count == 0);
    assert(collector.end_count == 1);
    assert(collector.row_count == expected_rows);
    assert(collector.not_found_count == expected_not_found);
    printf("--> RESULTADO: PASS [OK]\n");
    fflush(stdout);
}

int main(void) {
    printf("==================================================\n");
    printf("EJECUTANDO SUITE DE PRUEBAS UNITARIAS: DATASET & PARSER\n");
    printf("Dataset objetivo: %s\n", DATASET_DEFAULT_PATH);
    printf("==================================================\n");

    /* 1. Busqueda exacta por ciudad ASCII */
    run_test("Ciudad Tokyo (Exacta)", OP_CITY, "Tokyo", 1, 0);

    /* 2. Busqueda insensible a mayusculas/minusculas */
    run_test("Ciudad tokyo (Minusculas)", OP_CITY, "tokyo", 1, 0);
    run_test("Ciudad Bogota (Capital y US)", OP_CITY, "Bogota", 2, 0);

    /* 3. Busqueda por Pais (muchos registros en streaming) */
    run_test("Pais Colombia (714 ciudades)", OP_COUNTRY, "Colombia", 714, 0);
    run_test("Pais colombia (Minusculas)", OP_COUNTRY, "colombia", 714, 0);

    /* 4. Busqueda por ISO2 */
    run_test("Codigo ISO2 'CO'", OP_ISO2, "CO", 714, 0);
    run_test("Codigo ISO2 'JP'", OP_ISO2, "JP", 1253, 0);

    /* 5. Busqueda inexistente (RESP_NOT_FOUND) */
    run_test("Ciudad Inexistente (AtlantisCity)", OP_CITY, "AtlantisCity", 0, 1);
    run_test("Pais Inexistente (PaisInventado)", OP_COUNTRY, "PaisInventado", 0, 1);

    printf("\n==================================================\n");
    printf("TODAS LAS PRUEBAS DE PERSONA 1 PASARON EXITOSAMENTE (100%% OK)\n");
    printf("==================================================\n");

    return 0;
}
