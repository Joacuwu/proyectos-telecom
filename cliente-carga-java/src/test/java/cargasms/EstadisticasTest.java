package cargasms;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class EstadisticasTest {

    @Test
    void percentilesPorRangoMasCercano() {
        final long[] datos = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        assertEquals(5, Estadisticas.percentil(datos, 50));
        assertEquals(10, Estadisticas.percentil(datos, 95));
        assertEquals(1, Estadisticas.percentil(datos, 0));
        assertEquals(10, Estadisticas.percentil(datos, 100));
    }

    @Test
    void resumenEnMilisegundos() {
        final Estadisticas e = new Estadisticas();
        for (int i = 1; i <= 100; i++)
            e.agregar(i * 1_000_000L); // 1 a 100 ms, desordenado no importa
        final Estadisticas.Resumen r = e.resumen();
        assertEquals(100, r.cantidad());
        assertEquals(50.5, r.promedio(), 1e-9);
        assertEquals(50, r.p50(), 1e-9);
        assertEquals(95, r.p95(), 1e-9);
        assertEquals(99, r.p99(), 1e-9);
        assertEquals(100, r.maximo(), 1e-9);
    }

    @Test
    void sinDatosDevuelveVacio() {
        assertEquals(Estadisticas.Resumen.VACIO, new Estadisticas().resumen());
    }

    @Test
    void aguantaVariosHilosAgregandoALaVez() throws InterruptedException {
        final Estadisticas e = new Estadisticas();
        final List<Thread> hilos = new ArrayList<>();
        for (int h = 0; h < 8; h++) {
            final Thread t = new Thread(() -> {
                for (int i = 0; i < 10_000; i++)
                    e.agregar(i);
            });
            hilos.add(t);
            t.start();
        }
        for (Thread t : hilos)
            t.join();
        assertEquals(80_000, e.cantidad());
    }
}
