# Telecom projects

[Versión en español](README.md)

Four small but complete projects, in four languages, built around a simulated mobile network. Each one has its own README and tests, and builds and runs on its own. Identifiers and UI text are in Spanish (they were built for an Argentine job application).

| Project | Language | What it does | Topics |
|---|---|---|---|
| [simulador-antenas](simulador-antenas/) | Python | Simulates cell sites that measure signal, users and temperature and send alarms over TCP | threads, queues, sockets, SQLite, SQL |
| [monitor-qt](monitor-qt/) | C++ / Qt 6 | Monitoring center with a GUI that receives live telemetry | Qt Widgets, multithreaded TCP server, signals and slots, QtTest |
| [servidor-mensajes-c](servidor-mensajes-c/) | C | SMS-style message hub for Linux | POSIX sockets, pthreads, Makefile, valgrind, sanitizers |
| [cliente-carga-java](cliente-carga-java/) | Java | Load test for the C server with hundreds of clients | concurrency, sockets, JDBC, Maven, JUnit |

```
 simulador-antenas (Python) ──TCP, JSON lines──▶ monitor-qt (C++/Qt)
                            ◀──── commands ────

 cliente-carga-java (Java) ──TCP, text lines──▶ servidor-mensajes-c (C)
```
Protocols: [telemetry](PROTOCOLO.md) (in Spanish) and [messaging](servidor-mensajes-c/PROTOCOLO_SMS.en.md).

## Try everything on Linux
```bash
sudo apt install build-essential cmake qt6-base-dev python3 openjdk-21-jdk maven

# Telemetry: monitor and simulator in two terminals
cmake -S monitor-qt -B monitor-qt/build && cmake --build monitor-qt/build -j
./monitor-qt/build/monitor-antenas
cd simulador-antenas && python3 -m simulador --celdas 8

# Messaging: server and load test in two terminals
make -C servidor-mensajes-c && ./servidor-mensajes-c/build/servidor-sms
cd cliente-carga-java && mvn -q package && java -jar target/cliente-carga-sms-1.0.0.jar
```
