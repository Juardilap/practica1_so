# Guía de Trabajo para el Equipo: 4 Integrantes y 4 Ramas

> Estado: FIFO y SHM integradas en `feature/testing-docs`, con pruebas y herramientas de observación/limpieza. Ejecutar `make test`. Consultar [LEEME.md](LEEME.md) para arquitectura, evidencias, límites y recuperación. Esta guía de equipo es apoyo de trabajo; no sustituye el enunciado oficial.
>
> Integración acordada: merge de `develop` y `feature/shared-memory` en `feature/testing-docs` → PR a `develop` → validación de Persona 4 y revisión cruzada de Persona 2 → merge aprobado. Abrir `develop` → `main` solo para entrega final aprobada, probando el candidato exacto. Preservar todos los commits; sin squash, rebase ni force-push.

**Práctica 1 de Sistemas Operativos — Universidad Nacional de Colombia**

Esta guía es un manual práctico dirigido a los **4 integrantes del grupo**. Explica paso a paso qué debe hacer cada persona, en qué rama debe trabajar, qué archivos le corresponden, cómo probar su componente y cómo integrarlo con el resto del proyecto.

---

## 1. Visión General del Proyecto

- **Objetivo:** Sistema cliente-servidor en lenguaje C sobre Linux (WSL2 Ubuntu 24.04) que busca ciudades en el archivo `data/worldcities.csv` (44.692 líneas) sin cargarlo completo en memoria RAM (memoria proporcional a la mayor línea).
- **Dos arquitecturas idénticas en funcionalidad:**
  1. **Versión A (FIFO):** 2 tuberías nombradas (`/tmp/so_p1_req.fifo` y `/tmp/so_p1_resp.fifo`).
  2. **Versión B (SHM):** Memoria compartida System V (`shmget`, `shmat`, `shmdt`, `shmctl`) con 3 semáforos POSIX process-shared (`req_ready`, `resp_ready`, `resp_consumed`).
- **Criterios de búsqueda acordados:**
  1. Ciudad ASCII (`city_ascii`, ej. `Bogota`, `Tokyo`)
  2. País (`country`, ej. `Colombia` -> devuelve múltiples resultados en streaming)
  3. Código ISO2 (`iso2`, ej. `CO`, `JP`)
  4. Salir (`OP_EXIT`)
  *(Insensible a mayúsculas/minúsculas usando `strcasecmp`).*

---

## 2. Estructura de Carpetas del Proyecto

```text
practica1_so/
├── data/
│   └── worldcities.csv        # Dataset con 44.691 registros y un encabezado (en disco, streaming con getline)
├── include/
│   ├── protocol.h             # Definición de Request, Response, SharedBlock, constantes y enums
│   ├── dataset.h              # Prototipos del parser y search_dataset()
│   ├── ipc_utils.h            # Transferencias completas y cancelables
│   ├── console_input.h        # Lector persistente compartido por los dos clientes
│   └── shm_utils.h            # Sesión SHM, identidad, esperas y limpieza
├── src/
│   ├── dataset.c              # Parser de comillas CSV y búsqueda secuencial línea a línea
│   ├── ipc_utils.c            # Transferencias completas, EINTR y espera cancelable con ppoll
│   ├── console_input.c        # Menú, entrada por lotes, EOF y líneas excesivas
│   ├── shm_utils.c            # Publicación atómica, pidfd y sem_clockwait
│   ├── cliente_fifo.c         # Cliente interactivo y CLI para versión FIFO
│   ├── servidor_fifo.c        # Servidor de búsqueda para versión FIFO
│   ├── cliente_shm.c          # Cliente interactivo y CLI para versión Memoria Compartida
│   └── servidor_shm.c         # Servidor de búsqueda para versión Memoria Compartida
├── scripts/
│   ├── test_matrix.sh         # Ejecuta la matriz completa con make test
│   ├── observe.py             # ps, /proc, lsof e ipcs para PIDs explícitos
│   ├── capture_evidence.py    # Capturas reproducibles con recursos privados
│   └── cleanup_ipc.py         # Rechaza recursos activos, ajenos o no verificables
├── tests/
│   └── test_parser.c          # Harness de prueba para validar el parser y dataset sin IPC
├── Makefile                   # Reglas de compilación estricta (-Wall -Wextra -Wpedantic -pthread)
├── GUIA_ORGANIZACION.md       # Documento maestro con las 10 fases, preguntas de sustentación y rúbrica
├── GUIA_EQUIPO.md             # Esta guía rápida por integrante y rama
├── LEEME.md                   # Documentación técnica final de entrega para el profesor
├── README.md                  # Presentación general del repositorio
└── .gitignore                 # Filtro de binarios compilados y temporales
```

