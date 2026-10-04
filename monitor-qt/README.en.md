# Antenna monitoring center (C++ / Qt)

[Versión en español](README.md)

Desktop application that receives antenna telemetry over **TCP** and shows it live: the state of each cell, a signal chart, an alarm log and buttons to send commands. Built with **C++17 and Qt 6** (Widgets, Network and Test) and **CMake**.

It receives data from the [Python simulator](../simulador-antenas/) over a shared [protocol](../PROTOCOLO.md). It is part of a [set of telecom projects](../README.en.md).

![Monitor running with 8 cells](docs/capturas/1-monitor.png)

## What it shows
- **Multithreaded TCP server:** every simulator that connects gets its own `QThread`. The socket is created inside that thread and messages travel to the UI thread through queued signals and slots, with no hand-rolled shared memory.
- **Sockets:** rebuilding lines from the TCP stream (split or merged messages), a 4096-byte line limit, dropping garbage without closing the connection, and commands in the opposite direction (`reiniciar`, `estado`) sent to the simulator that owns that cell.
- **Qt user interface (UI/UX):** custom models (`QAbstractTableModel`) for cells and alarms, colors per state, an RSRP chart drawn with `QPainter`, a summary bar and unacknowledged alarms in bold.
- **Business logic separated from the view:** the model knows nothing about the network or the window, so it is tested on its own.
- **Silence detection:** if a cell stops transmitting for 5 seconds it goes to "Sin datos" (no data), even if the simulator does not report it.
- **QtTest tests:** 18 cases, including one that starts the real server and connects two clients over TCP.

![A cell with interference and another with high temperature](docs/capturas/2-fallas.png)

## Build and run
Requires Qt 6 (on Ubuntu/Debian: `sudo apt install qt6-base-dev cmake g++`).
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Terminal 1: the monitor listens on port 5050
./build/monitor-antenas --puerto 5050

# Terminal 2: the simulator (from ../simulador-antenas)
python3 -m simulador --celdas 8 --intervalo 1 --puerto 5050
```
Selecting a cell shows its values and chart. "Reiniciar celda" (restart cell) turns it off for a few seconds: the `ENLACE_CAIDO` (link down) alarm opens and `ENLACE_RESTABLECIDO` (link restored) follows.

The screenshots in this README were made with `--captura file.png --seleccionar TIGRE-001`, which saves the window and exits (works headless with `xvfb-run`).

The UI and identifiers are in Spanish (the project was built for an Argentine job application).

## Layout
```
src/
  protocolo.*        Validates and builds JSON messages (same protocol as the simulator)
  lectorlineas.*     Rebuilds complete lines from the TCP stream
  modelored.*        Network state: cells, history, opened and closed alarms
  conexioncliente.*  One TCP client, lives in its own thread
  servidortcp.*      QTcpServer: one thread per client and command routing per cell
  grafico.*          RSRP chart with QPainter
  ventana.*          Main window
  main.cpp           Command-line options
tests/test_monitor.cpp  QtTest tests
```

## Tests
```bash
ctest --test-dir build --output-on-failure
# or directly
QT_QPA_PLATFORM=offscreen ./build/pruebas
```
They cover the protocol (including invalid cases), line rebuilding, cell ordering, alarms opening and closing, link down, silent cells, the history limit and a real TCP test with two clients, a split message, garbage and a command routed to the right client.

## Design decisions
| Decision | Why |
|---|---|
| One thread per client with `moveToThread` | If a simulator floods or hangs, it does not freeze the window or the other clients. This is the pattern Qt recommends (instead of subclassing `QThread`). |
| The socket is created in `iniciar()`, not in the constructor | A Qt object belongs to the thread where it is created; a socket born in the main thread would be used from another thread, which Qt forbids. |
| Commands via queued `QMetaObject::invokeMethod` | Writing to the socket from the UI thread is not safe; this way the write runs in the thread that owns the socket. |
| Chart with `QPainter` instead of Qt Charts | Fewer build dependencies and full control over the drawing (the red weak-signal band). |
| The model takes an injectable clock | Silent-cell tests do not have to wait 5 real seconds. |
