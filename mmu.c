#include "mmu.h"
#include <limits.h>

/* ===================== Inicializacion ===================== */

void inicializar_mmu(MMU* mmu, TipoAlgoritmo algo, int capacidad_simbolos) {
    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) mmu->ram[i] = NULL;

    if (capacidad_simbolos < 1) capacidad_simbolos = 1;
    mmu->simbolos = (PtrSimbolo*)calloc((size_t)capacidad_simbolos, sizeof(PtrSimbolo));
    mmu->capacidad_simbolos = capacidad_simbolos;
    mmu->total_simbolos = 0;
    mmu->siguiente_ptr = 0;
    mmu->siguiente_pagina_id = 0;

    mmu->algoritmo = algo;
    mmu->reloj = 0;
    mmu->tiempo_thrashing = 0;
    mmu->puntero_reemplazo = 0;
    mmu->reloj_logico = 0;
    mmu->siguiente_direccion_virtual = 0;

    mmu->total_pids_vistos = 0;
    for (int i = 0; i < MAX_PROCESOS; i++) mmu->pid_activo[i] = false;
}

void liberar_mmu(MMU* mmu) {
    for (int i = 0; i < mmu->total_simbolos; i++) {
        PtrSimbolo* s = &mmu->simbolos[i];
        for (int j = 0; j < s->num_paginas; j++) free(s->paginas[j]);
    }
    free(mmu->simbolos);
    mmu->simbolos = NULL;
}

/* ===================== Utilidades internas ===================== */

static int buscar_indice_simbolo(MMU* mmu, int ptr) {
    for (int i = 0; i < mmu->total_simbolos; i++) {
        if (mmu->simbolos[i].activo && mmu->simbolos[i].ptr == ptr) return i;
    }
    return -1;
}

bool mmu_ptr_existe(MMU* mmu, int ptr) {
    return buscar_indice_simbolo(mmu, ptr) != -1;
}

static int indice_pid(MMU* mmu, int pid, bool crear) {
    for (int i = 0; i < mmu->total_pids_vistos; i++) {
        if (mmu->pids_vistos[i] == pid) return i;
    }
    if (!crear || mmu->total_pids_vistos >= MAX_PROCESOS) return -1;
    mmu->pids_vistos[mmu->total_pids_vistos] = pid;
    mmu->pid_activo[mmu->total_pids_vistos] = true;
    return mmu->total_pids_vistos++;
}

static int buscar_marco_libre(MMU* mmu) {
    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
        if (mmu->ram[i] == NULL) return i;
    }
    return -1;
}

/* Distancia (en instrucciones futuras) hasta el proximo use() de este
 * ptr. Si no se vuelve a usar antes de que termine la simulacion,
 * devuelve LONG_MAX (candidato ideal para expulsar primero). Usado solo
 * por el algoritmo OPT. */
static long distancia_siguiente_uso(int ptr, const Instruccion* futuras, int total_futuras, int desde) {
    for (int j = desde; j < total_futuras; j++) {
        if (futuras[j].tipo == INST_USE && futuras[j].param == ptr) {
            return (long)(j - desde);
        }
    }
    return LONG_MAX;
}

/* Selecciona el marco (0..99) de la pagina victima segun el algoritmo
 * configurado en la MMU. Asume que la RAM esta llena (no hay marcos
 * libres); el caller ya lo verifico. */
