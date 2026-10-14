# Práctica 1 — World Cities: FIFO y memoria compartida

## Alcance y fuente de requisitos

Implementación en C/Linux de dos clientes y dos servidores independientes. Se integran el módulo común de Persona 1, FIFO de Persona 2 y SHM de Persona 3 en `feature/testing-docs`, para validación de Persona 4 y revisión cruzada de Persona 2. Los merges conservan las contribuciones originales. `main` sigue reservado para una entrega final aprobada.

La referencia disponible fue **Guia_Practica1_SO_WorldCities.pdf**, documento de apoyo suministrado por el equipo. El enunciado oficial no formó parte de los archivos revisados; el equipo debe contrastarlo antes de aprobar la entrega. Las observaciones se registran sobre ejecuciones reales, separadas de los resultados esperados de la matriz.

## Dataset y búsquedas

`data/worldcities.csv` tiene 44.691 registros más el encabezado. Cada consulta abre el archivo, valida el encabezado y recorre las filas secuencialmente con `getline()`. La memoria es proporcional a la línea más larga y a los buffers IPC; no se carga el CSV completo ni se crea un índice en memoria.

El menú permite buscar por `city_ascii` (1), `country` (2), `iso2` (3) y salir (4). Compara con `strcasecmp()`. Resultados de regresión: Tokyo/tokyo = 1; Bogota = 2; Colombia/colombia/CO = 714; JP = 1253; AtlantisCity y PaisInventado = 0. El programa reproduce los datos incluidos; no verifica la exactitud geográfica o demográfica de sus valores.

El parser exige once columnas, comillas cerradas y delimitadores correctos. Acepta campos vacíos, comas dentro de comillas, comillas escapadas, LF/CRLF y última línea sin salto. Rechaza encabezados incorrectos, filas malformadas, NUL incrustados y respuestas que excedan el buffer; informa el número de línea cuando corresponde. No admite campos con saltos internos ni BOM. Comprueba `ferror()` y `fclose()` antes de confirmar éxito.

## Arquitectura y contrato

| Capa | Archivos | Responsabilidad |
| --- | --- | --- |
| Común | `protocol.h`, `dataset.c` | Solicitudes/respuestas, parser y búsqueda secuencial mediante callback |
| Entrada | `console_input.c` | Lector persistente basado en `read()`, opciones estrictas y EOF |
| FIFO | `cliente_fifo.c`, `servidor_fifo.c`, `ipc_utils.c` | Dos tuberías nombradas, transferencias completas y cancelación |
| SHM | `cliente_shm.c`, `servidor_shm.c`, `shm_utils.c` | Segmento System V, tres semáforos, publicación y vida de sesión |

Los programas de producción no crean al otro proceso mediante `fork()` ni heredan una tubería anónima. Se lanzan por separado. El arnés automatizado sí crea procesos y pipes para controlar entrada/salida; esto no forma parte del transporte de la práctica.

`Request` conserva `int operation` y `char value[128]`; `Response`, `int type` y `char text[512]`. En x86-64 ocupan 132 y 516 bytes. Son estructuras nativas para el mismo entorno, no serialización portable.

| Resultado | Mensajes | Acción |
| --- | --- | --- |
| Coincidencias | `ROW` repetido → `END` | Consumir todas las filas y el terminal |
| Sin coincidencias | `NOT_FOUND` → `END` | `NOT_FOUND` es informativo; seguir leyendo |
| Error de solicitud/dataset | Cero o más filas → `ERROR` | Cerrar con fallo, sin esperar `END`; filas previas son parciales |
| Salir o EOF de entrada | `OP_EXIT` → `BYE` | Cerrar normalmente |
| Fallo de transporte/callback | Cese inmediato | No intentar otro mensaje; liberar recursos |

`search_dataset()` devuelve coincidencias o `-1`. Comprueba todos los callbacks, incluidos los terminales. El detalle está en [README_DATASET_PROTOCOL.md](README_DATASET_PROTOCOL.md).

## FIFO

Recursos de producción: `/tmp/so_p1_req.fifo`, `/tmp/so_p1_resp.fifo` y `/tmp/so_p1_fifo.lock`. Se solicita modo 0660, sujeto a umask. El servidor toma un bloqueo exclusivo antes de crear/reutilizar FIFOs, verifica tipo, propietario e inode y elimina las dos rutas verificadas al cerrar. Un segundo servidor se rechaza sin alterar el primero. **No borrar el archivo de bloqueo**: su inode debe conservarse entre sesiones.

El cliente abre solicitudes para escritura y después respuestas para lectura. El servidor usa aperturas no bloqueantes y `ppoll` cancelable. Después de conectar, `read_full_cancelable()` y `write_full_cancelable()` conservan bytes parciales y reaccionan a señales sin la carrera entre comprobar el indicador y dormir. Los helpers genéricos reintentan `EINTR`. EOF parcial es error de protocolo. Se ignora `SIGPIPE` y se trata `EPIPE` como desconexión.

Un cliente FIFO por sesión: la exclusión entre clientes se implementa en SHM, no en FIFO. Ver [README_FIFO.md](README_FIFO.md).

