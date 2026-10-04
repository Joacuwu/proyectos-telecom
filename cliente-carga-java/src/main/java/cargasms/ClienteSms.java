package cargasms;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Consumer;

/**
 * Un "teléfono" conectado al servidor de mensajes.
 *
 * <p>Usa dos hilos propios: uno lee todo lo que llega y otro escribe todo lo que sale. El que lee
 * nunca escribe directamente (los ACK van a la cola del escritor): si se bloqueara escribiendo
 * mientras el servidor espera que leamos, los dos quedarían esperándose para siempre.
 *
 * <p>El servidor responde las órdenes de cada conexión en el mismo orden en que llegan, por eso
 * alcanza con una cola FIFO de respuestas esperadas para saber a qué orden corresponde cada una.
 */
public class ClienteSms implements AutoCloseable {

    /** Resultado de un ENVIAR aceptado: id asignado y cuánto tardó el "OK ENVIADO". */
    public record Envio(long id, long nanos) {
    }

    /** Confirmación de entrega de un mensaje propio, con el tiempo total desde que se mandó. */
    public record Entrega(long id, String destino, long nanos) {
    }

    /** El servidor respondió "ERR codigo". */
    public static class ErrorDelServidor extends RuntimeException {
        private final String codigo;

        public ErrorDelServidor(String codigo) {
            super("El servidor respondió ERR " + codigo);
            this.codigo = codigo;
        }

        public String codigo() {
            return codigo;
        }
    }

    /** Una respuesta por llegar. Para un ENVIAR se completa "envio"; para lo demás, "respuesta". */
    private record Esperada(CompletableFuture<Protocolo.Respuesta> respuesta, CompletableFuture<Envio> envio,
                            long desde) {
    }

    private static final String FIN = "\u0000fin";

    private final Socket socket;
    private final BufferedReader entrada;
    private final BufferedWriter salida;
    private final Queue<Esperada> esperadas = new ConcurrentLinkedQueue<>();
    private final BlockingQueue<String> porEscribir = new LinkedBlockingQueue<>();
    private final Map<Long, Long> enviadosEn = new ConcurrentHashMap<>();
    private final Semaphore ventana;
    private final CompletableFuture<Void> cerrado = new CompletableFuture<>();
    private final Thread lector;
    private final Thread escritor;

    private volatile Consumer<Protocolo.Mensaje> alRecibir = m -> {
    };
    private volatile Consumer<Entrega> alEntregar = e -> {
    };

