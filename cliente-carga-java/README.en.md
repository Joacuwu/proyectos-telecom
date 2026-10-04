# Java load-testing client for the message server

[Versión en español](README.md)

Command-line tool that connects many "phones" to the [C message server](../servidor-mensajes-c/) and has them message each other at the same time. It measures how long the server takes to accept each message and how long the delivery report takes (p50, p95, p99), and stores every run in **SQLite via JDBC** for comparison. Built with **Java 21**, **Maven** and **JUnit 5**.

Identifiers and output are in Spanish (the project was built for an Argentine job application).

## What it shows
- **Java concurrency:** an `ExecutorService` for the sending threads, a `CompletableFuture` per reply, a `CountDownLatch` to wait for deliveries, a `Semaphore` as a window of unanswered messages, and `LongAdder` and `ConcurrentHashMap` for lock-free counting.
- **Sockets:** each connection has one reading thread and one writing thread. The reader never writes directly: if it got stuck writing while the server waits for us to read, both sides would wait forever (a distributed deadlock).
- **Flow control:** the window caps how many unanswered messages each connection may have, like the "window size" in SMPP, the protocol carriers use for SMS.
- **Databases:** JDBC with `PreparedStatement`, generated keys and `GROUP BY` queries to see how the server scales with the number of clients.
- **Modern Java:** `record`, `sealed` interfaces and pattern-matching `switch` to represent protocol lines as objects instead of text.
- **Tests:** 18 JUnit 5 tests, including a fake server written in Java to test the client without the C binary, and an optional test against the real server.

## How to use it
Requires Java 21 and Maven.
```bash
mvn package                        # builds, runs the tests and packages the .jar

# With the C server running (../servidor-mensajes-c: make && ./build/servidor-sms)
java -jar target/cliente-carga-sms-1.0.0.jar --clientes 100 --mensajes 200
java -jar target/cliente-carga-sms-1.0.0.jar --historial
```
Options: `--host`, `--puerto` (port), `--clientes` (clients), `--mensajes` (messages per client), `--hilos` (threads), `--ventana` (window), `--espera` (seconds to wait), `--semilla` (seed), `--db` and `--historial` (history). `--help` explains them.

## Sample results
Measured on a 4-core Linux machine, with server and client on the same host:
```
Conectando 100 clientes a 127.0.0.1:6080, 20000 mensajes en total...

Duración: 0,68 s
Aceptados: 20000 · entregados: 20000 · errores: 0 · recibidos: 20000
Rendimiento: 29480 mensajes entregados por segundo

Tiempos (ms)              promedio      p50      p95      p99   máximo
Aceptación (OK ENVIADO)       3,96     3,13     9,54    16,19    62,47
Entrega (ENTREGADO)          19,47    15,71    44,62    70,97   138,74
```
About 29,500 delivered messages per second. "Entrega" (delivery) is the full round trip: the sender sends, the server forwards to the recipient, the recipient confirms with `ACK` and the server notifies the sender. Comparison by number of clients: [docs/ejemplo-historial.txt](docs/ejemplo-historial.txt).

## Tests
```bash
mvn test
# Also against the C server, if it is running on port 6060:
SERVIDOR_SMS_PUERTO=6060 mvn test
```

## Layout
```
src/main/java/cargasms/
  Protocolo.java          Protocol lines as records (Saludo, Respuesta, Mensaje, Entregado)
  ClienteSms.java         One connection: reader thread, writer thread, window and timings
  PruebaDeCarga.java      Connects the clients, splits the work and collects results
  Estadisticas.java       Timings from many threads and percentiles
  HistorialCorridas.java  Stores and queries runs in SQLite (JDBC)
  Main.java               Options and report
src/test/java/cargasms/   JUnit 5 tests and a fake server for testing the client
```

## Design decisions
| Decision | Why |
|---|---|
| FIFO queue of expected replies | The server answers each connection's commands in arrival order. Queuing the expected reply and the line in the same `synchronized` block guarantees they match even with several sending threads. |
| Delivery timing recorded in the reader thread | A message's `ENTREGADO` arrives after its `OK ENVIADO` in the same stream. Storing the id in the reading thread means a report never arrives for an id that is still unknown. |
| Percentiles, not just the average | The average hides slow cases: what a user notices is the p95 or p99. |
| Per-run number prefix | If a previous run left stored messages, they do not mix with the new one. |
| Fake server in the tests | The client tests run on any machine with Maven, without building the C server. |
