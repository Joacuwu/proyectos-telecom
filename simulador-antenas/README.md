# Simulador de antenas de telefonía móvil

[English version](README.en.md)

Simula una red de celdas (antenas LTE) que miden señal, usuarios, tráfico y temperatura, detectan alarmas y las envían por **TCP** a un centro de monitoreo. Hecho en **Python 3.10+ sin librerías externas**: `socket`, `threading`, `queue`, `sqlite3`, `argparse` y `unittest`.

Trabaja junto con el [monitor en C++/Qt](../monitor-qt/): los dos hablan el mismo [protocolo](../PROTOCOLO.md). Es parte de un [conjunto de proyectos de telecomunicaciones](../README.md).

## Qué muestra
- **Multithreading:** un hilo por celda, un hilo que mantiene la conexión y lee comandos, y un hilo que escribe en la base. Los datos compartidos están protegidos con `Lock` y la comunicación entre hilos usa `Queue` (productor-consumidor).
- **Sockets TCP:** mensajes JSON de una línea, armado de mensajes partidos o juntos (TCP es un flujo, no paquetes), reconexión con espera creciente si se cae el monitor y comandos que llegan en sentido contrario (`reiniciar`, `estado`).
- **Bases de datos y SQL:** SQLite con índices, escritura en lotes dentro de una transacción y reportes con `GROUP BY`, `HAVING`, `LEFT JOIN`, `CASE` y `strftime`.
- **Datos creíbles:** la carga sigue la hora del día (picos a las 13 y a las 20), la señal varía de a poco y hay fallas de ventilación e interferencia que abren y cierran alarmas.

## Cómo usarlo
```bash
# Terminal 1: un monitor simple por consola (o el monitor en Qt)
python3 -m simulador.monitor_consola --puerto 5050

# Terminal 2: 8 celdas, una medición por segundo
python3 -m simulador --celdas 8 --intervalo 1

# Un día de datos en 24 segundos (reloj simulado 3600 veces más rápido) y reportes
python3 -m simulador --celdas 8 --intervalo 0.05 --velocidad 3600 --desde 2026-10-01T00:00 --duracion 24 --semilla 42
python3 -m simulador.reportes telemetria.db
```
En la consola del monitor se puede escribir `reiniciar CABA-001`: la celda deja de transmitir, se dispara la alarma `ENLACE_CAIDO` y a los 5 segundos `ENLACE_RESTABLECIDO`.

Ejemplo de reporte: [docs/reporte-ejemplo.txt](docs/reporte-ejemplo.txt).

## Estructura
```
simulador/
  protocolo.py        Codificación, validación y armado de líneas desde el flujo TCP
  celda.py            Modelo de una antena: mediciones, fallas y alarmas
  conexion.py         Cliente TCP: envío seguro entre hilos, lectura de comandos, reconexión
  almacen.py          Guardado en SQLite con un hilo escritor y una cola
  simulador.py        Orquesta un hilo por celda y responde comandos
  monitor_consola.py  Servidor TCP multihilo de referencia
  reportes.py         Consultas SQL
tests/                12 pruebas: protocolo, modelo, concurrencia en SQLite e integración TCP
```

## Pruebas
```bash
python3 -m unittest discover -s tests -v
```
Incluyen dos pruebas de integración reales por TCP: el simulador conecta, envía, recibe un `reiniciar` y genera las alarmas de enlace; y otra donde el monitor se reinicia y el simulador se reconecta solo.

## Decisiones
| Decisión | Por qué |
|---|---|
| Un hilo escritor para SQLite | SQLite bloquea la base en cada escritura; con varios hilos escribiendo aparecen errores `database is locked`. La cola lo evita y permite guardar en lotes. |
| JSON por línea | Se lee igual desde Python, C++ y C, y se puede depurar con `nc` o `telnet`. |
| Alarmas solo al abrirse y al cerrarse | Si se informara en cada medición, el operador recibiría cientos de alarmas iguales. |
| Semilla configurable | Las pruebas y las demos dan siempre los mismos datos. |
