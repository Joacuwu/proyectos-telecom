package cargasms;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import org.junit.jupiter.api.condition.EnabledIfEnvironmentVariable;

@Timeout(30)
class ClienteSmsTest {

    private static final Duration ESPERA = Duration.ofSeconds(5);

    @Test
    void mandaRecibeConfirmaYMideTiempos() throws Exception {
        try (ServidorFalso servidor = new ServidorFalso();
             ClienteSms a = new ClienteSms("127.0.0.1", servidor.puerto(), 10, ESPERA);
             ClienteSms b = new ClienteSms("127.0.0.1", servidor.puerto(), 10, ESPERA)) {
            final List<Protocolo.Mensaje> recibidos = new CopyOnWriteArrayList<>();
            final CompletableFuture<ClienteSms.Entrega> entrega = new CompletableFuture<>();
            b.alRecibir(recibidos::add);
            a.alEntregar(entrega::complete);
            assertEquals(0, a.login("1111111", ESPERA));
            assertEquals(0, b.login("2222222", ESPERA));

            final ClienteSms.Envio envio = a.enviar("2222222", "hola ñandú").get(5, TimeUnit.SECONDS);
            final ClienteSms.Entrega e = entrega.get(5, TimeUnit.SECONDS);

            assertEquals(envio.id(), e.id());
            assertEquals("2222222", e.destino());
            assertTrue(envio.nanos() > 0 && e.nanos() >= envio.nanos());
            assertEquals(List.of(new Protocolo.Mensaje(envio.id(), "1111111", "hola ñandú")), recibidos);
        }
    }

    @Test
    void unErrorDelServidorFallaSoloEseEnvio() throws Exception {
        try (ServidorFalso servidor = new ServidorFalso();
             ClienteSms a = new ClienteSms("127.0.0.1", servidor.puerto(), 1, ESPERA)) {
            servidor.limite = 1;
            a.login("1111111", ESPERA);
            a.enviar("3333333", "primero").get(5, TimeUnit.SECONDS);
            final var segundo = a.enviar("3333333", "segundo");
            final ExecutionException ex = assertThrows(ExecutionException.class, () -> segundo.get(5, TimeUnit.SECONDS));
            assertEquals("COLA_LLENA", assertInstanceOf(ClienteSms.ErrorDelServidor.class, ex.getCause()).codigo());
            // La ventana es de 1: si el error no la hubiera liberado, este envío quedaría trabado
            assertThrows(ExecutionException.class, () -> a.enviar("3333333", "tercero").get(5, TimeUnit.SECONDS));
        }
    }

    @Test
    void despuesDeCerrarFallanLosEnvios() throws Exception {
        final ServidorFalso servidor = new ServidorFalso();
        try (ClienteSms a = new ClienteSms("127.0.0.1", servidor.puerto(), 10, ESPERA)) {
            a.login("1111111", ESPERA);
            servidor.close();
            a.close();
            assertTrue(a.cerrado().isDone());
            assertThrows(ExecutionException.class, () -> a.enviar("2222222", "nadie").get(5, TimeUnit.SECONDS));
        }
    }

    @Test
    void pruebaDeCargaContraElServidorFalso() throws Exception {
        try (ServidorFalso servidor = new ServidorFalso()) {
            final var config = new PruebaDeCarga.Config("127.0.0.1", servidor.puerto(), 10, 100, 4, 20,
                    Duration.ofSeconds(20), 7);
            final PruebaDeCarga.Resultado r = new PruebaDeCarga(config).ejecutar();
            assertTrue(r.completo(), () -> "Resultado incompleto: " + r);
            assertEquals(1000, r.aceptados());
            assertEquals(1000, r.recibidos());
            assertEquals(1000, r.entrega().cantidad());
        }
    }

    /** Contra el servidor real en C: SERVIDOR_SMS_PUERTO=6060 mvn test */
    @Test
    @EnabledIfEnvironmentVariable(named = "SERVIDOR_SMS_PUERTO", matches = "\\d+")
    void pruebaDeCargaContraElServidorEnC() throws Exception {
        final int puerto = Integer.parseInt(System.getenv("SERVIDOR_SMS_PUERTO"));
        final var config = new PruebaDeCarga.Config("127.0.0.1", puerto, 20, 200, 8, 50, Duration.ofSeconds(20), 7);
        final PruebaDeCarga.Resultado r = new PruebaDeCarga(config).ejecutar();
        assertTrue(r.completo(), () -> "Resultado incompleto: " + r);
    }
}
