package cargasms;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

class HistorialCorridasTest {

    private static PruebaDeCarga.Resultado resultado(int clientes, double p95, long errores) {
        final var config = new PruebaDeCarga.Config("127.0.0.1", 6060, clientes, 10, 4, 50, Duration.ofSeconds(5), 1);
        final var resumen = new Estadisticas.Resumen(10, 1, 1, p95, p95, p95);
        return new PruebaDeCarga.Resultado(config, config.total(), config.total() - errores, errores, config.total(),
                Duration.ofSeconds(1), resumen, resumen);
    }

    @Test
    void guardaYAgrupaPorClientes(@TempDir Path dir) throws Exception {
        final String archivo = dir.resolve("prueba.db").toString();
        try (HistorialCorridas h = new HistorialCorridas(archivo)) {
            h.guardar(resultado(10, 2.0, 0));
            h.guardar(resultado(10, 4.0, 0));
            h.guardar(resultado(50, 8.0, 0));
            h.guardar(resultado(50, 99.0, 3)); // con errores: no cuenta en el agrupado
        }
        // Se reabre para comprobar que quedó guardado en el archivo
        try (HistorialCorridas h = new HistorialCorridas(archivo)) {
            final List<HistorialCorridas.Fila> ultimas = h.ultimas(10);
            assertEquals(4, ultimas.size());
            assertEquals(3, ultimas.get(0).errores()); // la más nueva primero

            final List<HistorialCorridas.PorClientes> grupos = h.porCantidadDeClientes();
            assertEquals(2, grupos.size());
            assertEquals(10, grupos.get(0).clientes());
            assertEquals(2, grupos.get(0).corridas());
            assertEquals(3.0, grupos.get(0).p95Promedio(), 1e-9);
            assertEquals(2.0, grupos.get(0).p95Mejor(), 1e-9);
            assertEquals(1, grupos.get(1).corridas());
        }
    }
}
