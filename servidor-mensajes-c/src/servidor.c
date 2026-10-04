#define _POSIX_C_SOURCE 200809L

#include "servidor.h"

#include "lector.h"
#include "protocolo.h"

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/*
 * Un hilo acepta conexiones y cada cliente tiene su propio hilo que lee y responde.
 * Todo el estado compartido (conexiones, mensajes, avisos, contadores) está protegido
 * por un único mutex, s->mutex. Las escrituras a un socket se hacen FUERA de ese mutex,
 * con el mutex de escritura de cada conexión, para que un cliente lento no frene a todos.
 */

typedef struct conexion {
    servidor *s;
    int fd;
    int refs;                            /* s->mutex. Se libera cuando llega a 0 */
    char numero[LARGO_MAX_NUMERO + 1];   /* "" hasta el LOGIN. Lo escribe solo su hilo, con s->mutex */
    pthread_mutex_t escritura;           /* una línea a la vez en el socket */
    lector_lineas lector;
    struct conexion *sig;
} conexion;

typedef struct mensaje {
    unsigned long long id;
    char origen[LARGO_MAX_NUMERO + 1];
    char destino[LARGO_MAX_NUMERO + 1];
    char texto[LARGO_MAX_LINEA + 1];
    int en_vuelo;                        /* se escribió al destino y se espera su ACK */
    struct mensaje *sig;
} mensaje;

/* Confirmación de entrega para un remitente que estaba desconectado. */
typedef struct aviso {
    unsigned long long id;
    char remitente[LARGO_MAX_NUMERO + 1];
    char receptor[LARGO_MAX_NUMERO + 1];
    struct aviso *sig;
} aviso;

typedef struct hilo_terminado {
    pthread_t hilo;
    struct hilo_terminado *sig;
} hilo_terminado;

struct servidor {
    servidor_config cfg;
    int fd_escucha;
    unsigned short puerto;
    pthread_t hilo_aceptar;
    int aceptando;

    pthread_mutex_t mutex;
    pthread_cond_t sin_hilos;
    int deteniendo;
    int hilos_vivos;
    conexion *conexiones;
    int cantidad_conexiones;
    mensaje *mensajes, *ultimo_mensaje;  /* cola FIFO: se entregan en orden de llegada */
    aviso *avisos, *ultimo_aviso;
    int pendientes;
    unsigned long long siguiente_id;
    unsigned long long recibidos, entregados, rechazados;
    hilo_terminado *terminados;          /* hilos de clientes que ya terminaron y falta juntar */
};

static void registrar(const servidor *s, const char *fmt, ...)
{
    char hora[16];
    char texto[LARGO_MAX_LINEA + 128];
    struct tm tm;
    time_t t;
    va_list ap;

    if (!s->cfg.verbose)
        return;
    t = time(NULL);
    localtime_r(&t, &tm);
    strftime(hora, sizeof hora, "%H:%M:%S", &tm);
    va_start(ap, fmt);
    vsnprintf(texto, sizeof texto, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s %s\n", hora, texto);
}

/* Escribe una línea completa. Devuelve 0 o -1 si el cliente ya no está.
 * tomar_mutex = 0 cuando quien llama ya tiene c->escritura. */
static int escribir_v(conexion *c, int tomar_mutex, const char *fmt, va_list ap)
{
    char linea[LARGO_MAX_LINEA + 64];
    size_t largo, enviado = 0;
    int resultado = 0;

    const int n = vsnprintf(linea, sizeof linea - 1, fmt, ap);
    if (n < 0)
        return -1;
    largo = (size_t)n < sizeof linea - 2 ? (size_t)n : sizeof linea - 2;
    linea[largo++] = '\n';

    if (tomar_mutex)
        pthread_mutex_lock(&c->escritura);
    while (enviado < largo) {
        /* MSG_NOSIGNAL: si el otro lado cerró, send() devuelve EPIPE en vez de matar el proceso */
        const ssize_t k = send(c->fd, linea + enviado, largo - enviado, MSG_NOSIGNAL);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            resultado = -1;
            break;
        }
        enviado += (size_t)k;
    }
    if (tomar_mutex)
        pthread_mutex_unlock(&c->escritura);
    return resultado;
}

static int escribir(conexion *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int r = escribir_v(c, 1, fmt, ap);
    va_end(ap);
    return r;
}

