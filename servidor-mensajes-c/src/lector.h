#ifndef LECTOR_H
#define LECTOR_H

#include "protocolo.h"

#include <stddef.h>

/* Arma líneas completas a partir de lo que llega por TCP. Un recv() puede traer media
 * línea o varias juntas: TCP es un flujo de bytes, no de mensajes. */
typedef struct {
    char buf[LARGO_MAX_LINEA + 1];
    size_t usado;
    int descartando; /* la línea actual ya superó el máximo: se ignora hasta el próximo '\n' */
} lector_lineas;

/* linea es NULL cuando se descartó una línea demasiado larga. Devolver distinto de 0 corta. */
typedef int (*al_recibir_linea)(void *ctx, const char *linea);

void lector_iniciar(lector_lineas *l);

/* Procesa n bytes. Devuelve 0, o lo que devolvió la función si pidió cortar. */
int lector_agregar(lector_lineas *l, const char *datos, size_t n, al_recibir_linea fn, void *ctx);

#endif
