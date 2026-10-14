# Capa común: protocolo y dataset

Esta base sirve a FIFO y SHM. Se conservan las firmas públicas, los códigos y las estructuras `Request` y `Response`. En Linux x86-64, `Request` ocupa 132 bytes y `Response` 516; son estructuras nativas para procesos del mismo entorno, no un formato portable entre arquitecturas. `SharedBlock` se amplió de forma acordada a la versión 2; requiere recompilar juntos los ejecutables SHM.

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

## Integración de Persona 3

Se conservan `SHM_KEY` y los tres semáforos. `SharedBlock` añade marca, versión, tamaño, publicación atómica de inicialización y registro, PID e identidad de inicio de ambos procesos. El cliente comprueba propietario, tamaño, versión e inicialización antes de usar semáforos. La publicación release/acquire hace visibles los metadatos inicializados.

`shm_emit()` publica cada respuesta y espera su confirmación de consumo. El cliente copia y confirma todos los mensajes, incluidos `NOT_FOUND`, `END`, `ERROR`, `BYE` y los mensajes inválidos recibidos, antes de procesarlos o salir. Un fallo de entrega devuelve negativo. Los tres semáforos por sí solos no detectan la muerte de un proceso: `shm_utils.c` combina esperas de 200 ms con `pidfd`, identidad de inicio y estado atómico. El detalle y los límites están en [LEEME.md](LEEME.md).

La base común/FIFO ya entró en `develop` mediante PR #1. La integración actual conserva por merge el trabajo original de Persona 3 en `feature/testing-docs`. Para revisar esa base conjunta:

```bash
git fetch origin
git switch feature/testing-docs
git merge --ff-only origin/feature/testing-docs
make test
```

El siguiente PR es `feature/testing-docs` → `develop`; `develop` → `main` corresponde a la entrega final aprobada. Preservar el historial mediante merge.

## Validación reproducible

Desde la raíz, en Linux/WSL con GCC, Make y Python 3:

```bash
make test
```

- `tests/test_parser.c`: nueve búsquedas sobre el dataset real; comprueba tanto el retorno como las respuestas. Tokyo/tokyo: 1; Bogota: 2; Colombia/colombia/CO: 714; JP: 1253; ciudad y país inexistentes: 0.
- `tests/test_dataset.c`: CSV válido y malformado, encabezado, vacíos, NUL, operación inválida, callbacks fallidos en filas y mensajes terminales. Inyecta errores de lectura y cierre mediante wrappers de enlace que solo existen en el ejecutable de prueba.
- `tests/test_ipc.c`: transferencias fragmentadas, EOF parcial, `EINTR`, `EPIPE` y cancelación.
- `tests/test_fifo.py`: ejecutables reales, entradas juntas/graduales, señales, errores y propiedad de recursos. Ver [README_FIFO.md](README_FIFO.md).
- `tests/test_shm.py`: las nueve búsquedas sobre SHM, entrada, errores terminales, inicialización, exclusión, señales, muerte de procesos, acuses y limpieza segura.
- `tests/test_cleanup.py`: rechazo de sesiones FIFO activas, descriptores abiertos, archivos normales y enlaces; conservación del inode de bloqueo.

Estas pruebas cubren los escenarios enumerados; no constituyen una certificación de ausencia de errores. Los resultados y el commit observado se registran en el LEEME.