static int escribir_con_mutex_tomado(conexion *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int r = escribir_v(c, 0, fmt, ap);
    va_end(ap);
    return r;
}

/* Con s->mutex tomado. */
static conexion *buscar_conexion(servidor *s, const char *numero)
{
    for (conexion *c = s->conexiones; c; c = c->sig) {
        if (c->numero[0] && strcmp(c->numero, numero) == 0)
            return c;
    }
    return NULL;
}

static void liberar_conexion(conexion *c)
{
    close(c->fd);
    pthread_mutex_destroy(&c->escritura);
    free(c);
}

/* Suelta una referencia. El fd se cierra recién con la última, así nunca se escribe en un fd
 * que el sistema ya le reasignó a otra conexión. */
static void soltar(conexion *c)
{
    servidor *s = c->s;
    pthread_mutex_lock(&s->mutex);
    const int ultima = --c->refs == 0;
    pthread_mutex_unlock(&s->mutex);
    if (ultima)
        liberar_conexion(c);
}

/* Envía, de a uno y en orden, los mensajes y avisos guardados para este número. */
static int entregar_pendientes(conexion *c)
{
    servidor *s = c->s;
    for (;;) {
        char origen[LARGO_MAX_NUMERO + 1];
        char texto[LARGO_MAX_LINEA + 1];
        unsigned long long id = 0;
        aviso *a = NULL, *previo = NULL;

        pthread_mutex_lock(&s->mutex);
        for (mensaje *m = s->mensajes; m; m = m->sig) {
            if (!m->en_vuelo && strcmp(m->destino, c->numero) == 0) {
                m->en_vuelo = 1;
                id = m->id;
                strcpy(origen, m->origen);
                strcpy(texto, m->texto);
                break;
            }
        }
        if (id == 0) {
            for (a = s->avisos; a && strcmp(a->remitente, c->numero) != 0; a = a->sig)
                previo = a;
            if (a) {
                if (previo)
                    previo->sig = a->sig;
                else
                    s->avisos = a->sig;
                if (s->ultimo_aviso == a)
                    s->ultimo_aviso = previo;
                s->pendientes--;
            }
        }
        pthread_mutex_unlock(&s->mutex);

        if (id != 0) {
            if (escribir(c, "MSG %llu %s %s", id, origen, texto) != 0)
                return -1;
        } else if (a) {
            const int r = escribir(c, "ENTREGADO %llu %s", a->id, a->receptor);
            free(a);
            if (r != 0)
                return -1;
        } else {
            return 0;
        }
    }
}

static int login(conexion *c, const comando *cmd)
{
    servidor *s = c->s;
    const char *error = NULL;
    int pendientes = 0, r;

    /* Desde que el número queda registrado, otros hilos pueden escribirle un MSG. Con el
     * mutex de escritura tomado hasta mandar "OK LOGIN", esa respuesta siempre llega primero.
     * Orden de bloqueo: escritura y después s->mutex (nunca al revés, así no hay deadlock). */
    pthread_mutex_lock(&c->escritura);
    pthread_mutex_lock(&s->mutex);
    if (c->numero[0])
        error = "YA_LOGUEADO";
    else if (buscar_conexion(s, cmd->numero))
        error = "NUMERO_EN_USO";
    if (error) {
        s->rechazados++;
    } else {
        strcpy(c->numero, cmd->numero);
        for (mensaje *m = s->mensajes; m; m = m->sig)
            pendientes += !m->en_vuelo && strcmp(m->destino, c->numero) == 0;
        for (aviso *a = s->avisos; a; a = a->sig)
            pendientes += strcmp(a->remitente, c->numero) == 0;
    }
    pthread_mutex_unlock(&s->mutex);
    if (error)
        r = escribir_con_mutex_tomado(c, "ERR %s", error);
    else
        r = escribir_con_mutex_tomado(c, "OK LOGIN %s %d", c->numero, pendientes);
    pthread_mutex_unlock(&c->escritura);

    if (error || r != 0)
        return r;
    registrar(s, "%s inició sesión (%d pendientes)", c->numero, pendientes);
    return entregar_pendientes(c);
}

