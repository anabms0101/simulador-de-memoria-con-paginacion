#define _POSIX_C_SOURCE 200809L
#include "simulacion.h"
#include <sys/select.h>
#include <unistd.h>

#define ANSI_ROJO    "\x1b[31m"
#define ANSI_VERDE   "\x1b[32m"
#define ANSI_AMARILLO "\x1b[33m"
#define ANSI_RESET   "\x1b[0m"

static const char* nombre_algoritmo(TipoAlgoritmo a) {
    switch (a) {
        case ALGO_FIFO: return "FIFO";
        case ALGO_SC:   return "Second Chance";
        case ALGO_LRU:  return "LRU";
        case ALGO_LFU:  return "LFU";
        case ALGO_OPT:  return "OPTIMO (Belady)";
    }
    return "?";
}

static const char* nombre_instruccion(const Instruccion* in, char* buf, size_t buf_sz) {
    switch (in->tipo) {
        case INST_NEW:    snprintf(buf, buf_sz, "new(%d,%d)", in->pid, in->param); break;
        case INST_USE:    snprintf(buf, buf_sz, "use(%d)", in->param); break;
        case INST_DELETE: snprintf(buf, buf_sz, "delete(%d)", in->param); break;
        case INST_KILL:   snprintf(buf, buf_sz, "kill(%d)", in->pid); break;
    }
    return buf;
}

/* Barra compacta de ocupacion de RAM: un caracter por marco fisico,
 * '.' si esta libre, o el ultimo digito del ptr dueño si esta ocupado. */
static void imprimir_barra_ram(const MMU* mmu) {
    printf("  RAM [100 marcos]: ");
    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
        if (mmu->ram[i] == NULL) {
            putchar('.');
        } else {
            putchar('0' + (mmu->ram[i]->ptr % 10));
        }
    }
    printf("\n");
}

/* Tabla de paginas (Anexo 2): PAGE ID, PID, PTR, LOADED, M-ADDR, D-ADDR, LOADED-T */
static void imprimir_tabla_paginas(const MMU* mmu) {
    printf("  %-8s %-6s %-6s %-8s %-8s %-8s %-10s\n",
           "PAGE-ID", "PID", "PTR", "LOADED", "M-ADDR", "D-ADDR", "LOADED-T");
    int mostradas = 0;
    for (int i = 0; i < mmu->total_simbolos; i++) {
        const PtrSimbolo* s = &mmu->simbolos[i];
        if (!s->activo) continue;
        for (int j = 0; j < s->num_paginas; j++) {
            const Pagina* p = s->paginas[j];
            if (!p) continue;
            char m_addr[16], d_addr[16];
            if (p->en_memoria_real) snprintf(m_addr, sizeof(m_addr), "%d", p->direccion_fisica);
            else snprintf(m_addr, sizeof(m_addr), "-");
            if (!p->en_memoria_real) snprintf(d_addr, sizeof(d_addr), "%d", p->direccion_virtual);
            else snprintf(d_addr, sizeof(d_addr), "-");

            printf("  %-8d %-6d %-6d %-8s %-8s %-8s %-10ld\n",
                   p->id, p->pid, p->ptr, p->en_memoria_real ? "RAM" : "DISCO",
                   m_addr, d_addr, p->tiempo_cargada);
            mostradas++;
        }
    }
    if (mostradas == 0) printf("  (sin paginas activas)\n");
}

static void imprimir_metricas(const MMU* mmu) {
    long ram_kb = mmu_ram_usada_bytes(mmu) / 1024;
    long vram_kb = mmu_vram_usada_bytes(mmu) / 1024;
    double ram_pct = 100.0 * mmu_ram_usada_bytes(mmu) / (TOTAL_MARCOS_RAM * TAMANIO_PAGINA);
    double thrash_pct = (mmu->reloj > 0) ? (100.0 * (double)mmu->tiempo_thrashing / (double)mmu->reloj) : 0.0;
    long frag = mmu_fragmentacion_bytes(mmu);

    printf("  Procesos activos : %d\n", mmu_procesos_activos(mmu));
    printf("  Reloj simulado   : %ld s\n", mmu->reloj);
    printf("  RAM usada        : %ld KB / 400 KB (%.1f%%)\n", ram_kb, ram_pct);
    printf("  V-RAM (disco)    : %ld KB\n", vram_kb);
    printf("  Fragmentacion int: %ld bytes\n", frag);

    if (thrash_pct > 50.0) {
        printf("  Thrashing        : " ANSI_ROJO "%ld s (%.1f%%)  *** ALERTA: >50%% ***" ANSI_RESET "\n",
               mmu->tiempo_thrashing, thrash_pct);
    } else {
        printf("  Thrashing        : %ld s (%.1f%%)\n", mmu->tiempo_thrashing, thrash_pct);
    }
}

static void imprimir_bloque_mmu(const char* etiqueta, const MMU* mmu) {
    printf("\n--- [%s] (algoritmo: %s) ---\n", etiqueta, nombre_algoritmo(mmu->algoritmo));
    imprimir_barra_ram(mmu);
    imprimir_tabla_paginas(mmu);
    imprimir_metricas(mmu);
}

/* Espera bloqueante a que el usuario presione ENTER (modo paso a paso).
 * Devuelve true si el usuario escribio 'q'/'Q' (quiere terminar). */
static bool esperar_enter(const char* mensaje) {
    printf("%s", mensaje);
    fflush(stdout);
    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return true; /* EOF: terminar */
    return (buf[0] == 'q' || buf[0] == 'Q');
}

