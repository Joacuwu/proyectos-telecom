# Proyectos de telecomunicaciones

[English version](README.en.md)

Cuatro proyectos chicos pero completos, en cuatro lenguajes, alrededor de una red de telefonía móvil simulada. Cada uno tiene su README, sus pruebas y se puede compilar y correr por separado.

| Proyecto | Lenguaje | Qué hace | Temas |
|---|---|---|---|
| [simulador-antenas](simulador-antenas/) | Python | Simula antenas que miden señal, usuarios y temperatura, y mandan alarmas por TCP | hilos, colas, sockets, SQLite, SQL |
| [monitor-qt](monitor-qt/) | C++ / Qt 6 | Centro de monitoreo con interfaz gráfica que recibe la telemetría en vivo | Qt Widgets, servidor TCP multihilo, señales y slots, QtTest |
| [servidor-mensajes-c](servidor-mensajes-c/) | C | Central de mensajes tipo SMS para Linux | sockets POSIX, pthreads, Makefile, valgrind, sanitizers |
| [cliente-carga-java](cliente-carga-java/) | Java | Prueba de carga del servidor en C con cientos de clientes | concurrencia, sockets, JDBC, Maven, JUnit |

```
 simulador-antenas (Python) ──TCP, JSON por línea──▶ monitor-qt (C++/Qt)
                            ◀──── comandos ─────────

 cliente-carga-java (Java) ──TCP, texto por línea──▶ servidor-mensajes-c (C)
```
Protocolos: [telemetría](PROTOCOLO.md) y [mensajes](servidor-mensajes-c/PROTOCOLO_SMS.md).

## Probar todo en Linux
```bash
sudo apt install build-essential cmake qt6-base-dev python3 openjdk-21-jdk maven

# Telemetría: monitor y simulador en dos terminales
cmake -S monitor-qt -B monitor-qt/build && cmake --build monitor-qt/build -j
./monitor-qt/build/monitor-antenas
cd simulador-antenas && python3 -m simulador --celdas 8

# Mensajes: servidor y prueba de carga en dos terminales
make -C servidor-mensajes-c && ./servidor-mensajes-c/build/servidor-sms
cd cliente-carga-java && mvn -q package && java -jar target/cliente-carga-sms-1.0.0.jar
```
