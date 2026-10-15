#ifndef PARSER_H
#define PARSER_H

#include "mmu.h"

/* Lee un archivo de operaciones en el formato del Anexo 1
 * (new(pid,size) / use(ptr) / delete(ptr) / kill(pid), una por linea).
 * Reserva el arreglo de salida con malloc (el caller debe hacer free).
 * Devuelve 0 en exito, -1 si no se pudo abrir o el archivo esta vacio. */
int parser_leer_archivo(const char* ruta, Instruccion** out_instrucciones, int* out_total);

/* Genera una secuencia de instrucciones aleatoria y repetible (misma
 * semilla -> mismo resultado) para P procesos y a lo sumo N operaciones
 * en total, respetando las reglas del enunciado:
 *   - la suma total de operaciones no supera N
 *   - new() se puede usar en cualquier momento salvo despues de kill
 *   - use()/delete() solo si el proceso tiene al menos un ptr activo
 *   - kill() se usa una unica vez y es la ultima operacion del proceso
 * Reserva el arreglo de salida con malloc (el caller debe hacer free). */
void parser_generar_aleatorio(unsigned int semilla, int P, int N,
                               Instruccion** out_instrucciones, int* out_total);

/* Escribe la lista de instrucciones a disco en el mismo formato de
 * texto del Anexo 1 (para "permitir descargar" el archivo generado). */
int parser_escribir_archivo(const char* ruta, const Instruccion* instrucciones, int total);

#endif /* PARSER_H */
