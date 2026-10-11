#include "parser.h"
#include <ctype.h>

/* ===================== Lectura de archivo ===================== */

typedef struct {
    Instruccion* items;
    int cuenta;
    int capacidad;
} ListaInst;

static void lista_init(ListaInst* l) {
    l->items = NULL;
    l->cuenta = 0;
    l->capacidad = 0;
}

static void lista_agregar(ListaInst* l, Instruccion inst) {
    if (l->cuenta == l->capacidad) {
        l->capacidad = l->capacidad ? l->capacidad * 2 : 64;
        l->items = (Instruccion*)realloc(l->items, (size_t)l->capacidad * sizeof(Instruccion));
    }
    l->items[l->cuenta++] = inst;
}

/* Quita espacios, tabs y retornos de carro de una linea, en el mismo
 * buffer (para tolerar "new( 1 , 500 )" o finales de linea CRLF). */
static void comprimir_linea(char* linea) {
    char* escritura = linea;
    for (char* lectura = linea; *lectura; lectura++) {
        if (!isspace((unsigned char)*lectura)) {
            *escritura++ = *lectura;
        }
    }
    *escritura = '\0';
}

int parser_leer_archivo(const char* ruta, Instruccion** out_instrucciones, int* out_total) {
    FILE* f = fopen(ruta, "r");
    if (!f) {
        fprintf(stderr, "[ERROR] No se pudo abrir el archivo de operaciones '%s'.\n", ruta);
        return -1;
    }

    ListaInst lista;
    lista_init(&lista);

    char linea[512];
    int num_linea = 0;
    while (fgets(linea, sizeof(linea), f)) {
        num_linea++;
        comprimir_linea(linea);
        if (linea[0] == '\0' || linea[0] == '#') continue;

        int a, b;
        Instruccion inst;
        if (sscanf(linea, "new(%d,%d)", &a, &b) == 2) {
            inst.tipo = INST_NEW; inst.pid = a; inst.param = b;
        } else if (sscanf(linea, "use(%d)", &a) == 1) {
            inst.tipo = INST_USE; inst.pid = -1; inst.param = a;
        } else if (sscanf(linea, "delete(%d)", &a) == 1) {
            inst.tipo = INST_DELETE; inst.pid = -1; inst.param = a;
        } else if (sscanf(linea, "kill(%d)", &a) == 1) {
            inst.tipo = INST_KILL; inst.pid = a; inst.param = 0;
        } else {
            fprintf(stderr, "[ADVERTENCIA] Linea %d ignorada (formato no reconocido): %s\n", num_linea, linea);
            continue;
        }
        lista_agregar(&lista, inst);
    }
    fclose(f);

    if (lista.cuenta == 0) {
        fprintf(stderr, "[ERROR] El archivo '%s' no contenia ninguna instruccion valida.\n", ruta);
        free(lista.items);
        return -1;
    }

    *out_instrucciones = lista.items;
    *out_total = lista.cuenta;
    return 0;
}

/* ===================== Escritura de archivo ===================== */

int parser_escribir_archivo(const char* ruta, const Instruccion* instrucciones, int total) {
    FILE* f = fopen(ruta, "w");
    if (!f) {
        fprintf(stderr, "[ERROR] No se pudo crear el archivo '%s'.\n", ruta);
        return -1;
    }
    for (int i = 0; i < total; i++) {
        const Instruccion* in = &instrucciones[i];
        switch (in->tipo) {
            case INST_NEW:    fprintf(f, "new(%d,%d)\n", in->pid, in->param); break;
            case INST_USE:    fprintf(f, "use(%d)\n", in->param); break;
            case INST_DELETE: fprintf(f, "delete(%d)\n", in->param); break;
            case INST_KILL:   fprintf(f, "kill(%d)\n", in->pid); break;
        }
    }
    fclose(f);
    return 0;
}

/* ===================== Generacion aleatoria ===================== */

