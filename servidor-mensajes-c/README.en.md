# Message server in C (SMS-gateway style)

[Versión en español](README.md)

Linux TCP server that acts as a message hub: clients identify themselves with a phone number, send each other texts of up to 160 characters and get a delivery report, like an SMS. If the recipient is offline, the message is stored and delivered when they come back. Written in **C11 with POSIX sockets and pthreads**, no external libraries, with a **Makefile**, its own tests, **valgrind** and sanitizers.

The [Java client](../cliente-carga-java/) load-tests it by connecting hundreds of clients at once.

Commands and identifiers are in Spanish (the project was built for an Argentine job application).

## What it shows
- **POSIX sockets:** `socket`, `bind`, `listen`, `accept`, `recv`, `send` with `MSG_NOSIGNAL`, `SO_REUSEADDR`, a write timeout (`SO_SNDTIMEO`) and rebuilding lines from the TCP stream.
- **Multithreading with pthreads:** one thread accepts connections and each client gets its own thread. Shared state is protected by a mutex and each socket has its own write mutex. A fixed lock order prevents deadlocks.
- **Memory and object lifetime:** every connection is reference-counted, so a thread never writes to a descriptor the OS has already handed to another connection.
- **Clean shutdown:** Ctrl+C is handled with `sigwait()` in the main thread, blocked threads are woken with `shutdown()` and all of them are joined (`pthread_join`), with no memory leaks.
- **Messaging logic:** a pending queue in arrival order, "at least once" delivery (if the recipient leaves without confirming, it is resent), delivery reports for offline senders, and limits on clients and stored messages.
- **Quality:** builds with no warnings under `-Wall -Wextra -Wpedantic -Wshadow`, and the tests run clean under valgrind, ThreadSanitizer and AddressSanitizer.

## Build and run
Requires `gcc` and `make` (on Ubuntu/Debian: `sudo apt install build-essential`).
```bash
make
./build/servidor-sms --puerto 6060 -v
```
Try it by hand with `nc` from two terminals:
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
The full protocol is in [PROTOCOLO_SMS.en.md](PROTOCOLO_SMS.en.md).

## Tests
```bash
make test       # 111 checks: protocol, line rebuilding and TCP integration
make valgrind   # memory leaks and invalid accesses
make tsan       # data races between threads
make asan       # overflows and undefined behavior
```
The integration tests start the real server on a free port and cover: errors for each command, delivery with confirmation, messages stored for offline users, resending unconfirmed messages, reports for offline senders, lines and texts that are too long, limits, shutting down with clients connected, and a test where 20 clients send 4000 messages at once.

## A bug the tests found
Under ThreadSanitizer, which slows everything down and shifts timings, the load test failed now and then: one client got 199 of 200 messages. The cause was a real race: as soon as a number was registered, another thread could send it a `MSG` **before** its own thread replied `OK LOGIN`, and the client took that `MSG` as the login reply. The fix holds the connection's write mutex for the whole login, so `OK LOGIN` always goes out first. It is now documented as a protocol guarantee.

## Layout
```
src/
  protocolo.c/.h   Parses each command and validates numbers and texts (160 UTF-8 characters)
  lector.c/.h      Rebuilds complete lines from the TCP stream
  servidor.c/.h    Sockets, threads, connections, message queue and reports
  main.c           Options and Ctrl+C handling
tests/
  minitest.h       Tiny test framework (no dependencies)
  test_protocolo.c Unit tests
  test_servidor.c  TCP integration tests
```

## Design decisions
| Decision | Why |
|---|---|
| One thread per client | The simplest model to read and debug, and with the client limit it handles thousands of connections. For tens of thousands, `epoll` with a few threads would be better. |
| A single mutex for shared state | With one lock there can be no deadlocks between lists. Socket writes, the slow part, happen outside that mutex. |
| Reference count per connection | If a thread is writing to a client that disconnects, the descriptor is not closed until it finishes; otherwise it could write into someone else's connection. |
| 2-second write timeout | A client that does not read cannot stall the thread writing to it. If it does not respond it is disconnected and its messages go back to the queue. |
| Text protocol | Testable with `nc`, readable from any language, and errors are obvious when looking at the traffic. |
| Messages in memory | Keeps the project focused on sockets and threads. The next step would be storing them in SQLite or a log file so a restart does not lose them. |
