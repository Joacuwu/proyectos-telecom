"""Codificación y validación de mensajes del protocolo de telemetría (ver PROTOCOLO.md)."""

import json

LARGO_MAXIMO = 4096

TIPOS = {"hola", "medicion", "alarma", "comando", "respuesta"}
ACCIONES = {"reiniciar", "estado"}
SEVERIDADES = {"menor", "mayor", "critica"}

CAMPOS_OBLIGATORIOS = {
    "hola": ("origen",),
    "medicion": ("celda", "ts", "rsrp", "usuarios", "trafico_mbps", "temperatura"),
    "alarma": ("celda", "ts", "codigo", "severidad"),
    "comando": ("celda", "accion"),
    "respuesta": ("celda", "accion", "ok"),
}


class MensajeInvalido(ValueError):
    """El mensaje recibido no respeta el protocolo."""


def codificar(mensaje: dict) -> bytes:
    """Convierte un dict en una línea JSON terminada en \\n."""
    linea = json.dumps(mensaje, ensure_ascii=False, separators=(",", ":"))
    datos = (linea + "\n").encode("utf-8")
    if len(datos) > LARGO_MAXIMO:
        raise MensajeInvalido(f"mensaje de {len(datos)} bytes supera el máximo de {LARGO_MAXIMO}")
    return datos


def decodificar(linea: bytes | str) -> dict:
    """Convierte una línea recibida en dict y valida tipo y campos obligatorios."""
    if isinstance(linea, bytes):
        if len(linea) > LARGO_MAXIMO:
            raise MensajeInvalido("línea demasiado larga")
        try:
            linea = linea.decode("utf-8")
        except UnicodeDecodeError as e:
            raise MensajeInvalido("no es UTF-8") from e

    try:
        mensaje = json.loads(linea)
    except json.JSONDecodeError as e:
        raise MensajeInvalido(f"JSON inválido: {e.msg}") from e

    if not isinstance(mensaje, dict):
        raise MensajeInvalido("se esperaba un objeto JSON")
    tipo = mensaje.get("tipo")
    if tipo not in TIPOS:
        raise MensajeInvalido(f"tipo desconocido: {tipo!r}")

    faltan = [c for c in CAMPOS_OBLIGATORIOS[tipo] if c not in mensaje]
    if faltan:
        raise MensajeInvalido(f"faltan campos en '{tipo}': {', '.join(faltan)}")

    if tipo == "comando" and mensaje["accion"] not in ACCIONES:
        raise MensajeInvalido(f"acción desconocida: {mensaje['accion']!r}")
    if tipo == "alarma" and mensaje["severidad"] not in SEVERIDADES:
        raise MensajeInvalido(f"severidad desconocida: {mensaje['severidad']!r}")
    return mensaje


class LectorDeLineas:
    """Junta los bytes que llegan del socket y devuelve líneas completas.

    TCP es un flujo: un recv() puede traer medio mensaje o tres juntos.
    """

    def __init__(self):
        self._buffer = b""
        self.descartadas = 0

    def agregar(self, datos: bytes) -> list[bytes]:
        self._buffer += datos
        lineas = []
        while b"\n" in self._buffer:
            linea, self._buffer = self._buffer.split(b"\n", 1)
            if len(linea) > LARGO_MAXIMO:
                self.descartadas += 1
                continue
            if linea.strip():
                lineas.append(linea)
        # Un cliente que nunca manda \n no puede llenar la memoria
        if len(self._buffer) > LARGO_MAXIMO:
            self._buffer = b""
            self.descartadas += 1
        return lineas
