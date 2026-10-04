#ifndef SERVIDOR_H
#define SERVIDOR_H

typedef struct servidor servidor;

typedef struct {
    unsigned short puerto;   /* 0 = el sistema elige uno libre (útil en las pruebas) */
    int max_clientes;        /* conexiones simultáneas */
    int max_pendientes;      /* mensajes y avisos guardados esperando entrega */
    int verbose;             /* registra cada orden en stderr */
} servidor_config;

typedef struct {
    int conectados;
    int pendientes;
    unsigned long long recibidos;   /* ENVIAR aceptados */
    unsigned long long entregados;  /* confirmados con ACK por el destino */
    unsigned long long rechazados;  /* órdenes con error */
} servidor_stats;

servidor *servidor_crear(const servidor_config *cfg);

/* Abre el puerto y arranca el hilo que acepta conexiones. Devuelve 0 o -1 (errno indica la causa). */
int servidor_iniciar(servidor *s);

unsigned short servidor_puerto(const servidor *s);

void servidor_stats_leer(servidor *s, servidor_stats *out);

/* Cierra el puerto y todas las conexiones, y espera a que terminen los hilos. */
void servidor_detener(servidor *s);

void servidor_destruir(servidor *s);

#endif