static int seleccionar_victima(MMU* mmu, const Instruccion* futuras, int total_futuras, int indice_actual) {
    if (mmu->algoritmo == ALGO_OPT) {
        int victima = -1;
        long mejor_distancia = -1;
        for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
            Pagina* p = mmu->ram[i];
            if (!p) continue;
            long d = distancia_siguiente_uso(p->ptr, futuras, total_futuras, indice_actual + 1);
            if (d > mejor_distancia) { mejor_distancia = d; victima = i; }
        }
        return victima;
    }

    if (mmu->algoritmo == ALGO_LRU) {
        int victima = -1;
        long peor = LONG_MAX;
        for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
            Pagina* p = mmu->ram[i];
            if (!p) continue;
            if (p->timestamp_acceso < peor) { peor = p->timestamp_acceso; victima = i; }
        }
        return victima;
    }

    if (mmu->algoritmo == ALGO_LFU) {
        int victima = -1;
        long peor = LONG_MAX;
        for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
            Pagina* p = mmu->ram[i];
            if (!p) continue;
            /* empate: la mas antigua en RAM pierde primero */
            if (p->contador_accesos < peor ||
                (p->contador_accesos == peor && victima != -1 && p->tiempo_cargada < mmu->ram[victima]->tiempo_cargada)) {
                peor = p->contador_accesos;
                victima = i;
            }
        }
        return victima;
    }

    if (mmu->algoritmo == ALGO_SC) {
        int idx = mmu->puntero_reemplazo;
        while (true) {
            Pagina* p = mmu->ram[idx];
            if (p != NULL) {
                if (p->bit_uso == 1) {
                    p->bit_uso = 0; /* segunda oportunidad */
                } else {
                    mmu->puntero_reemplazo = (idx + 1) % TOTAL_MARCOS_RAM;
                    return idx;
                }
            }
            idx = (idx + 1) % TOTAL_MARCOS_RAM;
        }
    }

    /* ALGO_FIFO: la pagina que lleva mas tiempo CARGADA (no la de acceso
     * mas antiguo: FIFO se basa en el momento de carga, no de uso). */
    int victima = -1;
    long peor = LONG_MAX;
    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
        Pagina* p = mmu->ram[i];
        if (!p) continue;
        if (p->tiempo_cargada < peor) { peor = p->tiempo_cargada; victima = i; }
    }
    return victima;
}

/* Coloca 'pag' en un marco (expulsando una victima a disco si hace
 * falta) y actualiza sus metadatos como recien cargada. */
static void cargar_pagina_en_ram(MMU* mmu, Pagina* pag,
                                  const Instruccion* futuras, int total_futuras, int indice_actual) {
    int marco = buscar_marco_libre(mmu);
    if (marco == -1) {
        marco = seleccionar_victima(mmu, futuras, total_futuras, indice_actual);
        Pagina* victima = mmu->ram[marco];
        if (victima) {
            victima->en_memoria_real = false;
            victima->direccion_fisica = -1;
            victima->direccion_virtual = (int)(++mmu->siguiente_direccion_virtual);
        }
    }
    pag->direccion_fisica = marco;
    pag->en_memoria_real = true;
    pag->bit_uso = 1;
    pag->contador_accesos = (pag->contador_accesos > 0) ? pag->contador_accesos : 1;
    pag->timestamp_acceso = ++mmu->reloj_logico;
    pag->tiempo_cargada = pag->timestamp_acceso;
    mmu->ram[marco] = pag;
}

/* ===================== Operaciones ===================== */

int mmu_new(MMU* mmu, int pid, int size,
            const Instruccion* futuras, int total_futuras, int indice_actual) {
    if (mmu->total_simbolos >= mmu->capacidad_simbolos) {
        fprintf(stderr, "[ERROR] Capacidad de simbolos agotada en la MMU.\n");
        return -1;
    }
    if (size < 1) size = 1;

    indice_pid(mmu, pid, true);

    int num_paginas = (size + TAMANIO_PAGINA - 1) / TAMANIO_PAGINA;
    if (num_paginas > MAX_PAGINAS_POR_PTR) num_paginas = MAX_PAGINAS_POR_PTR;

    int nuevo_ptr = ++mmu->siguiente_ptr; /* global, nunca se repite */

    PtrSimbolo* simbolo = &mmu->simbolos[mmu->total_simbolos++];
    simbolo->ptr = nuevo_ptr;
    simbolo->pid = pid;
    simbolo->size_bytes = size;
    simbolo->num_paginas = 0;
    simbolo->activo = true;

    for (int i = 0; i < num_paginas; i++) {
        Pagina* nueva = (Pagina*)malloc(sizeof(Pagina));
        nueva->id = ++mmu->siguiente_pagina_id;
        nueva->pid = pid;
        nueva->ptr = nuevo_ptr;
        nueva->direccion_virtual = -1;
        nueva->bit_uso = 1;
        nueva->contador_accesos = 1;

        /* "Siempre considere paginas nuevas como un fallo": cada pagina
         * de un new() cuenta como fallo de pagina (+5s, +5s thrashing),
         * sin importar si hay marco libre o hay que expulsar a alguien. */
        mmu->reloj += 5;
        mmu->tiempo_thrashing += 5;

        cargar_pagina_en_ram(mmu, nueva, futuras, total_futuras, indice_actual);
        simbolo->paginas[simbolo->num_paginas++] = nueva;
    }

    return nuevo_ptr;
}