    /**
     * @param ventana cuántos ENVIAR pueden estar sin respuesta a la vez (control de flujo, como
     *                la "window size" de SMPP). Si se llena, {@link #enviar} espera.
     */
    public ClienteSms(String host, int puerto, int ventana, Duration timeoutConexion) throws IOException {
        this.ventana = new Semaphore(ventana);
        socket = new Socket();
        socket.connect(new InetSocketAddress(host, puerto), (int) timeoutConexion.toMillis());
        socket.setTcpNoDelay(true); // líneas cortas: que no esperen a juntarse con otras
        entrada = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8));
        salida = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream(), StandardCharsets.UTF_8));

        socket.setSoTimeout((int) timeoutConexion.toMillis());
        final String saludo = entrada.readLine();
        if (saludo == null || !(Protocolo.parsear(saludo) instanceof Protocolo.Saludo)) {
            socket.close();
            throw new IOException("El servidor no saludó como se esperaba: " + saludo);
        }
        socket.setSoTimeout(0);

        lector = new Thread(this::leer, "lector-" + socket.getLocalPort());
        escritor = new Thread(this::escribir, "escritor-" + socket.getLocalPort());
        lector.setDaemon(true);
        escritor.setDaemon(true);
        lector.start();
        escritor.start();
    }

    public void alRecibir(Consumer<Protocolo.Mensaje> accion) {
        alRecibir = accion;
    }

    public void alEntregar(Consumer<Entrega> accion) {
        alEntregar = accion;
    }

    /** Inicia sesión y devuelve cuántos mensajes o avisos tenía guardados. */
    public int login(String numero, Duration timeout) throws IOException {
        try {
            final Protocolo.Respuesta r = ordenar(Protocolo.login(numero)).get(timeout.toMillis(),
                    TimeUnit.MILLISECONDS);
            if (!r.ok())
                throw new ErrorDelServidor(r.contenido());
            return r.pendientesLogin();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrumpido esperando el LOGIN", e);
        } catch (ExecutionException | TimeoutException e) {
            throw new IOException("Sin respuesta al LOGIN", e);
        }
    }

    /** Manda un mensaje. Bloquea solo si la ventana está llena. */
    public CompletableFuture<Envio> enviar(String destino, String texto) throws InterruptedException {
        final String linea = Protocolo.enviar(destino, texto); // valida antes de ocupar la ventana
        ventana.acquire();
        final CompletableFuture<Envio> envio = new CompletableFuture<>();
        encolar(new Esperada(null, envio, System.nanoTime()), linea);
        return envio;
    }

    public CompletableFuture<Void> cerrado() {
        return cerrado;
    }

    @Override
    public void close() {
        porEscribir.offer(FIN);
        try {
            socket.close();
            lector.join(2000);
            escritor.join(2000);
        } catch (IOException e) {
            // ya estaba cerrado
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private CompletableFuture<Protocolo.Respuesta> ordenar(String linea) {
        final CompletableFuture<Protocolo.Respuesta> respuesta = new CompletableFuture<>();
        encolar(new Esperada(respuesta, null, System.nanoTime()), linea);
        return respuesta;
    }

    /* La respuesta esperada y la línea se encolan juntas: así el orden de las respuestas
     * coincide con el orden de escritura aunque varios hilos llamen a la vez. */
    private synchronized void encolar(Esperada esperada, String linea) {
        if (cerrado.isDone()) {
            fallar(esperada, new IOException("Conexión cerrada"));
            return;
        }
        esperadas.add(esperada);
        porEscribir.add(linea);
    }

    private void fallar(Esperada e, Exception causa) {
        if (e.envio() != null) {
            ventana.release();
            e.envio().completeExceptionally(causa);
        } else {
            e.respuesta().completeExceptionally(causa);
        }
    }

    private void escribir() {
        try {
            for (;;) {
                String linea = porEscribir.take();
                do {
                    if (linea == FIN)
                        return;
                    salida.write(linea);
                    salida.write('\n');
                    linea = porEscribir.poll(); // junta lo que ya está en cola en un solo flush
                } while (linea != null);
                salida.flush();
            }
        } catch (IOException | InterruptedException e) {
            terminar(e);
        }
    }

    private void leer() {
        try {
            String linea;
            while ((linea = entrada.readLine()) != null)
                procesar(Protocolo.parsear(linea));
            terminar(new IOException("El servidor cerró la conexión"));
        } catch (IOException | RuntimeException e) {
            terminar(e);
        }
    }

    private void procesar(Protocolo.Linea linea) {
        switch (linea) {
            case Protocolo.Respuesta r -> {
                final Esperada e = esperadas.poll();
                if (e == null)
                    throw new IllegalStateException("Respuesta que nadie esperaba: " + r);
                if (e.envio() == null) {
                    e.respuesta().complete(r);
                } else if (r.ok()) {
                    ventana.release();
                    final long id = r.idEnviado();
                    // Se guarda antes de seguir leyendo: el ENTREGADO de este id llega después en el flujo
                    enviadosEn.put(id, e.desde());
                    e.envio().complete(new Envio(id, System.nanoTime() - e.desde()));
                } else {
                    fallar(e, new ErrorDelServidor(r.contenido()));
                }
            }
            case Protocolo.Mensaje m -> {
                ordenar(Protocolo.ack(m.id()));
                alRecibir.accept(m);
            }
            case Protocolo.Entregado d -> {
                final Long desde = enviadosEn.remove(d.id());
                if (desde != null)
                    alEntregar.accept(new Entrega(d.id(), d.destino(), System.nanoTime() - desde));
            }
            case Protocolo.Saludo s -> {
                // solo llega al conectar
            }
        }
    }

    private void terminar(Exception causa) {
        if (!cerrado.complete(null))
            return;
        porEscribir.offer(FIN);
        Esperada e;
        synchronized (this) { // nadie más encola después de esto: encolar() ve "cerrado"
            while ((e = esperadas.poll()) != null)
                fallar(e, causa);
        }
        try {
            socket.close();
        } catch (IOException ignorada) {
            // nada que hacer
        }
    }
}
