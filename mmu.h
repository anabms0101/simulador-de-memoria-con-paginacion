#ifndef MMU_H
#define MMU_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define TOTAL_MARCOS_RAM 100      /* 400 KB / 4 KB = 100 marcos fisicos */
#define TAMANIO_PAGINA 4096       /* 4 KB por pagina */
#define MAX_PAGINAS_POR_PTR 256   /* tope de seguridad por asignacion (256*4KB = 1MB) */
#define MAX_PROCESOS 512          /* tope de PIDs distintos que se pueden rastrear */

/* Algoritmos de reemplazo de paginas soportados */
typedef enum {
    ALGO_FIFO,
    ALGO_SC,
    ALGO_LRU,
    ALGO_LFU,
    ALGO_OPT
} TipoAlgoritmo;

/* ===================== Instrucciones ===================== */
/* Definidas aqui (no en simulacion.h) porque el algoritmo OPT necesita
 * "mirar hacia el futuro" dentro de la propia lista de instrucciones
 * para decidir que pagina reemplazar. */
typedef enum {
    INST_NEW,
    INST_USE,
    INST_DELETE,
    INST_KILL
} TipoInstruccion;

typedef struct {
    TipoInstruccion tipo;
    int pid;   /* usado por NEW y KILL; -1 si no aplica (USE/DELETE no llevan pid) */
    int param; /* size en bytes si es NEW; ptr si es USE o DELETE; no se usa en KILL */
} Instruccion;

/* ===================== Pagina ===================== */
typedef struct {
    int id;                  /* identificador secuencial, solo para mostrar */
    int pid;
    int ptr;
    bool en_memoria_real;    /* true = RAM, false = V-RAM / disco */
    int direccion_fisica;    /* marco en RAM (0..99), -1 si esta en disco */
    int direccion_virtual;   /* "direccion" en V-RAM/disco, -1 si nunca ha salido de RAM */

    /* Metadatos para los algoritmos de reemplazo */
    int bit_uso;             /* Second Chance */
    long contador_accesos;   /* LFU */
    long timestamp_acceso;   /* LRU: se actualiza en cada hit */
    long tiempo_cargada;     /* FIFO y columna LOADED-T: se fija al entrar a RAM, no se actualiza en hits */
} Pagina;

/* ===================== Tabla de simbolos (mapa ptr -> paginas) =====================
 * Vive DENTRO de la MMU (no del proceso): el enunciado pide un mapa de
 * memoria global que relacione cada ptr con su lista de paginas, y el
 * formato de archivo de operaciones no indica el PID en use()/delete(),
 * asi que la unica forma de resolverlos es buscando el ptr en un mapa
 * global. El contador de ptr tambien es global y nunca se reutiliza,
 * ni siquiera despues de un delete. */
typedef struct {
    int ptr;
    int pid;
    long size_bytes;         /* tamanio originalmente solicitado, para fragmentacion interna */
    Pagina* paginas[MAX_PAGINAS_POR_PTR];
    int num_paginas;
    bool activo;             /* false una vez que se borra (el ptr nunca se reutiliza) */
} PtrSimbolo;

/* ===================== MMU ===================== */
typedef struct {
    Pagina* ram[TOTAL_MARCOS_RAM];
    TipoAlgoritmo algoritmo;

    PtrSimbolo* simbolos;     /* arreglo reservado dinamicamente (mapa global ptr->paginas) */
    int capacidad_simbolos;
    int total_simbolos;       /* entradas usadas (activas o no) */
    int siguiente_ptr;        /* contador global de ptr, nunca se repite */
    int siguiente_pagina_id;  /* id incremental de pagina, solo para mostrar */

    long reloj;               /* reloj global en segundos simulados */
    long tiempo_thrashing;    /* segundos acumulados por fallos de pagina */

    int puntero_reemplazo;    /* indice circular auxiliar (FIFO/SC) */
    long reloj_logico;        /* contador monotono interno para timestamps de LRU/FIFO */
    long siguiente_direccion_virtual; /* contador de direcciones de disco, solo para mostrar */

    int pids_vistos[MAX_PROCESOS];
    bool pid_activo[MAX_PROCESOS];
    int total_pids_vistos;
} MMU;

/* ===================== API de la MMU ===================== */

/* capacidad_simbolos debe ser >= a la cantidad de instrucciones NEW que se
 * vayan a ejecutar (p. ej. el total de instrucciones de la simulacion). */
void inicializar_mmu(MMU* mmu, TipoAlgoritmo algo, int capacidad_simbolos);
void liberar_mmu(MMU* mmu);

/* 'futuras'/'total_futuras'/'indice_actual' son la lista completa de
 * instrucciones y la posicion actual dentro de ella; solo las usa el
 * algoritmo OPT para decidir la victima optima. Las demas MMUs pueden
 * recibir NULL/0/0 sin problema si su algoritmo no es OPT. */
int  mmu_new(MMU* mmu, int pid, int size,
             const Instruccion* futuras, int total_futuras, int indice_actual);
void mmu_use(MMU* mmu, int ptr,
             const Instruccion* futuras, int total_futuras, int indice_actual);
void mmu_delete(MMU* mmu, int ptr);
void mmu_kill(MMU* mmu, int pid);

/* Consultas usadas por el mapa de simbolos */
bool mmu_ptr_existe(MMU* mmu, int ptr);

/* ===================== Metricas ===================== */
long mmu_ram_usada_bytes(const MMU* mmu);
long mmu_vram_usada_bytes(const MMU* mmu);
long mmu_fragmentacion_bytes(const MMU* mmu);
int  mmu_procesos_activos(const MMU* mmu);
int  mmu_paginas_cargadas(const MMU* mmu);
int  mmu_paginas_descargadas(const MMU* mmu);

#endif /* MMU_H */
