#define _POSIX_C_SOURCE 200809L

#include "../src/servidor.h"
#include "minitest.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Pruebas de integración: levantan el servidor real en un puerto libre y se conectan por TCP. */

static unsigned short g_puerto;

typedef struct {
    int fd;
    char buf[8192];
    size_t usado;
    char linea[1024];
} cliente;

static int conectar(cliente *c, unsigned short puerto)
{
    struct sockaddr_in dir;
    memset(c, 0, sizeof *c);
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_port = htons(puerto);
    dir.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return connect(c->fd, (struct sockaddr *)&dir, sizeof dir);
}

static void decir(cliente *c, const char *fmt, ...)
{
    char linea[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(linea, sizeof linea - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof linea - 2)
        n = (int)sizeof linea - 2;
    linea[n++] = '\n';
    send(c->fd, linea, (size_t)n, MSG_NOSIGNAL);
}

/* Devuelve la próxima línea o NULL si no llegó nada en 2 segundos. */
static const char *escuchar(cliente *c)
{
    for (;;) {
        char *fin = memchr(c->buf, '\n', c->usado);
        if (fin) {
            const size_t largo = (size_t)(fin - c->buf);
            memcpy(c->linea, c->buf, largo);
            c->linea[largo] = '\0';
            memmove(c->buf, fin + 1, c->usado - largo - 1);
            c->usado -= largo + 1;
            return c->linea;
        }
        struct pollfd p = {c->fd, POLLIN, 0};
        if (poll(&p, 1, 2000) <= 0)
            return NULL;
        const ssize_t n = recv(c->fd, c->buf + c->usado, sizeof c->buf - c->usado, 0);
        if (n <= 0)
            return NULL;
        c->usado += (size_t)n;
    }
}

static void cerrar(cliente *c)
{
    close(c->fd);
}

/* Conecta, consume el saludo y hace LOGIN. */
static void entrar(cliente *c, const char *numero, int pendientes)
{
    char esperado[64];
    conectar(c, g_puerto);
    CHECK_STR(escuchar(c), "HOLA servidor-sms 1.0");
    decir(c, "LOGIN %s", numero);
    snprintf(esperado, sizeof esperado, "OK LOGIN %s %d", numero, pendientes);
    CHECK_STR(escuchar(c), esperado);
}

/* Espera a que el servidor procese una desconexión (corre en otro hilo). */
static void esperar_conectados(servidor *s, int cantidad)
{
    servidor_stats st;
    for (int i = 0; i < 200; i++) {
        servidor_stats_leer(s, &st);
        if (st.conectados == cantidad)
            return;
        const struct timespec pausa = {0, 10 * 1000 * 1000};
        nanosleep(&pausa, NULL);
    }
}

static servidor *g_servidor;

static void saludo_login_y_errores(void)
{
    cliente a, b;
    conectar(&a, g_puerto);
    CHECK_STR(escuchar(&a), "HOLA servidor-sms 1.0");
    decir(&a, "PING");
    CHECK_STR(escuchar(&a), "PONG");
    decir(&a, "ENVIAR 1122223333 hola");
    CHECK_STR(escuchar(&a), "ERR SIN_LOGIN");
    decir(&a, "VOLAR");
    CHECK_STR(escuchar(&a), "ERR COMANDO_DESCONOCIDO");
    decir(&a, "LOGIN 1111");
    CHECK_STR(escuchar(&a), "OK LOGIN 1111 0");
    decir(&a, "LOGIN 1112");
    CHECK_STR(escuchar(&a), "ERR YA_LOGUEADO");

    entrar(&b, "1112", 0);
    decir(&b, "ACK 999");
    CHECK_STR(escuchar(&b), "ERR ID_DESCONOCIDO");
    cerrar(&b);

    conectar(&b, g_puerto);
    escuchar(&b);
    decir(&b, "LOGIN 1111");
    CHECK_STR(escuchar(&b), "ERR NUMERO_EN_USO");
    decir(&b, "SALIR");
    CHECK_STR(escuchar(&b), "OK CHAU");
    CHECK(escuchar(&b) == NULL); /* el servidor cerró */
    cerrar(&b);
    cerrar(&a);
    esperar_conectados(g_servidor, 0);
}

static void entrega_en_linea_con_confirmacion(void)
{
    cliente a, b;
    unsigned long long id;
    entrar(&a, "2221", 0);
    entrar(&b, "2222", 0);
    decir(&a, "ENVIAR 2222 Hola, ¿cómo andás?");
    CHECK(sscanf(escuchar(&a), "OK ENVIADO %llu", &id) == 1);

    char esperado[128];
    snprintf(esperado, sizeof esperado, "MSG %llu 2221 Hola, ¿cómo andás?", id);
    CHECK_STR(escuchar(&b), esperado);
    decir(&b, "ACK %llu", id);
    snprintf(esperado, sizeof esperado, "OK ACK %llu", id);
    CHECK_STR(escuchar(&b), esperado);
    snprintf(esperado, sizeof esperado, "ENTREGADO %llu 2222", id);
    CHECK_STR(escuchar(&a), esperado);

    /* Un segundo ACK del mismo id ya no vale */
    decir(&b, "ACK %llu", id);
    CHECK_STR(escuchar(&b), "ERR ID_DESCONOCIDO");
    cerrar(&a);
    cerrar(&b);
    esperar_conectados(g_servidor, 0);
}

static void guarda_para_desconectados(void)
{
    cliente a, b;
    entrar(&a, "3331", 0);
    decir(&a, "ENVIAR 3332 primero");
    CHECK(strncmp(escuchar(&a), "OK ENVIADO", 10) == 0);
    decir(&a, "ENVIAR 3332 segundo");
    CHECK(strncmp(escuchar(&a), "OK ENVIADO", 10) == 0);

    entrar(&b, "3332", 2);
    const char *l = escuchar(&b);
    CHECK(l && strstr(l, " 3331 primero"));
    l = escuchar(&b);
    CHECK(l && strstr(l, " 3331 segundo"));
    cerrar(&a);
    cerrar(&b);
    esperar_conectados(g_servidor, 0);
}

static void reenvia_lo_no_confirmado(void)
{
    cliente a, b;
    unsigned long long id, id2;
    entrar(&a, "4441", 0);
    entrar(&b, "4442", 0);
    decir(&a, "ENVIAR 4442 importante");
    sscanf(escuchar(&a), "OK ENVIADO %llu", &id);
    CHECK(escuchar(&b) != NULL);
    cerrar(&b); /* se va sin mandar ACK */
    esperar_conectados(g_servidor, 1);

    entrar(&b, "4442", 1);
    CHECK(sscanf(escuchar(&b), "MSG %llu 4441", &id2) == 1 && id2 == id);
    cerrar(&a);
    cerrar(&b);
    esperar_conectados(g_servidor, 0);
}

static void confirmacion_para_remitente_desconectado(void)
{
    cliente a, b;
    unsigned long long id;
    char esperado[64];
    entrar(&a, "5551", 0);
    entrar(&b, "5552", 0);
    decir(&a, "ENVIAR 5552 te espero");
    sscanf(escuchar(&a), "OK ENVIADO %llu", &id);
    cerrar(&a);
    esperar_conectados(g_servidor, 1);

    CHECK(escuchar(&b) != NULL);
    decir(&b, "ACK %llu", id);
    escuchar(&b);

    entrar(&a, "5551", 1);
    snprintf(esperado, sizeof esperado, "ENTREGADO %llu 5552", id);
    CHECK_STR(escuchar(&a), esperado);
    cerrar(&a);
    cerrar(&b);
    esperar_conectados(g_servidor, 0);
}

static void lineas_y_textos_largos(void)
{
    cliente a;
    char grande[2000];
    entrar(&a, "6661", 0);
    memset(grande, 'z', sizeof grande - 1);
    grande[sizeof grande - 1] = '\0';
    decir(&a, "%s", grande);
    CHECK_STR(escuchar(&a), "ERR LINEA_LARGA");

    memset(grande, 'a', 161);
    grande[161] = '\0';
    decir(&a, "ENVIAR 6662 %s", grande);
    CHECK_STR(escuchar(&a), "ERR TEXTO_LARGO");
    decir(&a, "PING"); /* la conexión sigue viva */
    CHECK_STR(escuchar(&a), "PONG");
    cerrar(&a);
    esperar_conectados(g_servidor, 0);
}

/* --- Concurrencia: varios clientes mandan y reciben a la vez --- */

#define CLIENTES 20
#define MENSAJES 200

typedef struct {
    int indice;
    int recibidos, confirmados, enviados;
} resultado;

static void *cliente_carga(void *arg)
{
    resultado *r = arg;
    cliente c;
    char numero[16], destino[16];
    snprintf(numero, sizeof numero, "77%04d", r->indice);
    snprintf(destino, sizeof destino, "77%04d", (r->indice + 1) % CLIENTES);

    conectar(&c, g_puerto);
    escuchar(&c);
    decir(&c, "LOGIN %s", numero);
    escuchar(&c);
    for (int i = 0; i < MENSAJES; i++)
        decir(&c, "ENVIAR %s mensaje %d de %s", destino, i, numero);

    /* Lo que llega se mezcla: respuestas propias, mensajes de otro cliente y confirmaciones */
    while (r->enviados < MENSAJES || r->recibidos < MENSAJES || r->confirmados < MENSAJES) {
        const char *l = escuchar(&c);
        unsigned long long id;
        if (!l)
            break;
        if (strncmp(l, "OK ENVIADO", 10) == 0) {
            r->enviados++;
        } else if (sscanf(l, "MSG %llu", &id) == 1) {
            r->recibidos++;
            decir(&c, "ACK %llu", id);
        } else if (strncmp(l, "ENTREGADO", 9) == 0) {
            r->confirmados++;
        }
    }
    /* Espera los "OK ACK" que faltan antes de irse, para que todo quede confirmado */
    while (escuchar(&c) != NULL)
        ;
    cerrar(&c);
    return NULL;
}

static void muchos_clientes_a_la_vez(void)
{
    pthread_t hilos[CLIENTES];
    resultado res[CLIENTES];
    servidor_stats antes, despues;
    servidor_stats_leer(g_servidor, &antes);

    for (int i = 0; i < CLIENTES; i++) {
        memset(&res[i], 0, sizeof res[i]);
        res[i].indice = i;
        pthread_create(&hilos[i], NULL, cliente_carga, &res[i]);
    }
    int completos = 0;
    for (int i = 0; i < CLIENTES; i++) {
        pthread_join(hilos[i], NULL);
        completos += res[i].enviados == MENSAJES && res[i].recibidos == MENSAJES && res[i].confirmados == MENSAJES;
    }
    CHECK(completos == CLIENTES);
    esperar_conectados(g_servidor, 0);
    servidor_stats_leer(g_servidor, &despues);
    CHECK(despues.recibidos - antes.recibidos == CLIENTES * MENSAJES);
    CHECK(despues.entregados - antes.entregados == CLIENTES * MENSAJES);
    CHECK(despues.pendientes == antes.pendientes); /* las pruebas anteriores dejan algunos sin confirmar */
}

static void limites(void)
{
    servidor_config cfg = {0, 2, 3, 0};
    servidor *chico = servidor_crear(&cfg);
    cliente a, b, c;
    CHECK(servidor_iniciar(chico) == 0);

    conectar(&a, servidor_puerto(chico));
    escuchar(&a);
    conectar(&b, servidor_puerto(chico));
    escuchar(&b);
    conectar(&c, servidor_puerto(chico));
    CHECK_STR(escuchar(&c), "ERR SERVIDOR_LLENO");
    cerrar(&c);

    decir(&a, "LOGIN 8881");
    escuchar(&a);
    for (int i = 0; i < 3; i++) {
        decir(&a, "ENVIAR 8882 hola %d", i);
        CHECK(strncmp(escuchar(&a), "OK ENVIADO", 10) == 0);
    }
    decir(&a, "ENVIAR 8882 uno de más");
    CHECK_STR(escuchar(&a), "ERR COLA_LLENA");
    decir(&a, "ESTADO");
    CHECK_STR(escuchar(&a), "OK ESTADO conectados=2 pendientes=3 recibidos=3 entregados=0");

    /* Detener con clientes conectados no se cuelga y los desconecta */
    servidor_destruir(chico);
    CHECK(escuchar(&a) == NULL);
    cerrar(&a);
    cerrar(&b);
}

int main(void)
{
    servidor_config cfg = {0, 100, 100000, 0};
    g_servidor = servidor_crear(&cfg);
    if (servidor_iniciar(g_servidor) != 0) {
        perror("servidor_iniciar");
        return 1;
    }
    g_puerto = servidor_puerto(g_servidor);

    RUN(saludo_login_y_errores);
    RUN(entrega_en_linea_con_confirmacion);
    RUN(guarda_para_desconectados);
    RUN(reenvia_lo_no_confirmado);
    RUN(confirmacion_para_remitente_desconectado);
    RUN(lineas_y_textos_largos);
    RUN(muchos_clientes_a_la_vez);
    RUN(limites);

    servidor_destruir(g_servidor);
    return RESUMEN();
}
