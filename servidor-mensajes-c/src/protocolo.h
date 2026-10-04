#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stddef.h>

/* Protocolo de texto, una orden por línea terminada en '\n' (ver PROTOCOLO_SMS.md). */

#define LARGO_MAX_LINEA 512   /* bytes, sin contar el '\n' */
#define LARGO_MAX_TEXTO 160   /* caracteres, como un SMS */
#define LARGO_MAX_NUMERO 16   /* '+' opcional y hasta 15 dígitos (E.164) */

typedef enum {
    CMD_LOGIN,
    CMD_ENVIAR,
    CMD_ACK,
    CMD_PING,
    CMD_ESTADO,
    CMD_SALIR
} tipo_comando;

typedef struct {
    tipo_comando tipo;
    char numero[LARGO_MAX_NUMERO + 1];  /* LOGIN: número propio. ENVIAR: destino */
    char texto[LARGO_MAX_LINEA + 1];    /* ENVIAR */
    unsigned long long id;              /* ACK */
} comando;

/* Devuelve 1 si es un número de teléfono válido: '+' opcional y de 3 a 15 dígitos. */
int numero_valido(const char *s);

/* Cantidad de caracteres de un texto UTF-8 (no de bytes: "ñ" ocupa 2 bytes y cuenta 1). */
size_t caracteres_utf8(const char *s);

/* Interpreta una línea. Devuelve NULL si es válida o el código de error para responder
 * (por ejemplo "COMANDO_DESCONOCIDO"). */
const char *protocolo_parsear(const char *linea, comando *cmd);

#endif
