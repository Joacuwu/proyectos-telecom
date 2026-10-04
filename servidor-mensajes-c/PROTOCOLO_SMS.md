# Protocolo del servidor de mensajes

[English version](PROTOCOLO_SMS.en.md)

TCP, puerto **6060** por defecto. Texto UTF-8, **una orden por línea** terminada en `\n` (se acepta `\r\n`, así se puede probar con `nc` o `telnet`). Líneas de más de 512 bytes se descartan con `ERR LINEA_LARGA` y la conexión sigue.

Al conectarse, el servidor saluda con `HOLA servidor-sms 1.0`.

## Órdenes del cliente

| Orden | Respuesta | Notas |
|---|---|---|
| `LOGIN <número>` | `OK LOGIN <número> <pendientes>` | Número: `+` opcional y de 3 a 15 dígitos. Después llegan los pendientes. |
| `ENVIAR <destino> <texto>` | `OK ENVIADO <id>` | Hasta 160 caracteres (no bytes). Si el destino no está conectado, se guarda. |
| `ACK <id>` | `OK ACK <id>` | Confirma que se recibió el `MSG <id>`. |
| `PING` | `PONG` | |
| `ESTADO` | `OK ESTADO conectados=N pendientes=N recibidos=N entregados=N` | |
| `SALIR` | `OK CHAU` | El servidor cierra la conexión. |

## Mensajes que llegan sin pedirlos

| Línea | Cuándo |
|---|---|
| `MSG <id> <origen> <texto>` | Alguien te mandó un mensaje. Hay que responder `ACK <id>`. |
| `ENTREGADO <id> <destino>` | El destino confirmó tu mensaje (como el "entregado" de un SMS). |

## Errores
`ERR <código>`: `COMANDO_DESCONOCIDO`, `FALTAN_DATOS`, `NUMERO_INVALIDO`, `TEXTO_LARGO`, `TEXTO_INVALIDO` (caracteres de control), `ID_INVALIDO`, `ID_DESCONOCIDO`, `SIN_LOGIN`, `YA_LOGUEADO`, `NUMERO_EN_USO`, `COLA_LLENA`, `LINEA_LARGA`, `SERVIDOR_LLENO` (y se cierra la conexión), `INTERNO`.

## Garantías
- **Al menos una vez:** un mensaje se borra recién cuando llega su `ACK`. Si el destino se desconecta sin confirmarlo, se le vuelve a mandar en el próximo `LOGIN` (puede llegar repetido; el cliente lo reconoce por el `id`).
- **Orden:** los pendientes se entregan en el orden en que llegaron. Mensajes de remitentes distintos pueden intercalarse.
- `OK LOGIN` siempre llega antes que cualquier `MSG`.

## Ejemplo
```
C: LOGIN 1130979092            S: OK LOGIN 1130979092 0
C: ENVIAR 1155554444 Hola      S: OK ENVIADO 1
                 (el otro cliente recibe "MSG 1 1130979092 Hola" y responde "ACK 1")
                               S: ENTREGADO 1 1155554444
```
