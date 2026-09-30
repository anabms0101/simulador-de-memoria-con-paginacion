#ifndef SIMULACION_H
#define SIMULACION_H

#include "mmu.h"

// Tipos de instrucciones soportadas por el sistema
typedef enum {
    INST_NEW,
    INST_USE,
    INST_DELETE,
    INST_KILL
} TipoInstruccion;

// Estructura para modelar una instrucción individual
typedef struct {
    TipoInstruccion tipo;
    int pid;
    int param; // size si es 'new', ptr si es 'use' o 'delete'
} Instruccion;

// Prototipo de la simulación simultánea (Usuario vs OPT)
void ejecutar_simulacion_simultanea(Instruccion* instrucciones, int total_instrucciones, TipoAlgoritmo algo_usuario);

#endif