## SHM y sincronización

Recursos de producción: clave `0x00503141` y `/tmp/so_p1_shm_client.lock`. El servidor crea el segmento con `IPC_CREAT | IPC_EXCL | 0660`; no reinicializa segmentos existentes. El cliente mantiene `flock` exclusivo durante toda su asociación. El archivo de bloqueo no se elimina.

**SharedBlock versión 2** añade marca, tamaño, versión, PID e identidad de inicio de cada proceso, e indicadores atómicos de inicialización y registro. El servidor publica inicialización con release después de crear los tres semáforos; el cliente usa acquire antes de leer los metadatos. Se comprueban propietario, tamaño exacto, versión, identidad e inicialización antes de usar semáforos. Los binarios SHM antiguos son incompatibles: recompilar ambos juntos.

1. Cliente copia `Request` y hace `sem_post(req_ready)`.
2. Servidor consume la solicitud y publica cada `Response` mediante `sem_post(resp_ready)`.
3. Cliente copia el mensaje a una variable local, confirma con `sem_post(resp_consumed)` y lo valida/procesa.
4. Servidor espera ese acuse antes de sobrescribir la respuesta. **Todos los mensajes se confirman**, incluidos `END`, `ERROR`, `BYE` y mensajes inválidos recibidos.

Los tres semáforos usan `pshared=1`. `shm_wait()` consume primero un acuse ya disponible (el cliente puede haberse desacoplado tras enviarlo) y, si debe esperar, usa `sem_clockwait(CLOCK_MONOTONIC)` en intervalos de 200 ms. En cada vencimiento comprueba señales, cierre y vida del otro proceso mediante `pidfd`, contrastando PID con el inicio registrado en `/proc/PID/stat`. Esto evita confundir reutilización de PID con un proceso vivo. El cliente también comprueba conexión mientras espera entrada.

La espera por inicialización dura como máximo cinco segundos una vez encontrado el segmento. Si todavía no existe, el cliente falla y se vuelve a iniciar tras arrancar el servidor. Los intervalos de 200 ms **no son un timeout de consulta**: una búsqueda larga continúa mientras el servidor siga vivo.

Al cerrar, el servidor publica `STATE_CLOSING` y marca `IPC_RMID` para impedir nuevas búsquedas de la clave. Espera hasta dos segundos para tomar el bloqueo de cliente y comprobar que es la única asociación. Solo entonces destruye los semáforos inicializados. Si quedan usuarios, omite `sem_destroy`, informa fallo y se desacopla; el kernel elimina el segmento marcado al desaparecer la última asociación. Los handlers solo marcan un `volatile sig_atomic_t`; la limpieza se ejecuta en el flujo normal.

## Compilar y ejecutar

Entorno comprobado: WSL2 Ubuntu 24.04, GCC 13.3, Linux x86-64. Se requieren GCC, Make, Python 3; para observaciones, `strace`, `lsof` y util-linux (`ipcs`, `ipcrm`). SHM necesita las interfaces Linux `pidfd_open` (kernel 5.3+) y glibc `sem_clockwait` (2.30+), además de atómicos enteros sin bloqueo. No hay implementación alternativa para plataformas que carezcan de ellas.

```bash
make clean
make
# Terminal 1, desde la raíz:
./servidor_fifo
# Terminal 2, desde la misma raíz:
./cliente_fifo
# En otra sesión, sustituir ambos nombres por servidor_shm y cliente_shm.
```

Make usa `-D_GNU_SOURCE -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g`; SHM enlaza con `-pthread`. `make clean` solo borra ejecutables conocidos, nunca recursos IPC.

Con el servidor correspondiente iniciado:

```bash
printf '1\nAtlantisCity\n1\nTokyo\n2\nColombia\n4\n' | ./cliente_shm
```

Ambos clientes admiten entradas graduales o por lotes, última línea sin salto y EOF convertido en `OP_EXIT`, incluso en el prompt de valor. Rechazan opciones inválidas y líneas excesivas, consumiéndolas completas antes de leer la siguiente. El criterio admite 127 bytes más NUL.

Salida 0: `OP_EXIT`, EOF tratado como salida o señal manejada, si la limpieza tiene éxito. Salida 1: dataset/protocolo/transporte/limpieza fallidos. Cuando un proceso recibe una señal manejada, su contraparte puede salir con fallo por desconexión. `SIGKILL` no permite ejecutar handlers.

## Matriz y resultados

```bash
make test
# Equivalente:
bash scripts/test_matrix.sh
```

Se ejecutan nueve búsquedas originales; pruebas C de CSV, callback, errores de lectura/cierre, transferencias fragmentadas, EOF parcial y `EINTR`; 15 métodos de integración FIFO, 18 SHM y uno de limpieza FIFO. Los métodos contienen varios subcasos. SHM cubre arranque durante inicialización, demora y timeout inicial, fallos de cada `sem_init`, segmentos incompatibles, segundo servidor/cliente, errores terminales, señales y muertes durante esperas y acuses.

