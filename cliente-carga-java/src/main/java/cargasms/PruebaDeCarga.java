package cargasms;

import java.io.IOException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.LongAdder;

/**
 * Conecta muchos "teléfonos" al servidor y hace que se manden mensajes entre ellos al mismo tiempo.
 * Mide cuánto tarda el servidor en aceptar cada mensaje y cuánto tarda la confirmación de entrega.
 */
public class PruebaDeCarga {

    public record Config(String host, int puerto, int clientes, int mensajesPorCliente, int hilos, int ventana,
                         Duration espera, long semilla) {
        public Config {
            if (clientes < 2)
                throw new IllegalArgumentException("Hacen falta al menos 2 clientes");
            if (mensajesPorCliente < 1 || hilos < 1 || ventana < 1)
                throw new IllegalArgumentException("Mensajes, hilos y ventana tienen que ser positivos");
        }

        public int total() {
            return clientes * mensajesPorCliente;
        }
    }

    public record Resultado(Config config, long aceptados, long entregados, long errores, long recibidos,
                            Duration duracion, Estadisticas.Resumen aceptacion, Estadisticas.Resumen entrega) {
        public double entregadosPorSegundo() {
            final double segundos = duracion.toNanos() / 1e9;
            return segundos > 0 ? entregados / segundos : 0;
        }

        public boolean completo() {
            return entregados == config.total() && errores == 0;
        }
    }

    private final Config config;

    public PruebaDeCarga(Config config) {
        this.config = config;
    }

    /** Número de teléfono del cliente i. Usa un prefijo por corrida para no recibir restos de corridas anteriores. */
    static String numero(int prefijo, int i) {
        return String.format("11%03d%05d", prefijo, i);
    }

    public Resultado ejecutar() throws IOException, InterruptedException {
        final Random azar = new Random(config.semilla());
        final int prefijo = (int) (System.currentTimeMillis() / 1000 % 1000);
        final Estadisticas aceptacion = new Estadisticas();
        final Estadisticas entrega = new Estadisticas();
        final LongAdder errores = new LongAdder();
        final LongAdder recibidos = new LongAdder();
        final CountDownLatch faltan = new CountDownLatch(config.total());
        final List<ClienteSms> clientes = new ArrayList<>();

        try {
            for (int i = 0; i < config.clientes(); i++) {
                final ClienteSms c = new ClienteSms(config.host(), config.puerto(), config.ventana(), Duration.ofSeconds(5));
                clientes.add(c);
                c.alRecibir(m -> recibidos.increment());
                c.alEntregar(e -> {
                    entrega.agregar(e.nanos());
                    faltan.countDown();
                });
                c.login(numero(prefijo, i), Duration.ofSeconds(5));
            }

            // Cada tarea es un cliente que manda todos sus mensajes; el pool limita cuántos mandan a la vez
            final ExecutorService pool = Executors.newFixedThreadPool(config.hilos());
            final long inicio = System.nanoTime();
            final List<Future<?>> tareas = new ArrayList<>();
            for (int i = 0; i < config.clientes(); i++) {
                final int yo = i;
                final ClienteSms cliente = clientes.get(i);
                final int[] destinos = new int[config.mensajesPorCliente()];
                for (int k = 0; k < destinos.length; k++) {
                    final int otro = azar.nextInt(config.clientes() - 1);
                    destinos[k] = otro >= yo ? otro + 1 : otro; // cualquiera menos yo
                }
                tareas.add(pool.submit(() -> {
                    for (int k = 0; k < destinos.length; k++) {
                        cliente.enviar(numero(prefijo, destinos[k]), "Prueba " + (k + 1) + " desde el cliente " + yo)
                                .whenComplete((envio, error) -> {
                                    if (error != null) {
                                        errores.increment();
                                        faltan.countDown();
                                    } else {
                                        aceptacion.agregar(envio.nanos());
                                    }
                                });
                    }
                    return null;
                }));
            }
            for (Future<?> t : tareas) {
                try {
                    t.get();
                } catch (java.util.concurrent.ExecutionException e) {
                    throw new IOException("Falló un cliente", e.getCause());
                }
            }
            pool.shutdown();

            faltan.await(config.espera().toMillis(), TimeUnit.MILLISECONDS);
            final Duration duracion = Duration.ofNanos(System.nanoTime() - inicio);
            return new Resultado(config, aceptacion.cantidad(), entrega.cantidad(), errores.sum(), recibidos.sum(),
                    duracion, aceptacion.resumen(), entrega.resumen());
        } finally {
            clientes.forEach(ClienteSms::close);
        }
    }
}