static int enviar(conexion *c, const comando *cmd)
{
    servidor *s = c->s;
    conexion *destino = NULL;
    mensaje *m = calloc(1, sizeof *m);
    const char *error = NULL;
    unsigned long long id = 0;

    pthread_mutex_lock(&s->mutex);
    if (!m) {
        error = "INTERNO";
    } else if (s->pendientes >= s->cfg.max_pendientes) {
        error = "COLA_LLENA";
    } else {
        id = m->id = ++s->siguiente_id;
        strcpy(m->origen, c->numero);
        strcpy(m->destino, cmd->numero);
        strcpy(m->texto, cmd->texto);
        if (s->ultimo_mensaje)
            s->ultimo_mensaje->sig = m;
        else
            s->mensajes = m;
        s->ultimo_mensaje = m;
        s->pendientes++;
        s->recibidos++;
        destino = buscar_conexion(s, cmd->numero);
        if (destino) {
            destino->refs++;
            m->en_vuelo = 1;
        }
    }
    if (error)
        s->rechazados++;
    pthread_mutex_unlock(&s->mutex);

    if (error) {
        free(m);
        return escribir(c, "ERR %s", error);
    }
    registrar(s, "mensaje %llu: %s -> %s%s", id, c->numero, cmd->numero, destino ? "" : " (guardado)");
    /* Primero se confirma al remitente, así conoce el id antes de que llegue el ENTREGADO */
    const int r = escribir(c, "OK ENVIADO %llu", id);
    if (destino) {
        /* Si el destino no responde (cerró o tiene el buffer lleno más allá del timeout), se le
         * cierra el socket para que su hilo lo dé de baja y el mensaje vuelva a la cola */
        if (escribir(destino, "MSG %llu %s %s", id, c->numero, cmd->texto) != 0)
            shutdown(destino->fd, SHUT_RDWR);
        soltar(destino);
    }
    return r;
}

static int confirmar(conexion *c, const comando *cmd)
{
    servidor *s = c->s;
    conexion *remitente = NULL;
    mensaje *m, *previo = NULL;
    char origen[LARGO_MAX_NUMERO + 1];

    pthread_mutex_lock(&s->mutex);
    for (m = s->mensajes; m && m->id != cmd->id; m = m->sig)
        previo = m;
    if (!m || !m->en_vuelo || strcmp(m->destino, c->numero) != 0) {
        s->rechazados++;
        pthread_mutex_unlock(&s->mutex);
        return escribir(c, "ERR ID_DESCONOCIDO");
    }
    if (previo)
        previo->sig = m->sig;
    else
        s->mensajes = m->sig;
    if (s->ultimo_mensaje == m)
        s->ultimo_mensaje = previo;
    s->entregados++;
    strcpy(origen, m->origen);
    free(m);

    remitente = buscar_conexion(s, origen);
    if (remitente) {
        remitente->refs++;
        s->pendientes--;
    } else {
        /* Ocupa el lugar del mensaje que se acaba de borrar: pendientes no cambia */
        aviso *a = calloc(1, sizeof *a);
        if (a) {
            a->id = cmd->id;
            strcpy(a->remitente, origen);
            strcpy(a->receptor, c->numero);
            if (s->ultimo_aviso)
                s->ultimo_aviso->sig = a;
            else
                s->avisos = a;
            s->ultimo_aviso = a;
        } else {
            s->pendientes--;
        }
    }
    pthread_mutex_unlock(&s->mutex);

    registrar(s, "mensaje %llu entregado a %s", cmd->id, c->numero);
    const int r = escribir(c, "OK ACK %llu", cmd->id);
    if (remitente) {
        if (escribir(remitente, "ENTREGADO %llu %s", cmd->id, c->numero) != 0)
            shutdown(remitente->fd, SHUT_RDWR);
        soltar(remitente);
    }
    return r;
}

static int estado(conexion *c)
{
    servidor_stats st;
    servidor_stats_leer(c->s, &st);
    return escribir(c, "OK ESTADO conectados=%d pendientes=%d recibidos=%llu entregados=%llu", st.conectados,
                    st.pendientes, st.recibidos, st.entregados);
}

static int rechazar(conexion *c, const char *error)
{
    pthread_mutex_lock(&c->s->mutex);
    c->s->rechazados++;
    pthread_mutex_unlock(&c->s->mutex);
    return escribir(c, "ERR %s", error);
}

