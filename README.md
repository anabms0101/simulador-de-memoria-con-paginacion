# simulador-de-memoria-con-paginacion
##  Características Principales

- **Gestión de Memoria Física y Virtual:** 
  - RAM de **400 KB** dividida en **100 marcos** de **4 KB** cada uno.
  - V-RAM (Disco) para almacenamiento secundario de páginas desalojadas.
- **Políticas de Reemplazo de Páginas:**
  - FIFO (First-In, First-Out)
  - Second Chance (SC - Segunda Oportunidad)
  - LRU (Least Recently Used)
  - LFU (Least Frequently Used)
  - OPT (Algoritmo Óptimo Teórico)
- **Simulación Simultánea:** Ejecución paralela y sincronizada entre el algoritmo seleccionado por el usuario y el algoritmo Óptimo (OPT) usando la misma lista de instrucciones.
- **Control de Tiempos y Thrashing:** 
  - 1 segundo por instrucción (Hit / Acceso a RAM).
  - 5 segundos de penalización por fallo de página / acceso a disco.
  - Alerta visual de Thrashing cuando el tiempo de penalización supera umbrales críticos.
- **Módulo de Archivos:** Generación de instrucciones aleatorias mediante semillas numéricas (`seed`), exportación e importación de archivos de operaciones.

```bash
cd backend

gcc main.c mmu.c simulacion.c parser.c -o simulador

./simulador
