#ifndef MMU_H
#define MMU_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define TOTAL_MARCOS_RAM 100 // 400 KB / 4 KB = 100 páginas físicas en RAM
#define TAMANIO_PAGINA 4096  // 4 KB por página
#define MAX_SIMBOLOS 100     // Límite de punteros lógicos por proceso

// Algoritmos de reemplazo de páginas solicitados
typedef enum {
    ALGO_FIFO,
    ALGO_SC,
    ALGO_LRU,
    ALGO_LFU,
    ALGO_OPT
} TipoAlgoritmo;

// Estructura de una página[cite: 1]
typedef struct {
    char id[32];
    int pid;
    int ptr;
    bool en_memoria_real;    // true = RAM, false = V-RAM / Disco[cite: 1]
    int direccion_fisica;     // Marco en RAM (0 a 99)[cite: 1]
    
    // Metadatos para algoritmos de reemplazo
    int bit_uso;              // Usado para Second Chance (SC)
    int contador_accesos;     // Usado para LFU
    long timestamp;           // Usado para LRU
} Pagina;

// Tabla de símbolos para asociar un puntero (ptr) con su lista de páginas[cite: 1]
typedef struct {
    int ptr;
    Pagina* paginas[50];
    int num_paginas;
} PtrSimbolo;

// Estructura de un proceso[cite: 1]
typedef struct {
    int pid;
    PtrSimbolo simbolos[MAX_SIMBOLOS];
    int total_simbolos;
} Proceso;

// Estructura principal de la MMU y la computadora simulada[cite: 1]
typedef struct {
    Pagina* ram[TOTAL_MARCOS_RAM];
    TipoAlgoritmo algoritmo;
    int reloj;              // Reloj global en segundos (1s por instrucción / hit)[cite: 1]
    int tiempo_thrashing;   // Tiempo acumulado por fallos de página (+5s por página)[cite: 1]
    int puntero_reemplazo;  // Índice auxiliar para políticas de reemplazo
} MMU;

// Prototipos de funciones de la MMU
void inicializar_mmu(MMU* mmu, TipoAlgoritmo algo);
void mmu_new(MMU* mmu, Proceso* proc, int size);
void mmu_use(MMU* mmu, Proceso* proc, int ptr);
void mmu_delete(MMU* mmu, Proceso* proc, int ptr);
void mmu_kill(MMU* mmu, Proceso* proc);

#endif