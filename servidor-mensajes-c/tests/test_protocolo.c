#include "../src/lector.h"
#include "../src/protocolo.h"
#include "minitest.h"

#include <stdlib.h>

static void numeros(void)
{
    CHECK(numero_valido("1130979092"));
    CHECK(numero_valido("+5491130979092"));
    CHECK(numero_valido("911"));
    CHECK(!numero_valido("12"));
    CHECK(!numero_valido("+"));
    CHECK(!numero_valido("1234567890123456")); /* 16 dígitos */
    CHECK(!numero_valido("11-3097"));
    CHECK(!numero_valido("++123"));
}

static void utf8(void)
{
    CHECK(caracteres_utf8("hola") == 4);
    CHECK(caracteres_utf8("ñandú") == 5);
    CHECK(caracteres_utf8("€") == 1);
    CHECK(caracteres_utf8("") == 0);
}

static void comandos_validos(void)
{
    comando c;
    CHECK(protocolo_parsear("LOGIN 1130979092", &c) == NULL && c.tipo == CMD_LOGIN);
    CHECK_STR(c.numero, "1130979092");
    CHECK(protocolo_parsear("enviar +5491100001111 Hola, ¿llegó?", &c) == NULL && c.tipo == CMD_ENVIAR);
    CHECK_STR(c.numero, "+5491100001111");
    CHECK_STR(c.texto, "Hola, ¿llegó?");
    CHECK(protocolo_parsear("ACK 42", &c) == NULL && c.tipo == CMD_ACK && c.id == 42);
    CHECK(protocolo_parsear("PING", &c) == NULL && c.tipo == CMD_PING);
    CHECK(protocolo_parsear("ESTADO", &c) == NULL && c.tipo == CMD_ESTADO);
    CHECK(protocolo_parsear("SALIR", &c) == NULL && c.tipo == CMD_SALIR);
}

static void comandos_invalidos(void)
{
    comando c;
    CHECK_STR(protocolo_parsear("BORRAR TODO", &c), "COMANDO_DESCONOCIDO");
    CHECK_STR(protocolo_parsear("UNAPALABRAMUYLARGAQUENOENTRA", &c), "COMANDO_DESCONOCIDO");
    CHECK_STR(protocolo_parsear("LOGIN", &c), "FALTAN_DATOS");
    CHECK_STR(protocolo_parsear("LOGIN abc", &c), "NUMERO_INVALIDO");
    CHECK_STR(protocolo_parsear("LOGIN 111 222", &c), "NUMERO_INVALIDO");
    CHECK_STR(protocolo_parsear("ENVIAR 1130979092", &c), "FALTAN_DATOS");
    CHECK_STR(protocolo_parsear("ENVIAR", &c), "FALTAN_DATOS");
    CHECK_STR(protocolo_parsear("ENVIAR 12345678901234567890 hola", &c), "NUMERO_INVALIDO");
    CHECK_STR(protocolo_parsear("ENVIAR 113097 con\ttab", &c), "TEXTO_INVALIDO");
    CHECK_STR(protocolo_parsear("ACK", &c), "FALTAN_DATOS");
    CHECK_STR(protocolo_parsear("ACK -1", &c), "ID_INVALIDO");
    CHECK_STR(protocolo_parsear("ACK 0", &c), "ID_INVALIDO");
    CHECK_STR(protocolo_parsear("ACK 12x", &c), "ID_INVALIDO");
    CHECK_STR(protocolo_parsear("ACK 99999999999999999999999", &c), "ID_INVALIDO");
}

static void largo_del_texto(void)
{
    char linea[LARGO_MAX_LINEA + 1];
    comando c;
    /* 160 "ñ" son 320 bytes pero 160 caracteres: entra justo */
    strcpy(linea, "ENVIAR 1130979092 ");
    for (int i = 0; i < 160; i++)
        strcat(linea, "ñ");
    CHECK(protocolo_parsear(linea, &c) == NULL);
    CHECK(caracteres_utf8(c.texto) == 160);
    strcat(linea, "!");
    CHECK_STR(protocolo_parsear(linea, &c), "TEXTO_LARGO");
}

/* --- Lector de líneas --- */

typedef struct {
    char lineas[8][LARGO_MAX_LINEA + 1];
    int cantidad;
    int largas;
} recolector;

static int juntar(void *ctx, const char *linea)
{
    recolector *r = ctx;
    if (!linea)
        r->largas++;
    else if (r->cantidad < 8)
        strcpy(r->lineas[r->cantidad++], linea);
    return 0;
}

static void lector_partido_y_junto(void)
{
    lector_lineas l;
    recolector r = {0};
    lector_iniciar(&l);
    lector_agregar(&l, "PI", 2, juntar, &r);
    CHECK(r.cantidad == 0);
    lector_agregar(&l, "NG\nLOGIN 111\r\n\nESTA", 19, juntar, &r);
    lector_agregar(&l, "DO\n", 3, juntar, &r);
    CHECK(r.cantidad == 3);
    CHECK_STR(r.lineas[0], "PING");
    CHECK_STR(r.lineas[1], "LOGIN 111"); /* sin el \r */
    CHECK_STR(r.lineas[2], "ESTADO");    /* la línea vacía se ignora */
}

static void lector_descarta_lineas_largas(void)
{
    lector_lineas l;
    recolector r = {0};
    char *basura = malloc(5000);
    memset(basura, 'x', 5000);
    lector_iniciar(&l);
    lector_agregar(&l, basura, 5000, juntar, &r);
    lector_agregar(&l, "\nPING\n", 6, juntar, &r);
    CHECK(r.largas == 1);
    CHECK(r.cantidad == 1);
    CHECK_STR(r.lineas[0], "PING");

    /* Exactamente el máximo sí entra */
    lector_iniciar(&l);
    r.cantidad = 0;
    lector_agregar(&l, basura, LARGO_MAX_LINEA, juntar, &r);
    lector_agregar(&l, "\n", 1, juntar, &r);
    CHECK(r.cantidad == 1 && strlen(r.lineas[0]) == LARGO_MAX_LINEA);
    free(basura);
}

static int cortar(void *ctx, const char *linea)
{
    (void)linea;
    return ++*(int *)ctx == 2 ? 7 : 0;
}

static void lector_corta_cuando_se_pide(void)
{
    lector_lineas l;
    int llamadas = 0;
    lector_iniciar(&l);
    CHECK(lector_agregar(&l, "A\nB\nC\n", 6, cortar, &llamadas) == 7);
    CHECK(llamadas == 2);
}

int main(void)
{
    RUN(numeros);
    RUN(utf8);
    RUN(comandos_validos);
    RUN(comandos_invalidos);
    RUN(largo_del_texto);
    RUN(lector_partido_y_junto);
    RUN(lector_descarta_lineas_largas);
    RUN(lector_corta_cuando_se_pide);
    return RESUMEN();
}
