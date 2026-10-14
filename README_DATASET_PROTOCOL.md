# Capa común: protocolo y dataset

Esta base sirve al transporte FIFO y a la futura implementación SHM. Se conservan las firmas públicas, los valores de los enums y las estructuras `Request`, `Response` y `SharedBlock`. En Linux x86-64, `Request` ocupa 132 bytes y `Response` 516; son estructuras nativas para procesos del mismo entorno, no un formato portable entre arquitecturas.

## Contrato de respuestas

| Solicitud / resultado | Secuencia | Acción del cliente |
| --- | --- | --- |
| Búsqueda con coincidencias | Una o más `RESP_ROW`, luego `RESP_END` | Consumir hasta `END` |
| Búsqueda sin coincidencias | `RESP_NOT_FOUND`, luego `RESP_END` | Seguir leyendo tras `NOT_FOUND` |
| Error de solicitud, CSV o archivo | Cero o más filas previas, luego `RESP_ERROR` | Terminar la sesión con error; no esperar `END` |
| `OP_EXIT` | `RESP_BYE` | Cerrar la sesión normalmente |
| Fallo del callback / transporte | Se detienen inmediatamente las emisiones | Cerrar la sesión; no intentar otro mensaje |

`RESP_END` confirma que terminaron correctamente la lectura, el cierre del CSV y las emisiones anteriores. Un error posterior a algunas filas significa que esas filas son resultados parciales. `RESP_NOT_FOUND` nunca termina por sí solo una consulta.

`search_dataset(path, req, emit, ctx)` devuelve el número de coincidencias, o `-1` ante errores de argumentos, operación, CSV, archivo o callback. Si el callback devuelve un valor negativo, conserva su `errno` (o usa `EIO` si no lo estableció), libera el buffer, cierra el archivo y no vuelve a invocarlo. También comprueba los callbacks de `NOT_FOUND`, `END` y `ERROR`. Los argumentos nulos se rechazan con `EINVAL` sin callback.

## CSV admitido y errores

El encabezado debe contener estos once nombres, en este orden; pueden estar entre comillas:

```text
city,city_ascii,lat,lng,country,iso2,iso3,admin_name,capital,population,id
```

`parse_csv_line()` modifica el buffer recibido y devuelve once campos o `-1/EINVAL`. Acepta campos vacíos, comas dentro de comillas, comillas escapadas como `""`, LF, CRLF y una última línea sin salto. Rechaza columnas faltantes/adicionales, comillas sin cerrar, comillas dentro de campos sin delimitar y texto después de una comilla de cierre. No rellena columnas ausentes. No usar `CityRecord` después de un error ni después de reutilizar/liberar la línea.

La búsqueda comprueba también NUL incrustados, el encabezado y cada fila. Una fila malformada aborta con `RESP_ERROR` e identifica su número de línea física (encabezado = 1). Se distingue EOF de fallo de lectura con `ferror()`/`feof()` y se verifica `fclose()`. Una respuesta que no cabe en `text[512]` se rechaza con `EOVERFLOW`, sin truncarla silenciosamente.

El formato es de una fila por línea: no soporta campos con saltos de línea internos, BOM ni espacios fuera de las comillas delimitadoras. No valida tipos numéricos de latitud/población: la práctica compara textos. Las comparaciones usan `strcasecmp()` y las columnas `city_ascii`, `country` e `iso2`.

El archivo se recorre secuencialmente en cada consulta. La memoria del buffer es proporcional a la mayor línea leída, no al número total de registros. El dataset incluido tiene 44.691 registros y un encabezado (44.692 líneas).

## Integración con Persona 3

`SharedBlock`, `SHM_KEY` y sus tres semáforos permanecen sin cambios. La implementación SHM está pendiente y no hay objetivos SHM en el Makefile actual.

El futuro callback SHM debe publicar cada respuesta y esperar su confirmación de consumo, comprobando los retornos y `EINTR`. Esto incluye `NOT_FOUND`, `END`, `ERROR` y `BYE`: el cliente debe confirmar la lectura del último mensaje antes de terminar, para no dejar al servidor esperando. Devolver un valor negativo si falla la entrega y definir la política de terminación de ambos procesos. Los tres semáforos por sí solos no resuelven la muerte de un proceso.

La corrección común se propone mediante `codex/fifo-common-fixes` hacia `develop`, conservando los cuatro commits de Persona 2. Tras la aprobación de Persona 4 y la integración del PR:

```bash
git fetch origin
git switch feature/shared-memory
git merge origin/develop
make test
```

Resolver cualquier conflicto respetando el trabajo nuevo de Persona 3. No hacer reset, rebase forzado ni sobrescribir su rama. La actualización de `main` corresponde a una etapa posterior.

## Validación reproducible

Desde la raíz, en Linux/WSL con GCC, Make y Python 3:

```bash
make test
```

- `tests/test_parser.c`: nueve búsquedas sobre el dataset real; comprueba tanto el retorno como las respuestas. Tokyo/tokyo: 1; Bogota: 2; Colombia/colombia/CO: 714; JP: 1253; ciudad y país inexistentes: 0.
- `tests/test_dataset.c`: CSV válido y malformado, encabezado, vacíos, NUL, operación inválida, callbacks fallidos en filas y mensajes terminales. Inyecta errores de lectura y cierre mediante wrappers de enlace que solo existen en el ejecutable de prueba.
- `tests/test_ipc.c`: transferencias fragmentadas, EOF parcial, `EINTR`, `EPIPE` y cancelación.
- `tests/test_fifo.py`: ejecutables reales, entradas juntas/graduales, señales, errores y propiedad de recursos. Ver [README_FIFO.md](README_FIFO.md).

Estas pruebas cubren los escenarios enumerados; no constituyen una certificación de ausencia de errores ni validan SHM.
