package cargasms;

import java.time.Duration;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

public final class Main {

    private static final String USO = """
            Uso: java -jar cliente-carga-sms.jar [opciones]
              --host H          servidor (por defecto 127.0.0.1)
              --puerto N        puerto (por defecto 6060)
              --clientes N      teléfonos conectados a la vez (por defecto 50)
              --mensajes N      mensajes que manda cada uno (por defecto 200)
              --hilos N         hilos que mandan en paralelo (por defecto 8)
              --ventana N       mensajes sin respuesta por conexión (por defecto 50)
              --espera S        segundos máximos a esperar las entregas (por defecto 30)
              --semilla N       para repetir exactamente la misma prueba
              --db ARCHIVO      guarda el resultado en SQLite (por defecto resultados.db, "" para no guardar)
              --historial       muestra las corridas guardadas y no ejecuta nada
            """;

    private Main() {
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.forLanguageTag("es-AR"));
        final Map<String, String> op = new HashMap<>();
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("--historial") || args[i].equals("--help")) {
                op.put(args[i], "");
            } else if (args[i].startsWith("--") && i + 1 < args.length) {
                op.put(args[i], args[++i]);
            } else {
                System.err.print(USO);
                System.exit(2);
            }
        }
        if (op.containsKey("--help")) {
            System.out.print(USO);
            return;
        }
        final String db = op.getOrDefault("--db", "resultados.db");

        if (op.containsKey("--historial")) {
            mostrarHistorial(db);
            return;
        }

        final PruebaDeCarga.Config config = new PruebaDeCarga.Config(
                op.getOrDefault("--host", "127.0.0.1"),
                Integer.parseInt(op.getOrDefault("--puerto", "6060")),
                Integer.parseInt(op.getOrDefault("--clientes", "50")),
                Integer.parseInt(op.getOrDefault("--mensajes", "200")),
                Integer.parseInt(op.getOrDefault("--hilos", "8")),
                Integer.parseInt(op.getOrDefault("--ventana", "50")),
                Duration.ofSeconds(Long.parseLong(op.getOrDefault("--espera", "30"))),
                Long.parseLong(op.getOrDefault("--semilla", "42")));

        System.out.printf("Conectando %d clientes a %s:%d, %d mensajes en total...%n", config.clientes(),
                config.host(), config.puerto(), config.total());
        final PruebaDeCarga.Resultado r = new PruebaDeCarga(config).ejecutar();
        imprimir(r);

        if (!db.isEmpty()) {
            try (HistorialCorridas h = new HistorialCorridas(db)) {
                System.out.printf("%nGuardado como corrida %d en %s%n", h.guardar(r), db);
            }
        }
        System.exit(r.completo() ? 0 : 1);
    }

    private static void imprimir(PruebaDeCarga.Resultado r) {
        System.out.printf("%nDuración: %.2f s%n", r.duracion().toMillis() / 1000.0);
        System.out.printf("Aceptados: %d · entregados: %d · errores: %d · recibidos: %d%n", r.aceptados(),
                r.entregados(), r.errores(), r.recibidos());
        System.out.printf("Rendimiento: %.0f mensajes entregados por segundo%n%n", r.entregadosPorSegundo());
        System.out.println("Tiempos (ms)              promedio      p50      p95      p99   máximo");
        fila("Aceptación (OK ENVIADO)", r.aceptacion());
        fila("Entrega (ENTREGADO)", r.entrega());
        if (!r.completo())
            System.out.printf("%nATENCIÓN: faltaron %d entregas%n", r.config().total() - r.entregados());
    }

    private static void fila(String nombre, Estadisticas.Resumen s) {
        System.out.printf("%-24s %9.2f %8.2f %8.2f %8.2f %8.2f%n", nombre, s.promedio(), s.p50(), s.p95(), s.p99(),
                s.maximo());
    }

    private static void mostrarHistorial(String db) throws Exception {
        try (HistorialCorridas h = new HistorialCorridas(db)) {
            System.out.println("Últimas corridas");
            System.out.println("  id  fecha                clientes  mensajes   msg/s   p95 ms   p99 ms  errores");
            for (HistorialCorridas.Fila f : h.ultimas(10))
                System.out.printf("%4d  %s  %8d  %8d  %6.0f  %7.2f  %7.2f  %7d%n", f.id(), f.fecha(), f.clientes(),
                        f.mensajes(), f.porSegundo(), f.p95Entrega(), f.p99Entrega(), f.errores());
            System.out.println("\nPor cantidad de clientes (corridas sin errores)");
            System.out.println("  clientes  corridas   msg/s prom.   p95 prom.   p95 mejor");
            for (HistorialCorridas.PorClientes p : h.porCantidadDeClientes())
                System.out.printf("  %8d  %8d  %12.0f  %10.2f  %10.2f%n", p.clientes(), p.corridas(),
                        p.porSegundoPromedio(), p.p95Promedio(), p.p95Mejor());
        }
    }
}