/* En modo automatico: espera un breve instante revisando si llego
 * entrada por stdin (select con timeout). Si el usuario presiono ENTER,
 * se considera una pausa y se bloquea hasta el siguiente ENTER para
 * reanudar. Devuelve true si el usuario quiere terminar ('q'). */
static bool pausa_automatica(long milisegundos) {
    fd_set lectura;
    struct timeval tv;
    FD_ZERO(&lectura);
    FD_SET(STDIN_FILENO, &lectura);
    tv.tv_sec = milisegundos / 1000;
    tv.tv_usec = (milisegundos % 1000) * 1000;

    int listo = select(STDIN_FILENO + 1, &lectura, NULL, NULL, &tv);
    if (listo > 0 && FD_ISSET(STDIN_FILENO, &lectura)) {
        char buf[64];
        if (!fgets(buf, sizeof(buf), stdin)) return true;
        if (buf[0] == 'q' || buf[0] == 'Q') return true;
        return esperar_enter("  [PAUSADO] Presione ENTER para reanudar (o 'q' para salir)... ");
    }
    return false;
}

void ejecutar_simulacion_simultanea(const Instruccion* instrucciones, int total_instrucciones,
                                     TipoAlgoritmo algo_usuario, bool modo_automatico) {
    MMU mmu_usuario, mmu_opt;
    inicializar_mmu(&mmu_usuario, algo_usuario, total_instrucciones > 0 ? total_instrucciones : 1);
    inicializar_mmu(&mmu_opt, ALGO_OPT, total_instrucciones > 0 ? total_instrucciones : 1);

    /* Si stdin no es una terminal real (se redirigio un archivo, una
     * tuberia, /dev/null, o se esta corriendo en un script/calificador
     * automatico), NO tiene sentido esperar ENTER: un EOF inmediato se
     * leeria como "terminar" y la simulacion se cortaria tras el primer
     * paso. En ese caso se corre de corrido, sin pausas, imprimiendo
     * igualmente todo el dashboard de cada instruccion. */
    bool stdin_interactivo = isatty(STDIN_FILENO);
    bool con_pausas = stdin_interactivo;

    printf("\n=====================================================\n");
    printf(" SIMULACION SIMULTANEA: %s  vs.  OPTIMO\n", nombre_algoritmo(algo_usuario));
    printf(" Total de instrucciones: %d | Modo: %s%s\n", total_instrucciones,
           modo_automatico ? "automatico" : "paso a paso",
           con_pausas ? "" : " (stdin no interactivo: corriendo sin pausas)");
    printf("=====================================================\n");

    bool detener = false;
    for (int i = 0; i < total_instrucciones && !detener; i++) {
        const Instruccion* inst = &instrucciones[i];
        char desc[32];
        nombre_instruccion(inst, desc, sizeof(desc));

        printf("\n############ Instruccion %d/%d: %s ############\n", i + 1, total_instrucciones, desc);

        switch (inst->tipo) {
            case INST_NEW:
                mmu_new(&mmu_usuario, inst->pid, inst->param, instrucciones, total_instrucciones, i);
                mmu_new(&mmu_opt, inst->pid, inst->param, instrucciones, total_instrucciones, i);
                break;
            case INST_USE:
                mmu_use(&mmu_usuario, inst->param, instrucciones, total_instrucciones, i);
                mmu_use(&mmu_opt, inst->param, instrucciones, total_instrucciones, i);
                break;
            case INST_DELETE:
                mmu_delete(&mmu_usuario, inst->param);
                mmu_delete(&mmu_opt, inst->param);
                break;
            case INST_KILL:
                mmu_kill(&mmu_usuario, inst->pid);
                mmu_kill(&mmu_opt, inst->pid);
                break;
        }

        imprimir_bloque_mmu("ALGORITMO USUARIO", &mmu_usuario);
        imprimir_bloque_mmu("ALGORITMO OPTIMO", &mmu_opt);

        bool es_ultima = (i == total_instrucciones - 1);
        if (!es_ultima && con_pausas) {
            if (modo_automatico) {
                detener = pausa_automatica(600);
            } else {
                detener = esperar_enter("\nPresione ENTER para continuar (o 'q' para salir)... ");
            }
        }
    }

    printf("\n=====================================================\n");
    printf("                  RESULTADOS FINALES                 \n");
    printf("=====================================================\n");
    printf("[%s] Reloj total: %ld s | Thrashing: %ld s (%.1f%%)\n",
           nombre_algoritmo(algo_usuario), mmu_usuario.reloj, mmu_usuario.tiempo_thrashing,
           mmu_usuario.reloj > 0 ? 100.0 * (double)mmu_usuario.tiempo_thrashing / (double)mmu_usuario.reloj : 0.0);
    printf("[OPTIMO]  Reloj total: %ld s | Thrashing: %ld s (%.1f%%)\n",
           mmu_opt.reloj, mmu_opt.tiempo_thrashing,
           mmu_opt.reloj > 0 ? 100.0 * (double)mmu_opt.tiempo_thrashing / (double)mmu_opt.reloj : 0.0);

    if (mmu_usuario.reloj > mmu_opt.reloj) {
        printf(ANSI_AMARILLO "El algoritmo %s tardo %ld s mas que el optimo.\n" ANSI_RESET,
               nombre_algoritmo(algo_usuario), mmu_usuario.reloj - mmu_opt.reloj);
    } else if (mmu_usuario.reloj == mmu_opt.reloj) {
        printf(ANSI_VERDE "El algoritmo %s igualo el rendimiento del optimo.\n" ANSI_RESET,
               nombre_algoritmo(algo_usuario));
    }

    liberar_mmu(&mmu_usuario);
    liberar_mmu(&mmu_opt);
}
