package cargasms;

/**
 * Líneas del protocolo del servidor de mensajes (ver ../servidor-mensajes-c/PROTOCOLO_SMS.md).
 * Cada línea que llega se convierte en un objeto inmutable; así el resto del código no trabaja con texto.
 */
public final class Protocolo {

    public static final int LARGO_MAX_TEXTO = 160;

    private Protocolo() {
    }

    public sealed interface Linea permits Saludo, Respuesta, Mensaje, Entregado {
    }

    /** "HOLA servidor-sms 1.0" al conectarse. */
    public record Saludo(String version) implements Linea {
    }

    /** Respuesta a una orden propia: "OK ...", "PONG" o "ERR codigo". */
    public record Respuesta(boolean ok, String contenido) implements Linea {
        /** Id de "OK ENVIADO id". */
        public long idEnviado() {
            if (!ok || !contenido.startsWith("ENVIADO "))
                throw new IllegalStateException("No es una respuesta a ENVIAR: " + contenido);
            return Long.parseLong(contenido.substring(8));
        }

        /** Cantidad de pendientes de "OK LOGIN numero pendientes". */
        public int pendientesLogin() {
            final String[] partes = contenido.split(" ");
            if (!ok || partes.length != 3 || !partes[0].equals("LOGIN"))
                throw new IllegalStateException("No es una respuesta a LOGIN: " + contenido);
            return Integer.parseInt(partes[2]);
        }
    }

    /** "MSG id origen texto": un mensaje para este cliente. */
    public record Mensaje(long id, String origen, String texto) implements Linea {
    }

    /** "ENTREGADO id destino": el destino confirmó un mensaje que mandamos. */
    public record Entregado(long id, String destino) implements Linea {
    }

    public static Linea parsear(String linea) {
        if (linea.startsWith("HOLA "))
            return new Saludo(linea.substring(5));
        if (linea.equals("PONG"))
            return new Respuesta(true, "PONG");
        if (linea.startsWith("OK "))
            return new Respuesta(true, linea.substring(3));
        if (linea.startsWith("ERR "))
            return new Respuesta(false, linea.substring(4));
        if (linea.startsWith("MSG ")) {
            final String[] partes = linea.split(" ", 4);
            if (partes.length == 4)
                return new Mensaje(Long.parseLong(partes[1]), partes[2], partes[3]);
        }
        if (linea.startsWith("ENTREGADO ")) {
            final String[] partes = linea.split(" ");
            if (partes.length == 3)
                return new Entregado(Long.parseLong(partes[1]), partes[2]);
        }
        throw new IllegalArgumentException("Línea desconocida: " + linea);
    }

    public static String login(String numero) {
        validarNumero(numero);
        return "LOGIN " + numero;
    }

    public static String enviar(String destino, String texto) {
        validarNumero(destino);
        if (texto.isEmpty() || texto.chars().anyMatch(c -> c < 0x20 || c == 0x7F))
            throw new IllegalArgumentException("Texto vacío o con caracteres de control");
        // 160 caracteres, no bytes ni unidades UTF-16: un emoji ocupa dos "char" en Java
        if (texto.codePointCount(0, texto.length()) > LARGO_MAX_TEXTO)
            throw new IllegalArgumentException("El texto supera los " + LARGO_MAX_TEXTO + " caracteres");
        return "ENVIAR " + destino + " " + texto;
    }

    public static String ack(long id) {
        return "ACK " + id;
    }

    private static void validarNumero(String numero) {
        if (!numero.matches("\\+?\\d{3,15}"))
            throw new IllegalArgumentException("Número inválido: " + numero);
    }
}
