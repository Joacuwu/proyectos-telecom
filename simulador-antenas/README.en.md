# Mobile Network Cell Simulator

[Versión en español](README.md)

Simulates a network of cells (LTE antennas) that measure signal, users, traffic and temperature, detect alarms and send everything over **TCP** to a monitoring center. Built with **Python 3.10+ and no external libraries**: `socket`, `threading`, `queue`, `sqlite3`, `argparse` and `unittest`. Code identifiers and messages are in Spanish.

It works together with the [C++/Qt monitor](../monitor-qt/): both speak the same [protocol](../PROTOCOLO.md). It is part of a [set of telecom projects](../README.en.md).

## What it shows
- **Multithreading:** one thread per cell, one thread that keeps the connection alive and reads commands, and one thread that writes to the database. Shared state is protected with `Lock`, and threads communicate through a `Queue` (producer-consumer).
- **TCP sockets:** one-line JSON messages, reassembly of split or merged messages (TCP is a stream, not packets), reconnection with exponential backoff if the monitor goes down, and commands flowing the other way (`reiniciar` = restart, `estado` = status).
- **Databases and SQL:** SQLite with indexes, batched writes inside a transaction, and reports using `GROUP BY`, `HAVING`, `LEFT JOIN`, `CASE` and `strftime`.
- **Believable data:** load follows the time of day (peaks at 13:00 and 20:00), signal drifts gradually, and cooling failures and interference open and close alarms.

## Usage
```bash
# Terminal 1: a simple console monitor (or the Qt monitor)
python3 -m simulador.monitor_consola --puerto 5050

# Terminal 2: 8 cells, one measurement per second
python3 -m simulador --celdas 8 --intervalo 1

# One day of data in 24 seconds (simulated clock 3600x faster) and reports
python3 -m simulador --celdas 8 --intervalo 0.05 --velocidad 3600 --desde 2026-10-01T00:00 --duracion 24 --semilla 42
python3 -m simulador.reportes telemetria.db
```
In the monitor console, type `reiniciar CABA-001`: the cell stops transmitting, raises `ENLACE_CAIDO` (link down) and after 5 seconds `ENLACE_RESTABLECIDO` (link restored).

Sample report: [docs/reporte-ejemplo.txt](docs/reporte-ejemplo.txt).

## Structure
```
simulador/
  protocolo.py        Encoding, validation and line reassembly from the TCP stream
  celda.py            Antenna model: measurements, failures and alarms
  conexion.py         TCP client: thread-safe sending, command reading, reconnection
  almacen.py          SQLite storage with a writer thread and a queue
  simulador.py        Runs one thread per cell and answers commands
  monitor_consola.py  Reference multithreaded TCP server
  reportes.py         SQL queries
tests/                12 tests: protocol, model, SQLite concurrency and TCP integration
```

## Tests
```bash
python3 -m unittest discover -s tests -v
```
They include two real TCP integration tests: the simulator connects, sends, receives a restart command and raises the link alarms; and another where the monitor restarts and the simulator reconnects on its own.

## Design decisions
| Decision | Why |
|---|---|
| A single SQLite writer thread | SQLite locks the database on every write; several writer threads cause `database is locked` errors. The queue avoids that and allows batched writes. |
| JSON per line | Easy to read from Python, C++ and C, and easy to debug with `nc` or `telnet`. |
| Alarms only when they open and close | Reporting on every measurement would flood the operator with identical alarms. |
| Configurable seed | Tests and demos always produce the same data. |