/* Se llama por cada línea completa. Distinto de 0 = cerrar la conexión. */
static int al_recibir(void *ctx, const char *linea)
{
    conexion *c = ctx;
    comando cmd;
    const char *error;

    if (!linea)
        return rechazar(c, "LINEA_LARGA");
    error = protocolo_parsear(linea, &cmd);
    if (error)
        return rechazar(c, error);
    if (!c->numero[0] && (cmd.tipo == CMD_ENVIAR || cmd.tipo == CMD_ACK))
        return rechazar(c, "SIN_LOGIN");

    switch (cmd.tipo) {
    case CMD_LOGIN:
        return login(c, &cmd);
    case CMD_ENVIAR:
        return enviar(c, &cmd);
    case CMD_ACK:
        return confirmar(c, &cmd);
    case CMD_PING:
        return escribir(c, "PONG");
    case CMD_ESTADO:
        return estado(c);
    case CMD_SALIR:
        escribir(c, "OK CHAU");
        return 1;
    }
    return 0;
}

static void dar_de_baja(conexion *c)
{
    servidor *s = c->s;
    pthread_mutex_lock(&s->mutex);
    for (conexion **p = &s->conexiones; *p; p = &(*p)->sig) {
        if (*p == c) {
            *p = c->sig;
            break;
        }
    }
    s->cantidad_conexiones--;
    if (c->numero[0]) {
        /* Lo que se le mandó y no confirmó vuelve a la cola: se reenvía en el próximo LOGIN */
        for (mensaje *m = s->mensajes; m; m = m->sig) {
            if (m->en_vuelo && strcmp(m->destino, c->numero) == 0)
                m->en_vuelo = 0;
        }
        registrar(s, "%s se desconectó", c->numero);
        c->numero[0] = '\0';
    }
    pthread_mutex_unlock(&s->mutex);
}

static void *atender(void *arg)
{
    conexion *c = arg;
    servidor *s = c->s;
    char buf[4096];

    if (escribir(c, "HOLA servidor-sms 1.0") == 0) {
        for (;;) {
            const ssize_t n = recv(c->fd, buf, sizeof buf, 0);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            if (lector_agregar(&c->lector, buf, (size_t)n, al_recibir, c) != 0)
                break;
        }
    }
    dar_de_baja(c);
    soltar(c);

    hilo_terminado *t = malloc(sizeof *t);
    pthread_mutex_lock(&s->mutex);
    if (t) {
        t->hilo = pthread_self();
        t->sig = s->terminados;
        s->terminados = t;
    } else {
        pthread_detach(pthread_self());
    }
    s->hilos_vivos--;
    pthread_cond_broadcast(&s->sin_hilos);
    pthread_mutex_unlock(&s->mutex);
    return NULL;
}

/* pthread_join de los hilos de clientes que ya terminaron, para liberar sus recursos. */
static void juntar_terminados(servidor *s)
{
    pthread_mutex_lock(&s->mutex);
    hilo_terminado *lista = s->terminados;
    s->terminados = NULL;
    pthread_mutex_unlock(&s->mutex);
    while (lista) {
        hilo_terminado *sig = lista->sig;
        pthread_join(lista->hilo, NULL);
        free(lista);
        lista = sig;
    }
}

static void *aceptar(void *arg)
{
    servidor *s = arg;
    const struct timeval timeout_escritura = {2, 0};

    for (;;) {
        const int fd = accept(s->fd_escucha, NULL, NULL);
        juntar_terminados(s);
        if (fd < 0) {
            pthread_mutex_lock(&s->mutex);
            const int fin = s->deteniendo;
            pthread_mutex_unlock(&s->mutex);
            if (fin)
                break;
            if (errno == EMFILE || errno == ENFILE) {
                const struct timespec pausa = {0, 100 * 1000 * 1000};
                nanosleep(&pausa, NULL);
            }
            continue;
        }

        conexion *c = calloc(1, sizeof *c);
        int lleno = 0;
        pthread_mutex_lock(&s->mutex);
        if (!c || s->deteniendo || s->cantidad_conexiones >= s->cfg.max_clientes) {
            lleno = 1;
        } else {
            c->s = s;
            c->fd = fd;
            c->refs = 1; /* la del hilo que la atiende */
            pthread_mutex_init(&c->escritura, NULL);
            lector_iniciar(&c->lector);
            c->sig = s->conexiones;
            s->conexiones = c;
            s->cantidad_conexiones++;
            s->hilos_vivos++;
        }
        pthread_mutex_unlock(&s->mutex);

        if (lleno) {
            static const char aviso_lleno[] = "ERR SERVIDOR_LLENO\n";
            send(fd, aviso_lleno, sizeof aviso_lleno - 1, MSG_NOSIGNAL);
            close(fd);
            free(c);
            continue;
        }

        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout_escritura, sizeof timeout_escritura);
        pthread_t hilo;
        if (pthread_create(&hilo, NULL, atender, c) != 0) {
            dar_de_baja(c);
            pthread_mutex_lock(&s->mutex);
            s->hilos_vivos--;
            pthread_cond_broadcast(&s->sin_hilos);
            pthread_mutex_unlock(&s->mutex);
            soltar(c);
        }
    }
    return NULL;
}

