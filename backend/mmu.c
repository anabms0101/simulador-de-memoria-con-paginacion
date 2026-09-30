#include "mmu.h"

void inicializar_mmu(MMU* mmu, TipoAlgoritmo algo) {
    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
        mmu->ram[i] = NULL;
    }
    mmu->algoritmo = algo;
    mmu->reloj = 0;
    mmu->tiempo_thrashing = 0;
    mmu->puntero_reemplazo = 0;
}

// Busca un marco libre en la RAM (retorna -1 si está llena)
static int buscar_marco_libre(MMU* mmu) {
    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
        if (mmu->ram[i] == NULL) return i;
    }
    return -1;
}

// Selecciona la página víctima según el algoritmo configurado
static int seleccionar_victima(MMU* mmu) {
    int victima_idx = mmu->puntero_reemplazo;
    
    if (mmu->algoritmo == ALGO_SC) {
        // Lógica de Second Chance (Segunda Oportunidad)
        while (true) {
            Pagina* p = mmu->ram[victima_idx];
            if (p != NULL) {
                if (p->bit_uso == 1) {
                    p->bit_uso = 0; // Se le da una segunda oportunidad
                } else {
                    break; // Encontró la víctima con bit_uso en 0
                }
            }
            victima_idx = (victima_idx + 1) % TOTAL_MARCOS_RAM;
        }
    }
    
    // Actualiza el puntero circular para la próxima búsqueda
    mmu->puntero_reemplazo = (victima_idx + 1) % TOTAL_MARCOS_RAM;
    return victima_idx;
}

// Operación new(pid, size): Solicita memoria y asigna páginas[cite: 1]
void mmu_new(MMU* mmu, Proceso* proc, int size) {
    int num_paginas = (size + TAMANIO_PAGINA - 1) / TAMANIO_PAGINA; // Techo de la división[cite: 1]
    int nuevo_ptr = proc->total_simbolos + 1;

    PtrSimbolo* simbolo = &proc->simbolos[proc->total_simbolos];
    simbolo->ptr = nuevo_ptr;
    simbolo->num_paginas = 0;
    proc->total_simbolos++;

    printf("[NEW] PID %d asigna ptr %d (%d bytes, %d páginas).\n", proc->pid, nuevo_ptr, size, num_paginas);

    for (int i = 0; i < num_paginas; i++) {
        Pagina* nueva_pag = (Pagina*)malloc(sizeof(Pagina));
        nueva_pag->pid = proc->pid;
        nueva_pag->ptr = nuevo_ptr;
        nueva_pag->en_memoria_real = true;
        nueva_pag->bit_uso = 1;
        nueva_pag->contador_accesos = 1;
        sprintf(nueva_pag->id, "%d-p%d-%d", proc->pid, nuevo_ptr, i);

        int marco = buscar_marco_libre(mmu);

        if (marco != -1) {
            // Hay espacio directo en la RAM física
            nueva_pag->direccion_fisica = marco;
            mmu->ram[marco] = nueva_pag;
        } else {
            // RAM LLENA: Fallo de página por capacidad, se expulsa una víctima a disco[cite: 1]
            int marco_victima = seleccionar_victima(mmu);
            Pagina* victima = mmu->ram[marco_victima];

            if (victima != NULL) {
                victima->en_memoria_real = false; // Mandada a V-RAM / Disco[cite: 1]
            }

            nueva_pag->direccion_fisica = marco_victima;
            mmu->ram[marco_victima] = nueva_pag;

            // Penalización de tiempo en disco: +5s por fallo de página[cite: 1]
            mmu->reloj += 5;
            mmu->tiempo_thrashing += 5;
            printf("   [FALLO DE PÁGINA] RAM llena. Víctima expulsada a V-RAM (+5s).\n");
        }
        simbolo->paginas[simbolo->num_paginas++] = nueva_pag;
    }
}

// Operación use(ptr): Accede a un puntero existente[cite: 1]
void mmu_use(MMU* mmu, Proceso* proc, int ptr) {
    PtrSimbolo* simbolo = NULL;
    for (int i = 0; i < proc->total_simbolos; i++) {
        if (proc->simbolos[i].ptr == ptr) {
            simbolo = &proc->simbolos[i];
            break;
        }
    }

    if (!simbolo) {
        printf("[ERROR] Ptr %d no encontrado en el PID %d.\n", ptr, proc->pid);
        return;
    }

    printf("[USE] Accediendo a ptr %d del PID %d...\n", ptr, proc->pid);
    for (int i = 0; i < simbolo->num_paginas; i++) {
        Pagina* pag = simbolo->paginas[i];
        if (pag->en_memoria_real) {
            // HIT: La página está en RAM (+1s)[cite: 1]
            mmu->reloj += 1;
            pag->bit_uso = 1;
            pag->contador_accesos++;
            printf("   -> [HIT] Página en RAM (+1s).\n");
        } else {
            // FALLO: La página está en V-RAM y debe cargarse a RAM (+5s)[cite: 1]
            mmu->reloj += 5;
            mmu->tiempo_thrashing += 5;
            printf("   -> [FALLO] Página en V-RAM, trayendo a RAM (+5s).\n");

            int marco_victima = seleccionar_victima(mmu);
            Pagina* victima = mmu->ram[marco_victima];
            if (victima) {
                victima->en_memoria_real = false;
            }

            pag->direccion_fisica = marco_victima;
            pag->en_memoria_real = true;
            pag->bit_uso = 1;
            mmu->ram[marco_victima] = pag;
        }
    }
}

// Operación delete(ptr): Libera el puntero y su memoria asociada[cite: 1]
void mmu_delete(MMU* mmu, Proceso* proc, int ptr) {
    int index = -1;
    for (int i = 0; i < proc->total_simbolos; i++) {
        if (proc->simbolos[i].ptr == ptr) {
            index = i;
            break;
        }
    }

    if (index == -1) return;

    PtrSimbolo* simbolo = &proc->simbolos[index];
    printf("[DELETE] Borrando ptr %d del PID %d y liberando marcos.\n", ptr, proc->pid);

    for (int i = 0; i < simbolo->num_paginas; i++) {
        Pagina* pag = simbolo->paginas[i];
        if (pag->en_memoria_real) {
            mmu->ram[pag->direccion_fisica] = NULL;
        }
        free(pag);
    }

    for (int i = index; i < proc->total_simbolos - 1; i++) {
        proc->simbolos[i] = proc->simbolos[i + 1];
    }
    proc->total_simbolos--;
}

// Operación kill(pid): Elimina todos los punteros y libera toda la memoria del proceso[cite: 1]
void mmu_kill(MMU* mmu, Proceso* proc) {
    printf("[KILL] Terminando proceso PID %d y liberando toda su memoria.\n", proc->pid);
    while (proc->total_simbolos > 0) {
        mmu_delete(mmu, proc, proc->simbolos[0].ptr);
    }
}