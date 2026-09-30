#include "simulacion.h"

// Estructuras auxiliares de procesos independientes para cada MMU para evitar cruce de punteros
typedef struct {
    Proceso procesos[50];
    int total_procesos;
} GestorProcesos;

// Buscar o crear un proceso de forma segura dentro del gestor de una MMU
static Proceso* obtener_o_crear_proceso(GestorProcesos* gestor, int pid) {
    for (int i = 0; i < gestor->total_procesos; i++) {
        if (gestor->procesos[i].pid == pid) {
            return &gestor->procesos[i];
        }
    }
    // Si no existe, lo creamos
    Proceso nuevo_proc;
    nuevo_proc.pid = pid;
    nuevo_proc.total_simbolos = 0;
    gestor->procesos[gestor->total_procesos] = nuevo_proc;
    gestor->total_procesos++;
    return &gestor->procesos[gestor->total_procesos - 1];
}

void ejecutar_simulacion_simultanea(Instruccion* instrucciones, int total_instrucciones, TipoAlgoritmo algo_usuario) {
    MMU mmu_usuario;
    MMU mmu_opt;

    // Inicializamos ambas MMUs de forma paralela
    inicializar_mmu(&mmu_usuario, algo_usuario);
    inicializar_mmu(&mmu_opt, ALGO_OPT);

    GestorProcesos gp_usuario = { .total_procesos = 0 };
    GestorProcesos gp_opt = { .total_procesos = 0 };

    printf("\n=== INICIANDO SIMULACIÓN SIMULTÁNEA (USUARIO vs OPT) ===\n");

    for (int i = 0; i < total_instrucciones; i++) {
        Instruccion inst = instrucciones[i];
        
        printf("\n--- Procesando instrucción %d/%d ---\n", i + 1, total_instrucciones);

        // 1. Ejecutar en la MMU del Algoritmo del Usuario
        Proceso* proc_u = obtener_o_crear_proceso(&gp_usuario, inst.pid);
        // 2. Ejecutar simultáneamente en la MMU del Algoritmo Óptimo (OPT)[cite: 1]
        Proceso* proc_o = obtener_o_crear_proceso(&gp_opt, inst.pid);

        switch (inst.tipo) {
            case INST_NEW:
                printf("[Usuario] ");
                mmu_new(&mmu_usuario, proc_u, inst.param);
                printf("[OPT] ");
                mmu_new(&mmu_opt, proc_o, inst.param);
                break;

            case INST_USE:
                printf("[Usuario] ");
                mmu_use(&mmu_usuario, proc_u, inst.param);
                printf("[OPT] ");
                mmu_use(&mmu_opt, proc_o, inst.param);
                break;

            case INST_DELETE:
                printf("[Usuario] ");
                mmu_delete(&mmu_usuario, proc_u, inst.param);
                printf("[OPT] ");
                mmu_delete(&mmu_opt, proc_o, inst.param);
                break;

            case INST_KILL:
                printf("[Usuario] ");
                mmu_kill(&mmu_usuario, proc_u);
                printf("[OPT] ");
                mmu_kill(&mmu_opt, proc_o);
                break;
        }

        // Simula el avance de 1 instrucción por segundo en el núcleo de procesamiento[cite: 1]
        mmu_usuario.reloj += 1;
        mmu_opt.reloj += 1;
    }

    printf("\n========================================\n");
    printf("           RESULTADOS FINALES           \n");
    printf("========================================\n");
    printf("[Algoritmo Usuario] Reloj Total: %ds | Thrashing: %ds\n", mmu_usuario.reloj, mmu_usuario.tiempo_thrashing);
    printf("[Algoritmo OPT]     Reloj Total: %ds | Thrashing: %ds\n", mmu_opt.reloj, mmu_opt.tiempo_thrashing);
}