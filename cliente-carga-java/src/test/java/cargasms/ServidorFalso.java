package cargasms;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Versión mínima del servidor en Java para probar el cliente sin depender del binario en C.
 * Solo entrega a clientes conectados; alcanza para las pruebas.
 */
class ServidorFalso implements AutoCloseable {

    private final ServerSocket escucha = new ServerSocket(0);
    private final Map<String, PrintWriter> conectados = new ConcurrentHashMap<>();
    private final Map<Long, String> remitentes = new ConcurrentHashMap<>();
    private final AtomicLong ids = new AtomicLong();
    /** Si es mayor a 0, responde ERR COLA_LLENA a partir de ese mensaje. */
    volatile long limite;

    ServidorFalso() throws IOException {
        final Thread t = new Thread(this::aceptar, "servidor-falso");
        t.setDaemon(true);
        t.start();
    }

    int puerto() {
        return escucha.getLocalPort();
    }

    private void aceptar() {
        try {
            for (;;) {
                final Socket s = escucha.accept();
                final Thread t = new Thread(() -> atender(s));
                t.setDaemon(true);
                t.start();
            }
        } catch (IOException e) {
            // cerrado
        }
    }

    private void atender(Socket s) {
        String numero = null;
        try (s; BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8));
             PrintWriter out = new PrintWriter(s.getOutputStream(), true, StandardCharsets.UTF_8)) {
            escribir(out, "HOLA servidor-falso");
            String linea;
            while ((linea = in.readLine()) != null) {
                final String[] p = linea.split(" ", 3);
                switch (p[0]) {
                    case "LOGIN" -> {
                        numero = p[1];
                        conectados.put(numero, out);
                        escribir(out, "OK LOGIN " + numero + " 0");
                    }
                    case "ENVIAR" -> {
                        final long id = ids.incrementAndGet();
                        if (limite > 0 && id > limite) {
                            escribir(out, "ERR COLA_LLENA");
                            continue;
                        }
                        remitentes.put(id, numero);
                        escribir(out, "OK ENVIADO " + id);
                        final PrintWriter destino = conectados.get(p[1]);
                        if (destino != null)
                            escribir(destino, "MSG " + id + " " + numero + " " + p[2]);
                    }
                    case "ACK" -> {
                        final long id = Long.parseLong(p[1]);
                        escribir(out, "OK ACK " + id);
                        final PrintWriter origen = conectados.get(remitentes.remove(id));
                        if (origen != null)
                            escribir(origen, "ENTREGADO " + id + " " + numero);
                    }
                    case "PING" -> escribir(out, "PONG");
                    default -> escribir(out, "ERR COMANDO_DESCONOCIDO");
                }
            }
        } catch (IOException e) {
            // el cliente se fue
        } finally {
            if (numero != null)
                conectados.remove(numero);
        }
    }

    /* Como el servidor real: una línea a la vez por conexión aunque escriban varios hilos. */
    private static void escribir(PrintWriter out, String linea) {
        synchronized (out) {
            out.println(linea);
        }
    }

    @Override
    public void close() throws IOException {
        escucha.close();
    }
}
