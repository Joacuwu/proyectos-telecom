"""Uso: python -m simulador --celdas 8 --intervalo 1 --velocidad 60"""

import argparse
import logging
import signal
import threading
from datetime import datetime

from .simulador import Reloj, Simulador

ZONAS = ["CABA", "MORON", "QUILMES", "TIGRE", "LANUS", "PILAR", "LAPLATA", "SANISIDRO"]


def nombres_celdas(cantidad: int) -> list[str]:
    return [f"{ZONAS[i % len(ZONAS)]}-{i // len(ZONAS) + 1:03d}" for i in range(cantidad)]


def main(argv=None):
    p = argparse.ArgumentParser(prog="simulador", description="Simulador de antenas de telefonía móvil")
    p.add_argument("--host", default="127.0.0.1", help="host del monitor (por defecto 127.0.0.1)")
    p.add_argument("--puerto", type=int, default=5050)
    p.add_argument("--celdas", type=int, default=8, help="cantidad de celdas, una por hilo")
    p.add_argument("--intervalo", type=float, default=1.0, help="segundos entre mediciones de cada celda")
    p.add_argument("--velocidad", type=float, default=1.0,
                   help="aceleración del reloj simulado (60 = un minuto de datos por segundo)")
    p.add_argument("--desde", help="fecha y hora simulada inicial, ej. 2026-10-01T00:00")
    p.add_argument("--db", default="telemetria.db", help="archivo SQLite ('' para no guardar)")
    p.add_argument("--duracion", type=float, default=0, help="segundos a ejecutar (0 = hasta Ctrl+C)")
    p.add_argument("--semilla", type=int, help="semilla para resultados reproducibles")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args(argv)

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(threadName)-12s %(levelname)-7s %(message)s", datefmt="%H:%M:%S")

    if not 1 <= args.celdas <= 500:
        p.error("--celdas debe estar entre 1 y 500")
    reloj = Reloj(datetime.fromisoformat(args.desde) if args.desde else None, args.velocidad)
    sim = Simulador(nombres_celdas(args.celdas), args.host, args.puerto, args.intervalo,
                    args.db or None, reloj, args.semilla).iniciar()

    fin = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: fin.set())
    signal.signal(signal.SIGTERM, lambda *_: fin.set())
    fin.wait(args.duracion or None)

    sim.detener_todo()
    total = sum(c.mediciones for c in sim.celdas.values())
    print(f"\n{total} mediciones generadas · {sim.conexion.enviados} mensajes enviados · "
          f"{sim.conexion.perdidos} sin conexión" + (f" · {sim.almacen.guardados} guardados en {args.db}" if sim.almacen else ""))


if __name__ == "__main__":
    main()
