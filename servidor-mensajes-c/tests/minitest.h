#ifndef MINITEST_H
#define MINITEST_H

/* Mini framework de pruebas: sin dependencias, para compilar en cualquier Linux. */

#include <stdio.h>
#include <string.h>

static int mt_fallas = 0;
static int mt_chequeos = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        mt_chequeos++;                                                                 \
        if (!(cond)) {                                                                 \
            mt_fallas++;                                                               \
            fprintf(stderr, "  FALLA %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                              \
    } while (0)

#define CHECK_STR(actual, esperado)                                                    \
    do {                                                                               \
        const char *mt_a = (actual), *mt_e = (esperado);                               \
        mt_chequeos++;                                                                 \
        if (!mt_a || !mt_e || strcmp(mt_a, mt_e) != 0) {                               \
            mt_fallas++;                                                               \
            fprintf(stderr, "  FALLA %s:%d: se esperaba \"%s\" y llegó \"%s\"\n", __FILE__, __LINE__, \
                    mt_e ? mt_e : "(null)", mt_a ? mt_a : "(null)");                    \
        }                                                                              \
    } while (0)

#define RUN(prueba)                                                                    \
    do {                                                                               \
        const int mt_antes = mt_fallas;                                                \
        prueba();                                                                      \
        printf("%s %s\n", mt_fallas == mt_antes ? "OK   " : "FALLA", #prueba);         \
    } while (0)

#define RESUMEN()                                                                      \
    (printf("\n%d chequeos, %d fallas\n", mt_chequeos, mt_fallas), mt_fallas == 0 ? 0 : 1)

#endif