---

## 3. Modelo de Trabajo en Git

```mermaid
gitGraph
   commit id: "setup inicial"
   branch develop
   checkout develop
   branch feature/dataset-protocol
   checkout feature/dataset-protocol
   commit id: "Persona 1: protocol + dataset"
   checkout develop
   merge feature/dataset-protocol
   branch feature/fifo
   checkout feature/fifo
   commit id: "Persona 2: cliente/servidor fifo"
   checkout develop
   merge feature/fifo
   branch feature/shared-memory
   checkout feature/shared-memory
   commit id: "Persona 3: cliente/servidor shm"
   checkout develop
   branch feature/testing-docs
   checkout feature/testing-docs
   merge feature/shared-memory
   commit id: "Persona 4: tests, observe, LEEME"
   checkout develop
   merge feature/testing-docs
   checkout main
   merge develop tag: "v1.0-entrega"
```

### Reglas básicas de Git:
1. **NUNCA hacer commit directo a `main`.**
2. Cada integrante trabaja **exclusivamente en su rama de características**.
3. Al terminar su trabajo, sincroniza y solicita revisión a su compañero asignado (Revisión Cruzada).
4. El revisor aprueba y se integra la rama en `develop`.

---

## 4. Manual Detallado por Integrante

---

### 👤 Persona 1: Dataset y Protocolo Común
- **Rama asignada:** `feature/dataset-protocol`
- **Revisor asignado:** Persona 3
- **Comandos para empezar:**
  ```bash
  git checkout feature/dataset-protocol
  git pull origin feature/dataset-protocol   # (si hay cambios remotos)
  ```
- **Archivos a su cargo:**
  1. `include/protocol.h`:
     - Definir enums `Operation` (`OP_CITY`, `OP_COUNTRY`, `OP_ISO2`, `OP_EXIT`) y `ResponseType` (`RESP_ROW`, `RESP_END`, `RESP_NOT_FOUND`, `RESP_ERROR`, `RESP_BYE`).
     - Definir estructuras `Request` (campo `value[128]`), `Response` (campo `text[512]`) y `SharedBlock`.
     - Definir constantes `#define FIFO_REQ "/tmp/so_p1_req.fifo"`, `#define FIFO_RESP "/tmp/so_p1_resp.fifo"`, `#define SHM_KEY 0x503141`.
  2. `include/dataset.h` y `src/dataset.c`:
     - Parser CSV carácter a carácter que maneje comillas dobles, comas internas (ej. `"Washington, D.C."`) y retornos `\r\n`.
     - Función `search_dataset(path, req, emit_fn, ctx)` que lea secuencialmente con `getline()`.
     - Comparaciones insensibles a mayúsculas con `strcasecmp()`.
  3. `tests/test_parser.c`:
     - Programa de prueba aislado para buscar "Tokyo", "Colombia", "CO" y "CiudadInexistente" sin usar todavía FIFOs ni SHM.
- **Cómo validar tu trabajo:**
  ```bash
  make bin/test_parser
  ./bin/test_parser
  ```
- **Cómo entregar tu rama:**
  ```bash
  git add include/protocol.h include/dataset.h src/dataset.c tests/test_parser.c
  git commit -m "feat: implement protocol structures and CSV sequential parser"
  git push origin feature/dataset-protocol
  ```

---

### 👤 Persona 2: Versión A (Tuberías Nombradas / FIFOs)
- **Rama asignada:** `feature/fifo`
- **Revisor asignado:** Persona 4
- **Comandos para empezar:**
  ```bash
  git checkout feature/fifo
  ```