servidor *servidor_crear(const servidor_config *cfg)
{
    servidor *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->cfg = *cfg;
    if (s->cfg.max_clientes <= 0)
        s->cfg.max_clientes = 1000;
    if (s->cfg.max_pendientes <= 0)
        s->cfg.max_pendientes = 100000;
    s->fd_escucha = -1;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->sin_hilos, NULL);
    return s;
}

int servidor_iniciar(servidor *s)
{
    struct sockaddr_in dir;
    socklen_t largo = sizeof dir;
    const int si = 1;

    s->fd_escucha = socket(AF_INET, SOCK_STREAM, 0);
    if (s->fd_escucha < 0)
        return -1;
    /* Permite reiniciar el servidor enseguida aunque queden conexiones en TIME_WAIT */
    setsockopt(s->fd_escucha, SOL_SOCKET, SO_REUSEADDR, &si, sizeof si);

    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_addr.s_addr = htonl(INADDR_ANY);
    dir.sin_port = htons(s->cfg.puerto);
    if (bind(s->fd_escucha, (struct sockaddr *)&dir, sizeof dir) < 0 || listen(s->fd_escucha, SOMAXCONN) < 0 ||
        getsockname(s->fd_escucha, (struct sockaddr *)&dir, &largo) < 0)
        goto error;
    s->puerto = ntohs(dir.sin_port);

    if (pthread_create(&s->hilo_aceptar, NULL, aceptar, s) != 0)
        goto error;
    s->aceptando = 1;
    return 0;

error : {
    const int e = errno;
    close(s->fd_escucha);
    s->fd_escucha = -1;
    errno = e;
    return -1;
}
}

unsigned short servidor_puerto(const servidor *s)
{
    return s->puerto;
}

void servidor_stats_leer(servidor *s, servidor_stats *out)
{
    pthread_mutex_lock(&s->mutex);
    out->conectados = s->cantidad_conexiones;
    out->pendientes = s->pendientes;
    out->recibidos = s->recibidos;
    out->entregados = s->entregados;
    out->rechazados = s->rechazados;
    pthread_mutex_unlock(&s->mutex);
}

void servidor_detener(servidor *s)
{
    pthread_mutex_lock(&s->mutex);
    if (s->deteniendo) {
        pthread_mutex_unlock(&s->mutex);
        return;
    }
    s->deteniendo = 1;
    /* Despierta a cada hilo bloqueado en recv() */
    for (conexion *c = s->conexiones; c; c = c->sig)
        shutdown(c->fd, SHUT_RDWR);
    pthread_mutex_unlock(&s->mutex);

    if (s->fd_escucha >= 0)
        shutdown(s->fd_escucha, SHUT_RDWR); /* despierta al accept() */
    if (s->aceptando) {
        pthread_join(s->hilo_aceptar, NULL);
        s->aceptando = 0;
    }

    pthread_mutex_lock(&s->mutex);
    while (s->hilos_vivos > 0)
        pthread_cond_wait(&s->sin_hilos, &s->mutex);
    pthread_mutex_unlock(&s->mutex);
    juntar_terminados(s);

    if (s->fd_escucha >= 0) {
        close(s->fd_escucha);
        s->fd_escucha = -1;
    }
}

void servidor_destruir(servidor *s)
{
    if (!s)
        return;
    servidor_detener(s);
    while (s->mensajes) {
        mensaje *sig = s->mensajes->sig;
        free(s->mensajes);
        s->mensajes = sig;
    }
    while (s->avisos) {
        aviso *sig = s->avisos->sig;
        free(s->avisos);
        s->avisos = sig;
    }
    pthread_cond_destroy(&s->sin_hilos);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}
