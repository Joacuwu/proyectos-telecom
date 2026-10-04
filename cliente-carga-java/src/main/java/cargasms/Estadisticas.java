package cargasms;

import java.util.Arrays;

/** Junta tiempos (en nanosegundos) desde varios hilos y calcula percentiles. */
public class Estadisticas {

    /** Tiempos en milisegundos. */
    public record Resumen(int cantidad, double promedio, double p50, double p95, double p99, double maximo) {
        public static final Resumen VACIO = new Resumen(0, 0, 0, 0, 0, 0);
    }

    private long[] valores = new long[1024];
    private int cantidad;

    public synchronized void agregar(long nanos) {
        if (cantidad == valores.length)
            valores = Arrays.copyOf(valores, cantidad * 2);
        valores[cantidad++] = nanos;
    }

    public synchronized int cantidad() {
        return cantidad;
    }

    public Resumen resumen() {
        final long[] copia;
        synchronized (this) {
            copia = Arrays.copyOf(valores, cantidad);
        }
        if (copia.length == 0)
            return Resumen.VACIO;
        Arrays.sort(copia);
        final double suma = Arrays.stream(copia).asDoubleStream().sum();
        return new Resumen(copia.length, ms(suma / copia.length), ms(percentil(copia, 50)), ms(percentil(copia, 95)),
                ms(percentil(copia, 99)), ms(copia[copia.length - 1]));
    }

    /** Percentil por "rango más cercano": el valor que deja al menos p% de los datos por debajo o igual. */
    static long percentil(long[] ordenados, double p) {
        final int rango = (int) Math.ceil(p / 100.0 * ordenados.length);
        return ordenados[Math.max(0, rango - 1)];
    }

    private static double ms(double nanos) {
        return nanos / 1_000_000.0;
    }
}