- **Archivos a su cargo:**
  1. `src/servidor_fifo.c`:
     - Crear FIFOs de forma idempotente con `mkfifo("/tmp/so_p1_req.fifo", 0660)` y `mkfifo("/tmp/so_p1_resp.fifo", 0660)`.
     - Tomar el bloqueo exclusivo de sesión antes de crear/reutilizar las FIFOs. Abrir `FIFO_REQ` en `O_RDONLY | O_NONBLOCK` y luego `FIFO_RESP` en `O_WRONLY | O_NONBLOCK`; esperar con `ppoll` durante la conexión para permitir cancelación sin carreras de señales.
     - Proteger contra caída por desconexión del cliente ignorando `SIGPIPE`: `signal(SIGPIPE, SIG_IGN)` y chequear `errno == EPIPE`.
     - Bucle de servicio: recibir `Request` con `read_full_cancelable()`. Si es búsqueda, invocar `search_dataset()`; si es `OP_EXIT`, enviar `RESP_BYE`, cerrar descriptores y hacer `unlink()` de las dos FIFOs.
  2. `src/cliente_fifo.c`:
     - Abrir `FIFO_REQ` en `O_WRONLY` y luego `FIFO_RESP` en `O_RDONLY`.
     - Presentar menú por consola con opciones 1 a 4.
     - Enviar `Request` con `write_full()`.
     - Bucle de lectura con `read_full()` imprimiendo respuestas hasta `RESP_END`; `RESP_NOT_FOUND` es informativo y debe consumirse también el `END` posterior. `RESP_ERROR` es terminal sin `END` y cierra la sesión con error; `RESP_BYE` confirma la salida.
- **Cómo validar tu trabajo:**
  - Abrir dos terminales:
    - Terminal 1: `./servidor_fifo`
    - Terminal 2: `./cliente_fifo`
  - Realizar búsquedas de "Tokyo", "Colombia" (múltiples filas) y seleccionar opción 4 (Salir). Verificar que `/tmp/so_p1_*.fifo` se eliminen automáticamente.
- **Cómo entregar tu rama:**
  ```bash
  git add src/servidor_fifo.c src/cliente_fifo.c
  git commit -m "feat: implement complete FIFO client and server with SIGPIPE handling"
  git push origin feature/fifo
  ```

---

### 👤 Persona 3: Versión B (Memoria Compartida System V + Semáforos)
- **Rama asignada:** `feature/shared-memory`
- **Revisor asignado:** Persona 1
- **Comandos para empezar:**
  ```bash
  git checkout feature/shared-memory
  ```
- **Archivos a su cargo:**
  1. `src/servidor_shm.c`:
     - Crear exclusivamente con `shmget(SHM_KEY, sizeof(SharedBlock), IPC_CREAT | IPC_EXCL | 0660)`; nunca reinicializar un segmento existente. La gestión está en `shm_utils.c`.
     - Inicializar los 3 semáforos POSIX en memoria compartida:
       - `sem_init(&shm->req_ready, 1, 0)` (`pshared=1`)
       - `sem_init(&shm->resp_ready, 1, 0)`
       - `sem_init(&shm->resp_consumed, 1, 0)`
     - Publicar metadatos e inicialización mediante atómicos release/acquire. Esperar solicitud con `shm_wait()`: `sem_clockwait(CLOCK_MONOTONIC)` en intervalos de 200 ms, comprobando señales, estado y vida del cliente con `pidfd`.
     - Por cada fila encontrada:
       1. Copiar fila a `shm->response`.
       2. Señalizar disponibilidad: `sem_post(&shm->resp_ready)`.
       3. Esperar confirmación con `shm_wait(..., &shm->resp_consumed)` antes de sobrescribir el buffer; también confirmar mensajes terminales.
     - Al salir: publicar cierre, marcar `IPC_RMID`, esperar hasta dos segundos al desacoplamiento y destruir semáforos solo si no hay usuarios. Comprobar retornos y desacoplar con `shmdt()`.
  2. `src/cliente_shm.c`:
     - Tomar bloqueo exclusivo de cliente; comprobar propietario, tamaño, versión e identidad del creador. Esperar inicialización hasta cinco segundos antes de tocar semáforos; rechazar segmentos incompatibles o residuales.
     - Menú interactivo: copiar solicitud a `shm->request` y notificar con `sem_post(&shm->req_ready)`.
     - Bucle de lectura: `shm_wait(..., &shm->resp_ready)`, copiar mensaje, confirmar con `sem_post(&shm->resp_consumed)` y luego procesarlo. `ERROR` cierra con fallo; EOF envía `OP_EXIT`. Usar el lector común `console_input.c`.