/* Para mantener el generador simple y correcto, cada proceso se genera
 * de forma completa y secuencial (todas sus operaciones seguidas,
 * terminando en su propio kill) antes de pasar al siguiente proceso.
 * El enunciado PERMITE intercalar operaciones de distintos procesos,
 * pero no lo exige; generarlos secuencialmente evita por completo el
 * riesgo de que una operacion de un proceso quede despues de que otro
 * proceso ya fue "matado" sin tener que rastrear ownership cruzado, y
 * sigue produciendo un archivo 100% valido segun las reglas dadas. */
void parser_generar_aleatorio(unsigned int semilla, int P, int N,
                               Instruccion** out_instrucciones, int* out_total) {
    srand(semilla);

    ListaInst lista;
    lista_init(&lista);

    int* pool = (int*)malloc(sizeof(int) * (size_t)(N > 0 ? N : 1));
    int pool_n;

    int restante = N;
    for (int pid = 1; pid <= P && restante > 0; pid++) {
        /* presupuesto de operaciones para este proceso, incluyendo su kill final */
        int max_posible = restante;
        int presupuesto = 1 + (max_posible > 1 ? rand() % max_posible : 0);
        if (presupuesto > restante) presupuesto = restante;
        if (presupuesto < 1) presupuesto = 1;

        pool_n = 0;
        int usadas = 0;

        /* reservamos 1 operacion del presupuesto para el kill final */
        while (usadas < presupuesto - 1) {
            int opcion = rand() % 100;
            Instruccion inst;

            if (pool_n == 0 || opcion < 50) {
                /* new(): tamanio aleatorio entre 1B y ~20KB (~5 paginas) */
                int size = 1 + rand() % 20000;
                inst.tipo = INST_NEW; inst.pid = pid; inst.param = size;
                lista_agregar(&lista, inst);
                /* el ptr real lo asigna la MMU en orden de aparicion; como
                 * generamos la secuencia final tal cual se va a ejecutar,
                 * podemos predecirlo contando cuantos new() van antes. */
                pool[pool_n++] = lista.cuenta; /* marcador temporal, se resuelve abajo */
            } else if (opcion < 85) {
                /* use() sobre un ptr propio activo */
                int elegido = pool[rand() % pool_n];
                inst.tipo = INST_USE; inst.pid = -1; inst.param = elegido;
                lista_agregar(&lista, inst);
            } else {
                /* delete() sobre un ptr propio activo */
                int sel = rand() % pool_n;
                int elegido = pool[sel];
                inst.tipo = INST_DELETE; inst.pid = -1; inst.param = elegido;
                lista_agregar(&lista, inst);
                pool[sel] = pool[--pool_n];
            }
            usadas++;
        }

        Instruccion kill_inst;
        kill_inst.tipo = INST_KILL; kill_inst.pid = pid; kill_inst.param = 0;
        lista_agregar(&lista, kill_inst);
        usadas++;

        restante -= usadas;
    }
    free(pool);

    /* Resolver los marcadores temporales de ptr: el ptr real de cada
     * new() es simplemente 1,2,3... en el orden en que aparecen los new()
     * dentro de la lista final (asi es exactamente como mmu_new() los
     * asigna en tiempo de ejecucion), y cada use()/delete() debe apuntar
     * al numero de ptr (no al indice de instruccion) del new() que lo
     * genero. */
    int* indice_a_ptr = (int*)calloc((size_t)lista.cuenta + 1, sizeof(int));
    int contador_ptr = 0;
    for (int i = 0; i < lista.cuenta; i++) {
        if (lista.items[i].tipo == INST_NEW) {
            contador_ptr++;
            indice_a_ptr[i + 1] = contador_ptr; /* +1 porque guardamos "lista.cuenta" como marcador (1-indexado) */
        }
    }
    for (int i = 0; i < lista.cuenta; i++) {
        if (lista.items[i].tipo == INST_USE || lista.items[i].tipo == INST_DELETE) {
            int marcador = lista.items[i].param;
            lista.items[i].param = indice_a_ptr[marcador];
        }
    }
    free(indice_a_ptr);

    *out_instrucciones = lista.items;
    *out_total = lista.cuenta;
}
