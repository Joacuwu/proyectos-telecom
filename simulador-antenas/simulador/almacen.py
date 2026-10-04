"""Guarda mediciones y alarmas en SQLite.

SQLite no admite bien escrituras desde muchos hilos a la vez, así que los hilos de las celdas
dejan los registros en una cola y un único hilo escritor los guarda en lotes (patrón productor-consumidor).
"""

import queue
import sqlite3
import threading

ESQUEMA = """
CREATE TABLE IF NOT EXISTS celdas (
    nombre TEXT PRIMARY KEY
);
CREATE TABLE IF NOT EXISTS mediciones (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    celda        TEXT    NOT NULL REFERENCES celdas(nombre),
    ts           TEXT    NOT NULL,
    rsrp         REAL    NOT NULL,
    usuarios     INTEGER NOT NULL,
    trafico_mbps REAL    NOT NULL,
    temperatura  REAL    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_mediciones_celda_ts ON mediciones (celda, ts);
CREATE TABLE IF NOT EXISTS alarmas (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    celda     TEXT NOT NULL REFERENCES celdas(nombre),
    ts        TEXT NOT NULL,
    codigo    TEXT NOT NULL,
    severidad TEXT NOT NULL,
    detalle   TEXT
);
CREATE INDEX IF NOT EXISTS idx_alarmas_ts ON alarmas (ts);
"""

_FIN = object()


def conectar(ruta: str) -> sqlite3.Connection:
    con = sqlite3.connect(ruta)
    con.executescript(ESQUEMA)
    return con


class Almacen:
    def __init__(self, ruta: str, tam_lote: int = 50):
        self.ruta = ruta
        self.tam_lote = tam_lote
        self._cola: queue.Queue = queue.Queue()
        self._hilo = threading.Thread(target=self._escribir, name="almacen", daemon=True)
        self.guardados = 0
        self.error = None

    def iniciar(self, celdas: list[str]) -> "Almacen":
        con = conectar(self.ruta)
        with con:
            con.executemany("INSERT OR IGNORE INTO celdas (nombre) VALUES (?)", [(c,) for c in celdas])
        con.close()
        self._hilo.start()
        return self

    def guardar(self, mensaje: dict) -> None:
        """Se puede llamar desde cualquier hilo."""
        if mensaje.get("tipo") in ("medicion", "alarma"):
            self._cola.put(mensaje)

    def cerrar(self, espera: float = 5.0) -> None:
        self._cola.put(_FIN)
        self._hilo.join(espera)

    def _escribir(self):
        con = conectar(self.ruta)
        try:
            terminar = False
            while not terminar:
                lote = [self._cola.get()]
                # Junta lo que ya esté en la cola para guardar en una sola transacción
                while len(lote) < self.tam_lote:
                    try:
                        lote.append(self._cola.get_nowait())
                    except queue.Empty:
                        break
                if _FIN in lote:
                    terminar = True
                    lote = [m for m in lote if m is not _FIN]
                self._guardar_lote(con, lote)
        except sqlite3.Error as e:
            self.error = e
        finally:
            con.close()

    def _guardar_lote(self, con, lote):
        mediciones = [(m["celda"], m["ts"], m["rsrp"], m["usuarios"], m["trafico_mbps"], m["temperatura"])
                      for m in lote if m["tipo"] == "medicion"]
        alarmas = [(m["celda"], m["ts"], m["codigo"], m["severidad"], m.get("detalle"))
                   for m in lote if m["tipo"] == "alarma"]
        with con:  # una transacción por lote: mucho más rápido que un commit por fila
            con.executemany("INSERT INTO mediciones (celda, ts, rsrp, usuarios, trafico_mbps, temperatura) "
                            "VALUES (?, ?, ?, ?, ?, ?)", mediciones)
            con.executemany("INSERT INTO alarmas (celda, ts, codigo, severidad, detalle) VALUES (?, ?, ?, ?, ?)",
                            alarmas)
        self.guardados += len(lote)