- **Cómo validar tu trabajo:**
  - Ejecutar servidor y cliente en terminales separadas.
  - Comprobar con `ipcs -m` que el bloque de memoria existe con permisos 660.
  - Al salir normalmente, comprobar con `ipcs -m` que el bloque se eliminó del sistema.
- **Cómo entregar tu rama:**
  ```bash
  git add src/servidor_shm.c src/cliente_shm.c
  git commit -m "feat: implement shared memory and 3-semaphore synchronization"
  git push origin feature/shared-memory
  ```

---

### 👤 Persona 4: Integración, Herramientas del SO, Pruebas y Documentación
- **Rama asignada:** `feature/testing-docs`
- **Revisor asignado:** Persona 2
- **Comandos para empezar:**
  ```bash
  git checkout feature/testing-docs
  ```
- **Archivos a su cargo:**
  1. `Makefile`:
     - Compilación estricta con flags: `CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -D_GNU_SOURCE -Iinclude`, `LDFLAGS = -pthread`.
     - Targets actuales: `all`, los cuatro ejecutables FIFO/SHM, `clean`, `test` y binarios aislados de prueba; SHM enlaza con `-pthread`.
  2. `include/ipc_utils.h` y `src/ipc_utils.c`:
     - Implementar `read_full()` y `write_full()` que manejen fragmentación de bytes, `EINTR` y `EPIPE`.
     - Implementar registro de manejadores `sigaction()` para captura limpia de `SIGINT` y `SIGTERM`.
  3. `scripts/test_matrix.sh`:
     - Ejecutar con `bash scripts/test_matrix.sh`: pruebas C, 15 casos FIFO, 18 SHM y limpieza FIFO; muchos contienen varios subcasos.
  4. `scripts/observe.py` y `scripts/capture_evidence.py`:
     - Inspeccionar PIDs explícitos con `ps`, `/proc`, `lsof` e `ipcs`; capturar `strace` desde el arranque en un entorno privado reproducible.
  5. `scripts/cleanup_ipc.py`:
     - Script de limpieza de emergencia: verificar que no haya sesión activa y comprobar identidad/tipo antes de retirar recursos residuales; no borrar el archivo de bloqueo de una sesión ni usar limpieza global sobre recursos ajenos.
  6. `LEEME.md`:
     - Revisar el LEEME y las dos observaciones capturadas; contrastar la guía de apoyo con el enunciado oficial antes de aprobar la entrega final.
- **Cómo validar tu trabajo:**
  ```bash
  make clean && make
  make test
  ```
- **Cómo entregar tu rama:**
  ```bash
  git add Makefile include/ipc_utils.h src/ipc_utils.c scripts/* LEEME.md
  git commit -m "feat: add build system, ipc utilities, test matrix and LEEME documentation"
  git push origin feature/testing-docs
  ```

---

## 5. Tabla de Herramientas del Sistema Operativo Obligatorias

La guía de apoyo solicita al menos **dos observaciones concretas** en `LEEME.md`. Preparar la sustentación con estas herramientas y contrastar los requisitos con el enunciado oficial:

