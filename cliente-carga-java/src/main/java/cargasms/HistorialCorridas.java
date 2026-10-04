package cargasms;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;

/** Guarda cada corrida en SQLite (JDBC) para comparar el rendimiento entre cambios del servidor. */
public class HistorialCorridas implements AutoCloseable {

    public record Fila(long id, String fecha, int clientes, int mensajes, double porSegundo, double p95Entrega,
                       double p99Entrega, long errores) {
    }

    public record PorClientes(int clientes, int corridas, double porSegundoPromedio, double p95Promedio,
                              double p95Mejor) {
    }

    private final Connection conexion;

    public HistorialCorridas(String archivo) throws SQLException {
        conexion = DriverManager.getConnection("jdbc:sqlite:" + archivo);
        try (Statement st = conexion.createStatement()) {
            st.execute("""
                    CREATE TABLE IF NOT EXISTS corrida (
                        id               INTEGER PRIMARY KEY AUTOINCREMENT,
                        fecha            TEXT    NOT NULL,
                        servidor         TEXT    NOT NULL,
                        clientes         INTEGER NOT NULL,
                        mensajes         INTEGER NOT NULL,
                        hilos            INTEGER NOT NULL,
                        ventana          INTEGER NOT NULL,
                        duracion_ms      INTEGER NOT NULL,
                        entregados       INTEGER NOT NULL,
                        errores          INTEGER NOT NULL,
                        por_segundo      REAL    NOT NULL,
                        p50_aceptacion   REAL    NOT NULL,
                        p95_aceptacion   REAL    NOT NULL,
                        p50_entrega      REAL    NOT NULL,
                        p95_entrega      REAL    NOT NULL,
                        p99_entrega      REAL    NOT NULL,
                        max_entrega      REAL    NOT NULL
                    )""");
            st.execute("CREATE INDEX IF NOT EXISTS idx_corrida_clientes ON corrida (clientes)");
        }
    }

    public long guardar(PruebaDeCarga.Resultado r) throws SQLException {
        final String sql = """
                INSERT INTO corrida (fecha, servidor, clientes, mensajes, hilos, ventana, duracion_ms, entregados,
                                     errores, por_segundo, p50_aceptacion, p95_aceptacion, p50_entrega, p95_entrega,
                                     p99_entrega, max_entrega)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""";
        try (PreparedStatement ps = conexion.prepareStatement(sql, Statement.RETURN_GENERATED_KEYS)) {
            final PruebaDeCarga.Config c = r.config();
            ps.setString(1, LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")));
            ps.setString(2, c.host() + ":" + c.puerto());
            ps.setInt(3, c.clientes());
            ps.setInt(4, c.total());
            ps.setInt(5, c.hilos());
            ps.setInt(6, c.ventana());
            ps.setLong(7, r.duracion().toMillis());
            ps.setLong(8, r.entregados());
            ps.setLong(9, r.errores());
            ps.setDouble(10, r.entregadosPorSegundo());
            ps.setDouble(11, r.aceptacion().p50());
            ps.setDouble(12, r.aceptacion().p95());
            ps.setDouble(13, r.entrega().p50());
            ps.setDouble(14, r.entrega().p95());
            ps.setDouble(15, r.entrega().p99());
            ps.setDouble(16, r.entrega().maximo());
            ps.executeUpdate();
            try (ResultSet claves = ps.getGeneratedKeys()) {
                return claves.next() ? claves.getLong(1) : -1;
            }
        }
    }

    public List<Fila> ultimas(int cantidad) throws SQLException {
        final List<Fila> filas = new ArrayList<>();
        try (PreparedStatement ps = conexion.prepareStatement("""
                SELECT id, fecha, clientes, mensajes, por_segundo, p95_entrega, p99_entrega, errores
                FROM corrida ORDER BY id DESC LIMIT ?""")) {
            ps.setInt(1, cantidad);
            try (ResultSet rs = ps.executeQuery()) {
                while (rs.next())
                    filas.add(new Fila(rs.getLong(1), rs.getString(2), rs.getInt(3), rs.getInt(4), rs.getDouble(5),
                            rs.getDouble(6), rs.getDouble(7), rs.getLong(8)));
            }
        }
        return filas;
    }

    /** Cómo escala el servidor: promedios por cantidad de clientes, solo corridas sin errores. */
    public List<PorClientes> porCantidadDeClientes() throws SQLException {
        final List<PorClientes> filas = new ArrayList<>();
        try (Statement st = conexion.createStatement();
             ResultSet rs = st.executeQuery("""
                     SELECT clientes, COUNT(*), AVG(por_segundo), AVG(p95_entrega), MIN(p95_entrega)
                     FROM corrida
                     WHERE errores = 0
                     GROUP BY clientes
                     ORDER BY clientes""")) {
            while (rs.next())
                filas.add(new PorClientes(rs.getInt(1), rs.getInt(2), rs.getDouble(3), rs.getDouble(4),
                        rs.getDouble(5)));
        }
        return filas;
    }

    @Override
    public void close() throws SQLException {
        conexion.close();
    }
}
