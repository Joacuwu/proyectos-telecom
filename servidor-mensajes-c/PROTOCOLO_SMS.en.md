# Message server protocol

[Versión en español](PROTOCOLO_SMS.md)

TCP, port **6060** by default. UTF-8 text, **one command per line** ending in `\n` (`\r\n` is accepted, so it can be tried with `nc` or `telnet`). Lines over 512 bytes are dropped with `ERR LINEA_LARGA` and the connection stays open.

On connect, the server greets with `HOLA servidor-sms 1.0`. Commands are Spanish words (the project was built for an Argentine job application).

## Client commands

| Command | Reply | Notes |
|---|---|---|
| `LOGIN <number>` | `OK LOGIN <number> <pending>` | Number: optional `+` and 3 to 15 digits. Pending items follow. |
| `ENVIAR <to> <text>` (send) | `OK ENVIADO <id>` | Up to 160 characters (not bytes). If the recipient is offline, it is stored. |
| `ACK <id>` | `OK ACK <id>` | Confirms `MSG <id>` was received. |
| `PING` | `PONG` | |
| `ESTADO` (status) | `OK ESTADO conectados=N pendientes=N recibidos=N entregados=N` | |
| `SALIR` (quit) | `OK CHAU` | The server closes the connection. |

## Unsolicited lines

| Line | When |
|---|---|
| `MSG <id> <from> <text>` | Someone sent you a message. Reply `ACK <id>`. |
| `ENTREGADO <id> <to>` (delivered) | The recipient confirmed your message (like an SMS delivery report). |

## Errors
`ERR <code>`: `COMANDO_DESCONOCIDO` (unknown command), `FALTAN_DATOS` (missing data), `NUMERO_INVALIDO`, `TEXTO_LARGO` (text too long), `TEXTO_INVALIDO` (control characters), `ID_INVALIDO`, `ID_DESCONOCIDO` (unknown id), `SIN_LOGIN` (not logged in), `YA_LOGUEADO` (already logged in), `NUMERO_EN_USO` (number in use), `COLA_LLENA` (queue full), `LINEA_LARGA` (line too long), `SERVIDOR_LLENO` (server full, connection closed), `INTERNO` (internal).

## Guarantees
- **At least once:** a message is deleted only when its `ACK` arrives. If the recipient disconnects without confirming it, it is sent again on the next `LOGIN` (it may arrive twice; the client recognizes it by `id`).
- **Order:** pending messages are delivered in arrival order. Messages from different senders may interleave.
- `OK LOGIN` always arrives before any `MSG`.

## Example
```
C: LOGIN 1130979092            S: OK LOGIN 1130979092 0
C: ENVIAR 1155554444 Hola      S: OK ENVIADO 1
                 (the other client gets "MSG 1 1130979092 Hola" and replies "ACK 1")
                               S: ENTREGADO 1 1155554444
```
