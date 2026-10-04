#include "protocolo.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int numero_valido(const char *s)
{
    size_t digitos = 0;
    if (*s == '+')
        s++;
    for (; *s; s++, digitos++) {
        if (!isdigit((unsigned char)*s))
            return 0;
    }
    return digitos >= 3 && digitos <= 15;
}

size_t caracteres_utf8(const char *s)
{
    size_t n = 0;
    for (; *s; s++) {
        /* Los bytes 10xxxxxx continúan un carácter, no empiezan uno nuevo */
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    }
    return n;
}

/* Copia la siguiente palabra de *p en destino y deja *p apuntando al resto. */
static size_t siguiente_palabra(const char **p, char *destino, size_t capacidad)
{
    const char *inicio = *p;
    size_t largo = strcspn(inicio, " ");
    if (largo < capacidad) {
        memcpy(destino, inicio, largo);
        destino[largo] = '\0';
    } else {
        destino[0] = '\0';
    }
    *p = inicio + largo;
    if (**p == ' ')
        (*p)++;
    return largo;
}

static int tiene_control(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || *s == 0x7F)
            return 1;
    }
    return 0;
}

const char *protocolo_parsear(const char *linea, comando *cmd)
{
    char palabra[16];
    const char *resto = linea;

    memset(cmd, 0, sizeof *cmd);
    if (siguiente_palabra(&resto, palabra, sizeof palabra) >= sizeof palabra)
        return "COMANDO_DESCONOCIDO";

    if (strcasecmp(palabra, "PING") == 0) {
        cmd->tipo = CMD_PING;
    } else if (strcasecmp(palabra, "ESTADO") == 0) {
        cmd->tipo = CMD_ESTADO;
    } else if (strcasecmp(palabra, "SALIR") == 0) {
        cmd->tipo = CMD_SALIR;
    } else if (strcasecmp(palabra, "LOGIN") == 0) {
        cmd->tipo = CMD_LOGIN;
        if (*resto == '\0')
            return "FALTAN_DATOS";
        if (strlen(resto) > LARGO_MAX_NUMERO || !numero_valido(resto))
            return "NUMERO_INVALIDO";
        strcpy(cmd->numero, resto);
    } else if (strcasecmp(palabra, "ENVIAR") == 0) {
        cmd->tipo = CMD_ENVIAR;
        if (siguiente_palabra(&resto, cmd->numero, sizeof cmd->numero) > LARGO_MAX_NUMERO ||
            (cmd->numero[0] && !numero_valido(cmd->numero)))
            return "NUMERO_INVALIDO";
        if (cmd->numero[0] == '\0' || *resto == '\0')
            return "FALTAN_DATOS";
        if (tiene_control(resto))
            return "TEXTO_INVALIDO";
        if (caracteres_utf8(resto) > LARGO_MAX_TEXTO)
            return "TEXTO_LARGO";
        strcpy(cmd->texto, resto); /* entra: la línea entera mide como mucho LARGO_MAX_LINEA */
    } else if (strcasecmp(palabra, "ACK") == 0) {
        char *fin;
        cmd->tipo = CMD_ACK;
        if (*resto == '\0')
            return "FALTAN_DATOS";
        if (!isdigit((unsigned char)*resto))
            return "ID_INVALIDO";
        errno = 0;
        cmd->id = strtoull(resto, &fin, 10);
        if (errno != 0 || *fin != '\0' || cmd->id == 0)
            return "ID_INVALIDO";
    } else {
        return "COMANDO_DESCONOCIDO";
    }
    return NULL;
}
