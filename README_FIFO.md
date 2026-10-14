# FIFO: ejecución, cancelación y validación

## Compilar y ejecutar

Requiere Linux (probado en WSL2 Ubuntu 24.04), GCC, Make y Python 3 para las pruebas. Ejecutar desde la raíz del repositorio, donde existe `data/worldcities.csv`:

```bash
make
# Terminal 1:
./servidor_fifo
# Terminal 2:
./cliente_fifo
```

Elegir 1 y `Tokyo`, 2 y `Colombia`, o 3 y `CO`; elegir 4 para salir. También se admite entrada completa por pipe, manteniendo el servidor en otra terminal:

```bash
printf '1\nAtlantisCity\n1\nTokyo\n2\nColombia\n4\n' | ./cliente_fifo
```

Se esperan 0, 1 y 714 coincidencias, y salida 0 de ambos procesos. EOF de la entrada del cliente envía `OP_EXIT`, incluso si ocurre esperando el valor de una búsqueda. Una última línea sin salto es válida. Las entradas excesivas se descartan hasta su fin sin consumir la siguiente línea. El límite del criterio es de 127 bytes más NUL.

`make` compila solo FIFO, con C11, `_GNU_SOURCE`, `-Wall -Wextra -Wpedantic -Werror`. `make clean` elimina únicamente los ejecutables conocidos, no recursos IPC de una sesión activa.

## Sesión y recursos

Solo un servidor puede poseer la sesión: toma `flock(LOCK_EX | LOCK_NB)` sobre `/tmp/so_p1_fifo.lock` antes de crear/reutilizar las FIFOs. Un segundo servidor falla sin tocarlas. El archivo regular de bloqueo permanece tras salir: eliminarlo permitiría crear otro inode y romper la exclusión. Su descriptor se cierra para liberar el bloqueo.

El servidor crea las FIFOs con modo 0660 (modificado por la umask) o comprueba que las existentes sean FIFOs de su usuario. Rechaza archivos normales y enlaces simbólicos. Registra dispositivo/inode y comprueba los descriptores abiertos. Al cerrar, elimina ambas FIFOs verificadas, incluidas las reutilizadas; si una ruta fue reemplazada, informa el error y la conserva. Esto coordina los servidores de la aplicación, no clientes múltiples ni procesos ajenos que modifiquen deliberadamente los nombres durante una sesión.

El orden sigue siendo solicitudes y luego respuestas. El cliente abre solicitudes para escritura y respuestas para lectura, de forma bloqueante. El servidor abre ambos extremos con `O_NONBLOCK`; mientras no hay lector de respuestas, espera 50 ms mediante `ppoll` antes de reintentar. Así evita la ventana de carrera entre comprobar una señal y un `open()` bloqueante.

Una vez conectado, el servidor usa `read_full_cancelable()` / `write_full_cancelable()` con descriptores no bloqueantes y espera de disponibilidad mediante `ppoll`. `SIGINT`/`SIGTERM` se bloquean alrededor de la comprobación del indicador y se restauran atómicamente durante la espera. Los handlers solo marcan `volatile sig_atomic_t`; la cancelación retorna `-1/ECANCELED`. La limpieza ocurre en el flujo normal y los cierres se comprueban.

Los helpers genéricos `read_full()`/`write_full()` conservan el progreso y reintentan `EINTR`. EOF limpio devuelve 0, EOF a mitad de mensaje devuelve `EPROTO`; un mensaje parcial fallido no puede reenviarse como si fuera nuevo. `SIGPIPE` se ignora y `EPIPE` se trata como desconexión.

Un fallo de dataset o transporte termina la sesión con salida 1. `OP_EXIT` y señales manejadas terminan con salida 0 si la limpieza tiene éxito. No hay reconexión automática. El contrato de mensajes está en [README_DATASET_PROTOCOL.md](README_DATASET_PROTOCOL.md).

## Pruebas

```bash
make test
```

La suite usa dos binarios adicionales con rutas relativas de prueba, dentro de directorios temporales privados. Copia los mismos bytes del dataset a Linux para que la latencia de `/mnt/c` no se confunda con un bloqueo IPC. No toca las FIFOs de producción.

Los 15 escenarios de integración, algunos con varias combinaciones, cubren:

- Inexistente → Tokyo → Colombia → salida, por lotes y gradualmente.
- EOF, última línea sin salto, entradas inválidas o mayores que el buffer.
- CSV inválido/ausente y solicitudes inválidas, sin falso `END`.
- Peticiones fragmentadas, EOF parcial y desconexión al responder.
- FIFOs residuales, segundo servidor, archivos/enlaces ajenos y reemplazo de una ruta.
- `SIGINT` y `SIGTERM` antes de conectar, esperando solicitud, con petición parcial, durante lectura del CSV y con el canal de respuesta lleno.

Las pruebas C complementan la integración con inyección de errores de archivo, callbacks y señales que interrumpen transferencias conservando los bytes previos. `fork()`/`pipe()` se usan en ese arnés de pruebas; los ejecutables de la práctica siguen siendo procesos independientes.

## Observación del bloqueo

Con las aperturas bloqueantes de la versión inicial, iniciar solo el servidor lo detenía primero en `open()`, no en `read()`. La espera en `read()` solo podía observarse tras conectar un escritor.

En esta versión cancelable, observar las esperas reales en `ppoll`, tanto al conectar como al esperar datos/capacidad:

```bash
strace -e trace=openat,read,write,ppoll,rt_sigprocmask,close,unlink,flock ./servidor_fifo
# En otra terminal, con el PID observado:
ps -o pid,ppid,stat,wchan,cmd -p PID
cat /proc/PID/wchan
ls -l /proc/PID/fd
```

Al iniciar el cliente y enviar una consulta, `ppoll` indica disponibilidad y después ocurre la lectura/escritura. El nombre exacto de `wchan` depende del kernel; no debe prefijarse `pipe_read` como evidencia. Persona 4 debe capturar los resultados reales y completar el `LEEME` final junto con SHM.

## Límites

Una sesión atiende un solo cliente, en el mismo entorno Linux y con acceso a las FIFOs. El cliente requiere que el servidor haya creado las rutas; si no existen falla con `ENOENT`, y si son residuales puede esperar en `open()`. `SIGKILL` no permite limpieza: otro servidor podrá adquirir el bloqueo y reutilizar las FIFOs residuales.

La cancelación del recorrido del CSV se observa en el siguiente callback o ante una lectura interrumpida; no se promete una latencia máxima para recorridos sin coincidencias ni para E/S de almacenamiento no interrumpible. No se implementan SHM, supervisión de procesos ni clientes concurrentes en esta etapa.
