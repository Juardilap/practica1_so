# Práctica 1: Sistemas Operativos (UNAL)
**Procesos, Llamadas al Sistema e IPC en Linux — World Cities**

Solución cliente-servidor en lenguaje C sobre Linux (WSL2 Ubuntu 24.04) para realizar búsquedas secuenciales sobre el dataset `worldcities.csv` (44.692 registros), implementando dos mecanismos de Comunicación entre Procesos (IPC):
1. **Versión A:** FIFOs / Tuberías Nombradas (`cliente_fifo` y `servidor_fifo`).
2. **Versión B:** Memoria Compartida System V y 3 Semáforos POSIX (`cliente_shm` y `servidor_shm`).

---

## Estructura del Repositorio

```text
practica1_so/
├── data/
│   └── worldcities.csv        # Dataset con 44.692 registros
├── include/                   # Cabeceras (.h) de protocolo, dataset e IPC
├── src/                       # Código fuente (.c) de clientes y servidores
├── scripts/                   # Scripts de observabilidad, pruebas y limpieza IPC
├── tests/                     # Harnesses de pruebas unitarias y de integración
├── GUIA_ORGANIZACION.md       # Guía de equipo, roles, 10 fases y sustentación
├── LEEME.md                   # Documentación técnica completa de entrega
├── Makefile                   # Reglas de compilación y limpieza
└── .gitignore                 # Filtro de binarios y temporales
```

---

## Modelo de Ramas (Git Flow)

- `main`: Rama de código estable para entrega y sustentación.
- `develop`: Rama de integración continua.
- Ramas de trabajo:
  - `feature/dataset-protocol`: Parser CSV, lectura secuencial y `protocol.h`.
  - `feature/fifo`: Implementación de cliente y servidor FIFO.
  - `feature/shared-memory`: Implementación de cliente y servidor SHM con 3 semáforos.
  - `feature/testing-docs`: Suites de pruebas, observabilidad y documentación.

Para más detalles sobre la distribución del trabajo, ver [GUIA_ORGANIZACION.md](GUIA_ORGANIZACION.md).
