package cargasms;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

class ProtocoloTest {

    @Test
    void parseaCadaTipoDeLinea() {
        assertInstanceOf(Protocolo.Saludo.class, Protocolo.parsear("HOLA servidor-sms 1.0"));
        assertEquals(new Protocolo.Mensaje(7, "1130979092", "Hola, ¿todo bien?"),
                Protocolo.parsear("MSG 7 1130979092 Hola, ¿todo bien?"));
        assertEquals(new Protocolo.Entregado(7, "1155554444"), Protocolo.parsear("ENTREGADO 7 1155554444"));

        final var enviado = (Protocolo.Respuesta) Protocolo.parsear("OK ENVIADO 12");
        assertTrue(enviado.ok());
        assertEquals(12, enviado.idEnviado());
        assertEquals(3, ((Protocolo.Respuesta) Protocolo.parsear("OK LOGIN 1130979092 3")).pendientesLogin());

        final var error = (Protocolo.Respuesta) Protocolo.parsear("ERR COLA_LLENA");
        assertFalse(error.ok());
        assertEquals("COLA_LLENA", error.contenido());
        assertTrue(((Protocolo.Respuesta) Protocolo.parsear("PONG")).ok());
    }

    @ParameterizedTest
    @ValueSource(strings = {"", "CUALQUIERA", "MSG 1 solo-origen", "ENTREGADO 1", "MSG x 1 hola"})
    void rechazaLineasRaras(String linea) {
        assertThrows(IllegalArgumentException.class, () -> Protocolo.parsear(linea));
    }

    @Test
    void armaOrdenesYValida() {
        assertEquals("ENVIAR +5491130979092 hola", Protocolo.enviar("+5491130979092", "hola"));
        assertEquals("ACK 5", Protocolo.ack(5));
        assertThrows(IllegalArgumentException.class, () -> Protocolo.enviar("11-3097", "hola"));
        assertThrows(IllegalArgumentException.class, () -> Protocolo.enviar("1130979092", "dos\nlíneas"));
        assertThrows(IllegalArgumentException.class, () -> Protocolo.enviar("1130979092", ""));
        assertThrows(IllegalArgumentException.class, () -> Protocolo.login("12"));
    }

    @Test
    void elLimiteEsDe160CaracteresNoDeChars() {
        // Un emoji son 2 "char" en Java pero 1 carácter: 160 emojis tienen que entrar
        final String emojis = "😀".repeat(160);
        assertEquals(320, emojis.length());
        Protocolo.enviar("1130979092", emojis);
        assertThrows(IllegalArgumentException.class, () -> Protocolo.enviar("1130979092", emojis + "!"));
    }
}