void mmu_use(MMU* mmu, int ptr,
             const Instruccion* futuras, int total_futuras, int indice_actual) {
    int idx = buscar_indice_simbolo(mmu, ptr);
    if (idx == -1) {
        fprintf(stderr, "[ERROR] use(%d): el ptr no existe (o ya fue borrado).\n", ptr);
        return;
    }
    PtrSimbolo* simbolo = &mmu->simbolos[idx];

    for (int i = 0; i < simbolo->num_paginas; i++) {
        Pagina* pag = simbolo->paginas[i];
        if (pag->en_memoria_real) {
            /* HIT: +1s */
            mmu->reloj += 1;
            pag->bit_uso = 1;
            pag->contador_accesos++;
            pag->timestamp_acceso = ++mmu->reloj_logico;
        } else {
            /* FALLO: +5s, tambien cuenta para thrashing */
            mmu->reloj += 5;
            mmu->tiempo_thrashing += 5;
            pag->contador_accesos++;
            cargar_pagina_en_ram(mmu, pag, futuras, total_futuras, indice_actual);
        }
    }
}

void mmu_delete(MMU* mmu, int ptr) {
    int idx = buscar_indice_simbolo(mmu, ptr);
    if (idx == -1) {
        fprintf(stderr, "[ERROR] delete(%d): el ptr no existe (o ya fue borrado).\n", ptr);
        return;
    }
    PtrSimbolo* simbolo = &mmu->simbolos[idx];

    for (int i = 0; i < simbolo->num_paginas; i++) {
        Pagina* pag = simbolo->paginas[i];
        if (pag->en_memoria_real && pag->direccion_fisica >= 0) {
            mmu->ram[pag->direccion_fisica] = NULL;
        }
        free(pag);
        simbolo->paginas[i] = NULL;
    }
    simbolo->num_paginas = 0;
    simbolo->activo = false; /* el ptr queda invalido para siempre: no se reutiliza */
}

void mmu_kill(MMU* mmu, int pid) {
    for (int i = 0; i < mmu->total_simbolos; i++) {
        PtrSimbolo* s = &mmu->simbolos[i];
        if (s->activo && s->pid == pid) {
            mmu_delete(mmu, s->ptr);
        }
    }
    int idx = indice_pid(mmu, pid, false);
    if (idx != -1) mmu->pid_activo[idx] = false;
}

/* ===================== Metricas ===================== */

long mmu_ram_usada_bytes(const MMU* mmu) {
    long marcos_usados = 0;
    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) if (mmu->ram[i]) marcos_usados++;
    return marcos_usados * TAMANIO_PAGINA;
}

long mmu_vram_usada_bytes(const MMU* mmu) {
    long total = 0;
    for (int i = 0; i < mmu->total_simbolos; i++) {
        const PtrSimbolo* s = &mmu->simbolos[i];
        if (!s->activo) continue;
        for (int j = 0; j < s->num_paginas; j++) {
            if (!s->paginas[j]->en_memoria_real) total += TAMANIO_PAGINA;
        }
    }
    return total;
}

long mmu_fragmentacion_bytes(const MMU* mmu) {
    long total = 0;
    for (int i = 0; i < mmu->total_simbolos; i++) {
        const PtrSimbolo* s = &mmu->simbolos[i];
        if (!s->activo) continue;
        long reservado = (long)s->num_paginas * TAMANIO_PAGINA;
        long desperdicio = reservado - s->size_bytes;
        if (desperdicio > 0) total += desperdicio;
    }
    return total;
}

int mmu_procesos_activos(const MMU* mmu) {
    int cuenta = 0;
    for (int i = 0; i < mmu->total_pids_vistos; i++) if (mmu->pid_activo[i]) cuenta++;
    return cuenta;
}

int mmu_paginas_cargadas(const MMU* mmu) {
    int cuenta = 0;
    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) if (mmu->ram[i]) cuenta++;
    return cuenta;
}

int mmu_paginas_descargadas(const MMU* mmu) {
    int cuenta = 0;
    for (int i = 0; i < mmu->total_simbolos; i++) {
        const PtrSimbolo* s = &mmu->simbolos[i];
        if (!s->activo) continue;
        for (int j = 0; j < s->num_paginas; j++) {
            if (!s->paginas[j]->en_memoria_real) cuenta++;
        }
    }
    return cuenta;
}