| Herramienta | Uso Esperado en la Práctica | Comando Ejemplo para Probar |
| :--- | :--- | :--- |
| **`ps` / `pstree`** | Identificar PID, PPID, estado y verificar que cliente y servidor **no son procesos emparentados** (no creados con `fork()`). | `ps -o pid,ppid,stat,wchan,cmd -C servidor_fifo`<br>`pstree -p` |
| **`/proc/<pid>/`** | Examinar información que el kernel expone sobre el proceso (`status`, descriptores y función de espera). | `cat /proc/$PID/status`<br>`ls -l /proc/$PID/fd`<br>`cat /proc/$PID/wchan` |
| **`lsof`** | Identificar archivos y descriptores abiertos: ver los dos extremos de las FIFOs en `/tmp` y el archivo `worldcities.csv` abierto por el servidor. | `lsof -p $PID_SERVIDOR`<br>`lsof /tmp/so_p1_req.fifo` |
| **`strace`** | Observar llamadas al sistema en tiempo real y evidenciar cuándo y dónde se bloquea un proceso. | `strace -f -e trace=openat,read,write,ppoll,rt_sigprocmask,close ./servidor_fifo`<br>`strace -f -e trace=shmget,shmat,futex ./servidor_shm` |
| **`ipcs` / `ipcrm`** | Inspeccionar recursos IPC creados en el kernel (segmentos de memoria compartida) y eliminarlos si quedan huérfanos. | `ipcs -m`<br>`ipcrm -m <SHMID>` |
| **`top` / `htop`** | Observar estado y consumo de recursos durante la búsqueda, constatando que la memoria RAM se mantiene proporcional a la mayor línea porque el archivo se lee secuencialmente desde disco. | `top -p $PID_SERVIDOR` |

### Las Dos Observaciones Obligatorias para `LEEME.md`:
1. **Observación 1 (Bloqueo en FIFO con `strace` y `wchan`):**
   - Ejecutar el servidor FIFO solo.
   - En el diseño inicial con aperturas bloqueantes, el servidor solo esperaba primero en `open()`. Solo tras conectar un escritor podía esperar en `read()`. En la versión cancelable actual, observar la espera de conexión y de datos en `ppoll` (ver README_FIFO).
   - Registrar el `wchan` real y el estado en `/proc/<PID>/status`; el nombre de espera depende del kernel y no debe suponerse `pipe_read`.
   - Mostrar cómo, al iniciar el cliente y enviar una consulta, `ppoll` indica disponibilidad y después se leen los bytes recibidos.
2. **Observación 2 (Inspección y Limpieza de Memoria Compartida con `ipcs` e `ipcrm`):**
   - Iniciar `servidor_shm`, listar con `ipcs -m` mostrando `shmid`, `nattch = 1` y clave `0x00503141`. El capturador usa una clave privada y registra su valor real. Al conectar el cliente, observar `nattch = 2`.
   - Provocar una terminación abrupta con `kill -9 $PID_SERVIDOR` (simulando un fallo sin cleanup).
   - Mostrar que el segmento permanece huérfano en el kernel con `nattch = 0`.
   - Documentar la eliminación manual con `ipcrm -m <shmid>`.

---

## 6. Restricciones Técnicas Críticas (No olvidar)

- **Lenguaje:** C estándar sobre Linux.
- **Compilador:** `gcc` con `Makefile`.
- **Ejecutables separados:** Cliente y servidor son programas independientes (no usan `fork()` ni tuberías anónimas heredadas).
- **Verificación rigurosa de retornos:** En **todas** las llamadas al sistema (`fopen`, `getline`, `mkfifo`, `open`, `read`, `write`, `close`, `unlink`, `shmget`, `shmat`, `shmdt`, `shmctl`, `sem_init`, `sem_wait`, `sem_post`, `sem_destroy`), comprobar siempre si devuelven `-1` o `NULL` e informar el error con `perror()` o `fprintf(stderr, ...)`.
## Contrato común vigente

Búsqueda con resultados: `RESP_ROW` repetido → `RESP_END`. Sin resultados: `RESP_NOT_FOUND` → `RESP_END`. Error: `RESP_ERROR` terminal, sin `END`; cerrar la sesión. Salida: `RESP_BYE`. Cualquier callback negativo detiene inmediatamente las emisiones y `search_dataset()` devuelve `-1`. CSV inválido (incluido encabezado), errores de lectura y de cierre abortan la consulta; las filas anteriores a un error son parciales.

En SHM, confirmar el consumo de todos los mensajes, incluidos los terminales, antes de salir. Persona 4 valida la integración y Persona 2 realiza la revisión cruzada; Persona 3 conserva la autoría de su implementación inicial en el historial.
