# Práctica 1: Sistemas Operativos (UNAL)

Sistema cliente-servidor en C/Linux para buscar secuencialmente en `data/worldcities.csv`: 44.691 registros y un encabezado. Incluye dos transportes: FIFO y memoria compartida System V con tres semáforos POSIX. Ambos usan el mismo parser, contrato y lector de entrada.

## Compilación y pruebas

```bash
make
make test
```

En terminales distintas, desde la raíz: `./servidor_fifo` y `./cliente_fifo`, o `./servidor_shm` y `./cliente_shm`. Iniciar el servidor primero. Requisitos comprobados: WSL2 Ubuntu 24.04, GCC 13, Make y Python 3. SHM requiere `pidfd_open`, `sem_clockwait` y atómicos C11 sin bloqueo; recompilar juntos ambos ejecutables (ABI SHM versión 2).

## Documentación

- [README_FIFO.md](README_FIFO.md): ejecución, señales, limpieza, pruebas y límites.
- [LEEME.md](LEEME.md): arquitectura, SHM, recuperación, observaciones reales y entrega.
- [README_DATASET_PROTOCOL.md](README_DATASET_PROTOCOL.md): contrato definitivo y parser.
- [GUIA_EQUIPO.md](GUIA_EQUIPO.md): reparto de trabajo y revisión cruzada.
- [GUIA_ORGANIZACION.md](GUIA_ORGANIZACION.md): planificación general y sustentación.

`scripts/test_matrix.sh` ejecuta `make test`; `scripts/observe.py` inspecciona procesos y `scripts/cleanup_ipc.py` rechaza recursos activos o ajenos antes de limpiar residuos verificados.

## Integración

`feature/testing-docs` incorpora mediante merge `develop` (`3af12cb`, PR #1) y el trabajo de Persona 3 (`6ae297f`). Conserva los commits originales y añade las correcciones SHM, pruebas y documentación. Su PR hacia `develop` requiere validación integral de Persona 4 y revisión cruzada de Persona 2. Después de aprobarlo, usar merge; abrir `develop` → `main` solo para la entrega final aprobada y probar su candidato exacto. No usar squash, rebase ni force-push para reemplazar contribuciones.
