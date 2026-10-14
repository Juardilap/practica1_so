# Práctica 1: Sistemas Operativos (UNAL)

Sistema cliente-servidor en C/Linux para buscar secuencialmente en `data/worldcities.csv`: 44.691 registros y un encabezado. La base común y la versión FIFO están implementadas. La versión de memoria compartida System V con tres semáforos queda para Persona 3.

## Compilación y pruebas

```bash
make
make test
```

En terminales distintas, desde la raíz: `./servidor_fifo` y `./cliente_fifo`. Requisitos: Linux/WSL, GCC y Make; Python 3 para la integración. El Makefile no incluye objetivos SHM mientras sus fuentes no existan.

## Documentación

- [README_FIFO.md](README_FIFO.md): ejecución, señales, limpieza, pruebas y límites.
- [README_DATASET_PROTOCOL.md](README_DATASET_PROTOCOL.md): contrato definitivo, parser y base para SHM.
- [GUIA_EQUIPO.md](GUIA_EQUIPO.md): reparto de trabajo y revisión cruzada.
- [GUIA_ORGANIZACION.md](GUIA_ORGANIZACION.md): planificación general y sustentación.

Las guías contienen también entregables futuros: SHM, scripts de observación y `LEEME.md` final aún están pendientes.

## Integración

`codex/fifo-common-fixes` parte de `feature/fifo` en `0e4c24a` y conserva los cuatro commits de Persona 2. Sus correcciones se proponen a `develop` mediante PR para revisión de Persona 4. Tras aprobar e integrar, se incorporará `develop` mediante merge en `feature/shared-memory` y se volverá a ejecutar `make test`, preservando el trabajo de Persona 3. `main` no se actualiza en esta etapa.
