#include "lector.h"

void lector_iniciar(lector_lineas *l)
{
    l->usado = 0;
    l->descartando = 0;
}

int lector_agregar(lector_lineas *l, const char *datos, size_t n, al_recibir_linea fn, void *ctx)
{
    for (size_t i = 0; i < n; i++) {
        const char c = datos[i];
        int r = 0;

        if (c == '\n') {
            if (l->descartando) {
                r = fn(ctx, NULL);
            } else {
                if (l->usado > 0 && l->buf[l->usado - 1] == '\r') /* clientes tipo telnet */
                    l->usado--;
                l->buf[l->usado] = '\0';
                if (l->usado > 0)
                    r = fn(ctx, l->buf);
            }
            l->usado = 0;
            l->descartando = 0;
        } else if (!l->descartando) {
            if (l->usado == LARGO_MAX_LINEA) {
                l->descartando = 1;
                l->usado = 0;
            } else {
                l->buf[l->usado++] = c;
            }
        }
        if (r != 0)
            return r;
    }
    return 0;
}
