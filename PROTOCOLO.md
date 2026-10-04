# Protocolo de telemetría (compartido por los proyectos)

Conexión TCP, por defecto en el puerto **5050**. Cada mensaje es **un objeto JSON en una sola línea** terminado en `\n` (UTF-8). Así se puede leer con cualquier lenguaje con una lectura "por línea", sin calcular largos.

## Del simulador al monitor

**Presentación** (primer mensaje de cada conexión):
```json
{"tipo":"hola","origen":"simulador","celdas":["CABA-001","CABA-002"]}
```

**Medición** (cada celda, cada `intervalo` segundos):
```json
{"tipo":"medicion","celda":"CABA-001","ts":"2026-10-03T12:00:00","rsrp":-95.4,"usuarios":120,"trafico_mbps":45.2,"temperatura":38.5}
```
| Campo | Unidad | Rango normal |
|---|---|---|
| `rsrp` | dBm (potencia de señal de referencia LTE) | -80 a -105 |
| `usuarios` | usuarios conectados | 0 a 600 |
| `trafico_mbps` | Mbps | 0 a 300 |
| `temperatura` | °C del equipo | 25 a 60 |

**Alarma** (cuando una medición cruza un umbral o se cae el enlace):
```json
{"tipo":"alarma","celda":"CABA-001","ts":"2026-10-03T12:00:05","codigo":"SENAL_DEBIL","severidad":"mayor","detalle":"RSRP -112.0 dBm"}
```
Códigos: `SENAL_DEBIL` (rsrp < -110), `TEMPERATURA_ALTA` (> 70 °C), `SATURACION` (> 550 usuarios), `ENLACE_CAIDO`, `ENLACE_RESTABLECIDO`. Cuando una condición vuelve a la normalidad se envía el mismo código con el sufijo `_NORMALIZADA` (por ejemplo `SENAL_DEBIL_NORMALIZADA`). Severidades: `menor`, `mayor`, `critica`.

## Del monitor al simulador

**Comando:**
```json
{"tipo":"comando","celda":"CABA-001","accion":"reiniciar"}
```
Acciones: `reiniciar` (la celda se apaga unos segundos y vuelve) y `estado` (pide una medición inmediata).

**Respuesta a un comando:**
```json
{"tipo":"respuesta","celda":"CABA-001","accion":"reiniciar","ok":true,"detalle":"Reiniciando"}
```

## Reglas
- Un mensaje que no es JSON válido o no tiene `tipo` se descarta y se registra en el log; la conexión sigue.
- Líneas de más de 4096 bytes se descartan (protección contra clientes rotos).
- Si se corta la conexión, el simulador reintenta con espera creciente (1, 2, 4… hasta 30 segundos).
