#define _POSIX_C_SOURCE 200809L
#include "mmu.h"
#include "parser.h"
#include "simulacion.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ===================== Utilidades de entrada ===================== */

static void limpiar_salto(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

static int leer_entero(const char* prompt, int minimo, int maximo) {
    char buf[128];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) { return minimo; } /* EOF: usa el minimo por defecto */
        limpiar_salto(buf);
        char* fin;
        long val = strtol(buf, &fin, 10);
        if (fin != buf && val >= minimo && val <= maximo) return (int)val;
        printf("  Entrada invalida, debe ser un entero entre %d y %d.\n", minimo, maximo);
    }
}

static void leer_linea(const char* prompt, char* out, size_t out_sz) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(out, (int)out_sz, stdin)) { out[0] = '\0'; return; }
    limpiar_salto(out);
}

static TipoAlgoritmo parsear_algoritmo(const char* s, bool* ok) {
    *ok = true;
    if (!strcasecmp(s, "fifo")) return ALGO_FIFO;
    if (!strcasecmp(s, "sc")) return ALGO_SC;
    if (!strcasecmp(s, "lru")) return ALGO_LRU;
    if (!strcasecmp(s, "lfu")) return ALGO_LFU;
    if (!strcasecmp(s, "opt")) return ALGO_OPT;
    *ok = false;
    return ALGO_FIFO;
}

static const char* nombre_algoritmo_corto(TipoAlgoritmo a) {
    switch (a) {
        case ALGO_FIFO: return "FIFO";
        case ALGO_SC:   return "SC";
        case ALGO_LRU:  return "LRU";
        case ALGO_LFU:  return "LFU";
        case ALGO_OPT:  return "OPT";
    }
    return "?";
}

/* ===================== Modo interactivo ===================== */

static void modo_interactivo(void) {
    printf("=====================================================\n");
    printf(" Simulador de Memoria con Paginacion (MMU) - TEC\n");
    printf("=====================================================\n\n");

    printf("Seleccione el algoritmo de reemplazo de paginas:\n");
    printf("  1) FIFO\n  2) Second Chance (SC)\n  3) LRU\n  4) LFU\n  5) OPTIMO (OPT)\n");
    int opc_algo = leer_entero("Opcion [1-5]: ", 1, 5);
    TipoAlgoritmo algo = (TipoAlgoritmo)(opc_algo - 1);

    printf("\n¿Como desea obtener las instrucciones?\n");
    printf("  1) Cargar desde un archivo existente (formato Anexo 1)\n");
    printf("  2) Generar una secuencia aleatoria nueva\n");
    int opc_fuente = leer_entero("Opcion [1-2]: ", 1, 2);

    Instruccion* instrucciones = NULL;
    int total = 0;

    if (opc_fuente == 1) {
        char ruta[256];
        while (1) {
            leer_linea("Ruta del archivo de operaciones: ", ruta, sizeof(ruta));
            if (parser_leer_archivo(ruta, &instrucciones, &total) == 0) break;
            printf("  No se pudo leer ese archivo, intente de nuevo.\n");
        }
    } else {
        unsigned int semilla = (unsigned int)leer_entero("Semilla (entero, para reproducibilidad) [0-2000000000]: ", 0, 2000000000);
        int P = leer_entero("Cantidad de procesos P [1-100]: ", 1, 100);
        int N = leer_entero("Cantidad total de operaciones N [1-100000]: ", 1, 100000);

        parser_generar_aleatorio(semilla, P, N, &instrucciones, &total);

        char ruta_salida[256];
        snprintf(ruta_salida, sizeof(ruta_salida), "operaciones_generadas_%u.txt", semilla);
        if (parser_escribir_archivo(ruta_salida, instrucciones, total) == 0) {
            printf("\nArchivo de operaciones generado y guardado en: %s\n", ruta_salida);
            printf("(Puede descargarlo/revisarlo antes de que inicie la simulacion.)\n");
        }
        printf("Presione ENTER para iniciar la simulacion...");
        fflush(stdout);
        char tmp[8];
        if (!fgets(tmp, sizeof(tmp), stdin)) { /* continuar de todas formas */ }
    }

    printf("\n¿Modo de ejecucion?\n");
    printf("  1) Paso a paso (ENTER avanza cada instruccion)\n");
    printf("  2) Automatico (avanza solo; ENTER pausa/reanuda, 'q' termina)\n");
    int opc_modo = leer_entero("Opcion [1-2]: ", 1, 2);
    bool automatico = (opc_modo == 2);

    ejecutar_simulacion_simultanea(instrucciones, total, algo, automatico);
    free(instrucciones);
}

/* ===================== Modo no interactivo (CLI) =====================
 *
 * Uso:
 *   ./simulador <algoritmo> --archivo <ruta> [--auto]
 *   ./simulador <algoritmo> --generar <semilla> <P> <N> [--auto]
 *
 * <algoritmo> es uno de: fifo | sc | lru | lfu | opt
 * --auto corre en modo automatico; por defecto corre paso a paso, pero
 * en un pipe sin terminal interactivo, esperar ENTER se salta solo al
 * llegar a EOF, asi que una corrida no interactiva del profesor con
 * "./simulador fifo --archivo ops.txt < /dev/null" tambien funciona.
 */
static int modo_cli(int argc, char** argv) {
    bool ok;
    TipoAlgoritmo algo = parsear_algoritmo(argv[1], &ok);
    if (!ok) {
        fprintf(stderr, "Algoritmo invalido '%s'. Use: fifo | sc | lru | lfu | opt\n", argv[1]);
        return 1;
    }

    bool automatico = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--auto")) automatico = true;
    }

    Instruccion* instrucciones = NULL;
    int total = 0;

    if (argc >= 4 && !strcmp(argv[2], "--archivo")) {
        if (parser_leer_archivo(argv[3], &instrucciones, &total) != 0) {
            fprintf(stderr, "No se pudo leer el archivo de operaciones '%s'.\n", argv[3]);
            return 1;
        }
    } else if (argc >= 6 && !strcmp(argv[2], "--generar")) {
        unsigned int semilla = (unsigned int)strtoul(argv[3], NULL, 10);
        int P = atoi(argv[4]);
        int N = atoi(argv[5]);
        if (P < 1 || N < 1) {
            fprintf(stderr, "P y N deben ser enteros positivos.\n");
            return 1;
        }
        parser_generar_aleatorio(semilla, P, N, &instrucciones, &total);

        char ruta_salida[256];
        snprintf(ruta_salida, sizeof(ruta_salida), "operaciones_generadas_%u.txt", semilla);
        if (parser_escribir_archivo(ruta_salida, instrucciones, total) == 0) {
            printf("Archivo de operaciones generado: %s\n", ruta_salida);
        }
    } else {
        fprintf(stderr,
                "Uso:\n"
                "  %s <fifo|sc|lru|lfu|opt> --archivo <ruta> [--auto]\n"
                "  %s <fifo|sc|lru|lfu|opt> --generar <semilla> <P> <N> [--auto]\n"
                "  %s                       (sin argumentos: modo interactivo)\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }

    printf("Algoritmo seleccionado: %s | Instrucciones: %d | Modo: %s\n",
           nombre_algoritmo_corto(algo), total, automatico ? "automatico" : "paso a paso");

    ejecutar_simulacion_simultanea(instrucciones, total, algo, automatico);
    free(instrucciones);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        return modo_cli(argc, argv);
    }
    modo_interactivo();
    return 0;
}
