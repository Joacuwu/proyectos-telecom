# Cliente de carga en Java para el servidor de mensajes

[English version](README.en.md)

Herramienta de línea de comandos que conecta muchos "teléfonos" al [servidor de mensajes en C](../servidor-mensajes-c/) y los hace mandarse mensajes entre sí al mismo tiempo. Mide cuánto tarda el servidor en aceptar cada mensaje y cuánto tarda en llegar la confirmación de entrega (p50, p95, p99), y guarda cada corrida en **SQLite por JDBC** para comparar. Hecho en **Java 21** con **Maven** y **JUnit 5**.

## Qué muestra
- **Concurrencia en Java:** `ExecutorService` para los hilos que mandan, `CompletableFuture` para cada respuesta, `CountDownLatch` para esperar las entregas, `Semaphore` como ventana de mensajes sin respuesta, `LongAdder` y `ConcurrentHashMap` para contar sin bloquear.
- **Sockets:** cada conexión tiene un hilo que lee y otro que escribe. El que lee nunca escribe directamente: si se trabara escribiendo mientras el servidor espera que leamos, los dos quedarían esperándose para siempre (deadlock distribuido).
- **Control de flujo:** la ventana limita cuántos mensajes puede tener cada conexión sin respuesta, como la "window size" del protocolo SMPP que usan las operadoras para SMS.
- **Bases de datos:** JDBC con `PreparedStatement`, claves generadas y consultas con `GROUP BY` para ver cómo escala el servidor según la cantidad de clientes.
- **Java moderno:** `record`, interfaces `sealed` y `switch` con patrones para representar las líneas del protocolo como objetos en vez de texto.
- **Pruebas:** 18 pruebas con JUnit 5, incluido un servidor falso en Java para probar el cliente sin depender del binario en C, y una prueba opcional contra el servidor real.

## Cómo usarlo
Requiere Java 21 y Maven.
```bash
mvn package                        # compila, corre las pruebas y arma el .jar

# Con el servidor en C corriendo (../servidor-mensajes-c: make && ./build/servidor-sms)
java -jar target/cliente-carga-sms-1.0.0.jar --clientes 100 --mensajes 200
java -jar target/cliente-carga-sms-1.0.0.jar --historial
```
Opciones: `--host`, `--puerto`, `--clientes`, `--mensajes` (por cliente), `--hilos`, `--ventana`, `--espera` (segundos), `--semilla`, `--db` y `--historial`. `--help` las explica.

## Resultados de ejemplo
Medido en una máquina Linux de 4 núcleos, con el servidor y el cliente en la misma máquina:
```
Conectando 100 clientes a 127.0.0.1:6080, 20000 mensajes en total...

Duración: 0,68 s
Aceptados: 20000 · entregados: 20000 · errores: 0 · recibidos: 20000
Rendimiento: 29480 mensajes entregados por segundo

Tiempos (ms)              promedio      p50      p95      p99   máximo
Aceptación (OK ENVIADO)       3,96     3,13     9,54    16,19    62,47
Entrega (ENTREGADO)          19,47    15,71    44,62    70,97   138,74
```
La "entrega" incluye el viaje completo: el remitente manda, el servidor reenvía al destino, el destino confirma con `ACK` y el servidor avisa al remitente. Comparación por cantidad de clientes: [docs/ejemplo-historial.txt](docs/ejemplo-historial.txt).

## Pruebas
```bash
mvn test
# También contra el servidor en C, si está corriendo en el puerto 6060:
SERVIDOR_SMS_PUERTO=6060 mvn test
```

## Estructura
```
src/main/java/cargasms/
  Protocolo.java          Líneas del protocolo como records (Saludo, Respuesta, Mensaje, Entregado)
  ClienteSms.java         Una conexión: hilo lector, hilo escritor, ventana y tiempos
  PruebaDeCarga.java      Conecta los clientes, reparte el trabajo y junta los resultados
  Estadisticas.java       Tiempos desde varios hilos y percentiles
  HistorialCorridas.java  Guarda y consulta corridas en SQLite (JDBC)
  Main.java               Opciones y reporte
src/test/java/cargasms/   Pruebas JUnit 5 y un servidor falso para probar el cliente
```

## Decisiones
| Decisión | Por qué |
|---|---|
| Cola de respuestas esperadas en orden (FIFO) | El servidor responde las órdenes de cada conexión en el orden en que llegan. Encolar la respuesta esperada y la línea en el mismo bloque `synchronized` garantiza que coincidan aunque manden varios hilos. |
| El tiempo de entrega se registra en el hilo lector | El `ENTREGADO` de un mensaje llega después de su `OK ENVIADO` en el mismo flujo. Guardando el id en el mismo hilo que lee, nunca llega una confirmación de un id todavía desconocido. |
| Percentiles y no solo promedio | El promedio esconde los casos lentos: lo que nota un usuario es el p95 o p99. |
| Prefijo de número por corrida | Si una corrida anterior dejó mensajes guardados, no se mezclan con la nueva. |
| Servidor falso en las pruebas | Las pruebas del cliente corren en cualquier máquina con Maven, sin compilar el servidor en C. |
