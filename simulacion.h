#ifndef SIMULACION_H
#define SIMULACION_H

#include "mmu.h"
#include <stdbool.h>

/* TipoInstruccion / Instruccion ahora viven en mmu.h (el algoritmo OPT
 * necesita "ver el futuro" dentro de la propia MMU), asi que aqui solo
 * se declara el motor de la simulacion. */

/* Corre dos MMUs en paralelo sobre la MISMA secuencia de instrucciones:
 * una con el algoritmo elegido por el usuario y otra siempre con OPT
 * (para poder comparar), imprimiendo un dashboard despues de cada
 * instruccion. Si modo_automatico es true, avanza solo (con una breve
 * pausa entre pasos) y el usuario puede presionar ENTER en cualquier
 * momento para pausar/reanudar; si es false, exige ENTER para avanzar
 * cada paso. Escribir 'q' + ENTER en cualquier pausa termina la
 * simulacion anticipadamente (se igual imprimen las metricas finales). */
void ejecutar_simulacion_simultanea(const Instruccion* instrucciones, int total_instrucciones,
                                     TipoAlgoritmo algo_usuario, bool modo_automatico);

#endif