Las pruebas usan directorios privados, rutas FIFO relativas y claves SHM aleatorias verificadas libres. `SHM_TEST_BUILD` habilita la clave de prueba por entorno; los ejecutables normales no la leen. La biblioteca de inyección solo se carga en los procesos de prueba. Los bytes del CSV son los mismos; copiar a `/tmp` Linux evita confundir la latencia de `/mnt/c` con bloqueos IPC.

Los registros de una compilación/matriz desde archivo Git limpio y las observaciones están en `docs/evidence/`; contienen el commit de fuentes, entorno y comandos. No se afirma ausencia total de errores ni cobertura de todos los intercalados posibles.

## Observaciones reales

El capturador ejecuta los mismos archivos fuente con rutas y clave privadas. No usa las FIFOs ni el segmento de una sesión de producción. La clave, PIDs y SHMID reales quedan registrados; los valores cambian en cada ejecución.

```bash
# Tras make test, en una copia limpia del commit que se vaya a observar:
python3 scripts/capture_evidence.py --commit SHA_DE_LAS_FUENTES --output /tmp/evidencia-nueva
# Para inspeccionar una sesión propia en curso:
python3 scripts/observe.py --server-pid PID_SERVIDOR --client-pid PID_CLIENTE
```

**FIFO:** el registro captura `ps`, `/proc/PID/status`, `/proc/PID/wchan`, descriptores, `lsof` y `strace` antes de conectar, conectado sin consulta y después de Tokyo. La implementación actual usa `ppoll`; la original con aperturas bloqueantes podía esperar primero en `open()`, antes de llegar a `read()`. El nombre de `wchan` depende del kernel y la captura puntual puede coincidir con una parada de `strace`.

**SHM:** se captura `ipcs -m -i SHMID` al crear, conectar y después de matar únicamente el servidor de la sesión privada con `SIGKILL`. El cliente detecta su muerte y se desacopla. Se conserva el registro del segmento residual y de su eliminación con `ipcrm`, ejecutada por el limpiador tras verificarlo.

Los resultados concretos y sus extractos se incorporan junto con las capturas; los párrafos anteriores describen el procedimiento, no sustituyen la evidencia.

## Recuperación y límites

Un servidor SHM rechaza cualquier segmento existente. Después de `SIGKILL` del servidor, revisar el SHMID exacto:

```bash
ipcs -m
ipcs -m -i SHMID
python3 scripts/cleanup_ipc.py --shmid SHMID
# Para FIFO residual:
python3 scripts/cleanup_ipc.py --fifo
```

El limpiador SHM toma el bloqueo de cliente, exige clave y UID esperados, cero asociaciones y creador muerto; vuelve a comprobar los metadatos antes de `ipcrm -m SHMID`. Para claves privadas añadir `--key CLAVE --client-lock RUTA`. No usar `ipcrm -a`, borrar bloqueos ni eliminar segmentos de otras sesiones. Un PID de creador reutilizado y vivo puede provocar un rechazo conservador; no se fuerza su eliminación.

El limpiador FIFO toma el bloqueo de servidor y verifica tipo, propietario, inode y descriptores abiertos. Si `/proc` impide comprobar la inactividad, **rechaza la limpieza y conserva los nodos**. Esa restricción se observó en WSL. Una sesión normal de servidor puede validar/reutilizar sus FIFOs residuales y eliminarlas al cerrar. La coordinación presupone procesos cooperativos del mismo usuario; no es una frontera de seguridad frente a modificaciones deliberadas de memoria o rutas.

No hay reconexión ni varios clientes por sesión. SHM rechaza al segundo cliente; FIFO requiere que el operador inicie uno solo. Un proceso detenido con `SIGSTOP` sigue vivo: su contraparte puede seguir esperando hasta que continúe o termine. Un terminal o pipe de salida sin consumidor puede bloquear `stdio`; la supervisión de IPC no convierte la salida estándar en no bloqueante.

La cancelación durante el CSV se observa en el siguiente callback o una lectura interrumpida. No se promete una latencia máxima en almacenamiento no interrumpible o recorridos sin coincidencias. Las pruebas de lectura bloqueada usan un FIFO privado como archivo únicamente para provocar de forma determinista la interrupción.

## Entrega y uso de IA

Persona 4 debe reproducir la matriz y revisar evidencias; Persona 2 realiza la revisión cruzada del PR `feature/testing-docs` → `develop`. Integrar con merge después de aprobar. Abrir `develop` → `main` solo para la entrega final, ejecutar nuevamente la matriz sobre su candidato exacto y fusionar tras aprobación. Si cambian fuentes, pruebas o entorno relevante, renovar las evidencias afectadas; no reutilizar como actuales capturas de otro código.

**Uso significativo de IA:** se utilizó asistencia de Antigravity y Codex para planificación, revisión, corrección de la base común/FIFO, robustecimiento de SHM, elaboración de pruebas, scripts y documentación. Codex ejecutó las pruebas y capturas registradas. La autoría inicial del equipo se conserva en Git; esta asistencia no sustituye la revisión humana ni la comprensión individual requerida en la sustentación.
