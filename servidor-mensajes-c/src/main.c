#define _POSIX_C_SOURCE 200809L

#include "servidor.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void uso(const char *programa)
{
    fprintf(stderr,
            "Uso: %s [--puerto N] [--max-clientes N] [--max-pendientes N] [-v]\n"
            "  --puerto N          puerto TCP (por defecto 6060)\n"
            "  --max-clientes N    conexiones simultáneas (por defecto 1000)\n"
            "  --max-pendientes N  mensajes guardados esperando entrega (por defecto 100000)\n"
            "  -v                  muestra cada login, mensaje y desconexión\n",
            programa);
}

static int leer_entero(const char *texto, long minimo, long maximo, long *valor)
{
    char *fin;
    errno = 0;
    *valor = strtol(texto, &fin, 10);
    return errno == 0 && *fin == '\0' && *valor >= minimo && *valor <= maximo;
}

int main(int argc, char **argv)
{
    servidor_config cfg = {6060, 1000, 100000, 0};
    long valor;

    for (int i = 1; i < argc; i++) {
        const int hay_valor = i + 1 < argc;
        if (strcmp(argv[i], "--puerto") == 0 && hay_valor && leer_entero(argv[i + 1], 1, 65535, &valor)) {
            cfg.puerto = (unsigned short)valor;
            i++;
        } else if (strcmp(argv[i], "--max-clientes") == 0 && hay_valor && leer_entero(argv[i + 1], 1, 100000, &valor)) {
            cfg.max_clientes = (int)valor;
            i++;
        } else if (strcmp(argv[i], "--max-pendientes") == 0 && hay_valor &&
                   leer_entero(argv[i + 1], 1, 10000000, &valor)) {
            cfg.max_pendientes = (int)valor;
            i++;
        } else if (strcmp(argv[i], "-v") == 0) {
            cfg.verbose = 1;
        } else {
            uso(argv[0]);
            return strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }

    /* Ctrl+C y SIGTERM se bloquean en todos los hilos y se esperan acá con sigwait():
     * así el cierre ordenado corre en un hilo normal y no dentro de un manejador de señales */
    sigset_t senales;
    sigemptyset(&senales);
    sigaddset(&senales, SIGINT);
    sigaddset(&senales, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &senales, NULL);

    servidor *s = servidor_crear(&cfg);
    if (!s || servidor_iniciar(s) != 0) {
        fprintf(stderr, "No se pudo iniciar el servidor en el puerto %u: %s\n", cfg.puerto, strerror(errno));
        servidor_destruir(s);
        return 1;
    }
    fprintf(stderr, "Servidor de mensajes escuchando en el puerto %u (Ctrl+C para cerrar)\n", servidor_puerto(s));

    int senal;
    sigwait(&senales, &senal);
    fprintf(stderr, "\nCerrando...\n");
    servidor_detener(s);

    servidor_stats st;
    servidor_stats_leer(s, &st);
    fprintf(stderr, "Mensajes recibidos: %llu · entregados: %llu · pendientes: %d · órdenes rechazadas: %llu\n",
            st.recibidos, st.entregados, st.pendientes, st.rechazados);
    servidor_destruir(s);
    return 0;
}
