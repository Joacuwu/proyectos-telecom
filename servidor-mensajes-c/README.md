# Servidor de mensajes en C (tipo gateway de SMS)

[English version](README.en.md)

Servidor TCP para Linux que hace de central de mensajes: los clientes se identifican con un número de teléfono, se mandan textos de hasta 160 caracteres y reciben la confirmación de entrega, como un SMS. Si el destino no está conectado, el mensaje queda guardado y se entrega cuando vuelve. Escrito en **C11 con sockets POSIX y pthreads**, sin librerías externas, con **Makefile**, pruebas propias, **valgrind** y sanitizers.

Para medirlo bajo carga está el [cliente en Java](../cliente-carga-java/), que conecta cientos de clientes a la vez.

## Qué muestra
- **Sockets POSIX:** `socket`, `bind`, `listen`, `accept`, `recv`, `send` con `MSG_NOSIGNAL`, `SO_REUSEADDR`, timeout de escritura (`SO_SNDTIMEO`) y armado de líneas desde el flujo TCP.
- **Multithreading con pthreads:** un hilo acepta conexiones y cada cliente tiene su hilo. El estado compartido se protege con un mutex y cada socket tiene su propio mutex de escritura. Hay un orden de bloqueo fijo para que no haya deadlocks.
- **Memoria y vida de los objetos:** cada conexión tiene un contador de referencias, así un hilo nunca escribe en un descriptor que el sistema ya le dio a otra conexión.
- **Cierre ordenado:** Ctrl+C se atiende con `sigwait()` en el hilo principal, se despiertan los hilos bloqueados con `shutdown()` y se espera a que terminen todos (`pthread_join`), sin pérdidas de memoria.
- **Lógica de mensajería:** cola de pendientes en orden de llegada, entrega "al menos una vez" (si el destino se va sin confirmar, se le reenvía), avisos de entrega para remitentes desconectados y límites de clientes y de mensajes guardados.
- **Calidad:** compila sin advertencias con `-Wall -Wextra -Wpedantic -Wshadow`, y las pruebas pasan limpias con valgrind, ThreadSanitizer y AddressSanitizer.

## Cómo compilar y usar
Requiere `gcc` y `make` (en Ubuntu/Debian: `sudo apt install build-essential`).
```bash
make
./build/servidor-sms --puerto 6060 -v
```
Se puede probar a mano con `nc` desde dos terminales:
```
$ nc localhost 6060                         $ nc localhost 6060
HOLA servidor-sms 1.0                       HOLA servidor-sms 1.0
LOGIN 1130979092                            
OK LOGIN 1130979092 0                       
ENVIAR 1155554444 Hola, ¿llegó?             
OK ENVIADO 1                                LOGIN 1155554444
                                            OK LOGIN 1155554444 1
                                            MSG 1 1130979092 Hola, ¿llegó?
                                            ACK 1
ENTREGADO 1 1155554444                      OK ACK 1
```
El protocolo completo está en [PROTOCOLO_SMS.md](PROTOCOLO_SMS.md).

## Pruebas
```bash
make test       # 111 chequeos: protocolo, armado de líneas e integración por TCP
make valgrind   # pérdidas de memoria y accesos inválidos
make tsan       # condiciones de carrera entre hilos
make asan       # desbordes y comportamiento indefinido
```
Las pruebas de integración levantan el servidor real en un puerto libre y cubren: errores de cada orden, entrega con confirmación, mensajes guardados para desconectados, reenvío de lo no confirmado, avisos para remitentes desconectados, líneas y textos demasiado largos, límites, cierre con clientes conectados y una prueba con 20 clientes que se mandan 4000 mensajes a la vez.

## Un error que encontraron las pruebas
Con ThreadSanitizer, que hace todo más lento y cambia los tiempos, la prueba de carga fallaba de vez en cuando: un cliente recibía 199 de 200 mensajes. La causa era una carrera real: apenas un número quedaba registrado, otro hilo podía mandarle un `MSG` **antes** de que su propio hilo le respondiera `OK LOGIN`, y el cliente tomaba ese `MSG` como la respuesta al login. Se resolvió tomando el mutex de escritura de la conexión durante todo el login, así `OK LOGIN` siempre sale primero. Quedó documentado como garantía del protocolo.

## Estructura
```
src/
  protocolo.c/.h   Interpreta cada orden y valida números y textos (160 caracteres UTF-8)
  lector.c/.h      Arma líneas completas desde el flujo TCP
  servidor.c/.h    Sockets, hilos, conexiones, cola de mensajes y avisos
  main.c           Opciones y manejo de Ctrl+C
tests/
  minitest.h       Mini framework de pruebas (sin dependencias)
  test_protocolo.c Pruebas unitarias
  test_servidor.c  Pruebas de integración por TCP
```

## Decisiones
| Decisión | Por qué |
|---|---|
| Un hilo por cliente | Es el modelo más simple de leer y depurar, y con el límite de clientes alcanza para miles de conexiones. Para decenas de miles convendría `epoll` con pocos hilos. |
| Un solo mutex para el estado compartido | Con un solo candado no hay deadlocks posibles entre listas. Las escrituras a sockets, que son lo lento, se hacen fuera de ese mutex. |
| Contador de referencias por conexión | Si un hilo está escribiéndole a un cliente que se desconecta, el descriptor no se cierra hasta que termina; si no, podría escribir en la conexión de otra persona. |
| Timeout de escritura de 2 segundos | Un cliente que no lee no puede trabar al hilo que le escribe. Si no responde, se lo desconecta y sus mensajes vuelven a la cola. |
| Protocolo de texto | Se prueba con `nc`, se lee en cualquier lenguaje y los errores se entienden mirando el tráfico. |
| Mensajes en memoria | Mantiene el proyecto enfocado en sockets e hilos. El paso siguiente sería guardarlos en SQLite o en un archivo de registro para no perderlos si se reinicia. |
