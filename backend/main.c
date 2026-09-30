#include "mmu.h"
#include "simulacion.h"

int main() {
    // Definimos una pequeña lista de instrucciones de prueba
    Instruccion lista_pruebas[] = {
        { INST_NEW, 1, 8000 },  // PID 1 pide memoria (~2 páginas)[cite: 1]
        { INST_USE, 1, 1 },     // PID 1 usa el ptr 1[cite: 1]
        { INST_DELETE, 1, 1 },  // PID 1 borra el ptr 1[cite: 1]
        { INST_KILL, 1, 0 }     // PID 1 termina[cite: 1]
    };

    int total = sizeof(lista_pruebas) / sizeof(Instruccion);

    // Corrimos la simulación simultánea comparando FIFO contra OPT por ejemplo[cite: 1]
    ejecutar_simulacion_simultanea(lista_pruebas, total, ALGO_FIFO);

    return 0;
}