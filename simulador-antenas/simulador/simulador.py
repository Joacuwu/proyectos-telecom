"""Orquesta las celdas: un hilo por celda, la conexión con el monitor y el almacenamiento."""

import logging
import threading
import time
import zlib
from datetime import datetime, timedelta

from .almacen import Almacen
from .celda import Celda
from .conexion import Conexion

log = logging.getLogger(__name__)


class Reloj:
    """Reloj simulado: con velocidad 60, cada segundo real equivale a un minuto de datos."""

    def __init__(self, inicio: datetime | None = None, velocidad: float = 1.0):
        self.inicio = inicio or datetime.now()
        self.velocidad = velocidad
        self._t0 = time.monotonic()

    def ahora(self) -> datetime:
        return self.inicio + timedelta(seconds=(time.monotonic() - self._t0) * self.velocidad)


class Simulador:
    def __init__(self, nombres: list[str], host: str = "127.0.0.1", puerto: int = 5050,
                 intervalo: float = 1.0, base_datos: str | None = "telemetria.db",
                 reloj: Reloj | None = None, semilla: int | None = None, segundos_reinicio: float = 5.0):
        self.detener = threading.Event()
        self.intervalo = intervalo
        self.reloj = reloj or Reloj()
        self.segundos_reinicio = segundos_reinicio
        self.celdas = {n: Celda(n, None if semilla is None else semilla + i) for i, n in enumerate(nombres)}
        self.almacen = Almacen(base_datos) if base_datos else None
        self.conexion = Conexion(host, puerto, {"tipo": "hola", "origen": "simulador", "celdas": nombres},
                                 self._al_recibir, self.detener)
        self._hilos: list[threading.Thread] = []

    def iniciar(self) -> "Simulador":
        if self.almacen:
            self.almacen.iniciar(list(self.celdas))
        self.conexion.iniciar()
        for celda in self.celdas.values():
            hilo = threading.Thread(target=self._ciclo_celda, args=(celda,), name=celda.nombre, daemon=True)
            hilo.start()
            self._hilos.append(hilo)
        return self

    def detener_todo(self):
        self.detener.set()
        for hilo in self._hilos:
            hilo.join(self.intervalo + 1)
        self.conexion.cerrar()
        if self.almacen:
            self.almacen.cerrar()

    # --- Por celda ---------------------------------------------------------------

    def _publicar(self, mensaje: dict):
        if self.almacen:
            self.almacen.guardar(mensaje)
        self.conexion.enviar(mensaje)

    def _ciclo_celda(self, celda: Celda):
        activa = True
        # Arranques desfasados para que las celdas no transmitan todas en el mismo instante
        self.detener.wait(self.intervalo * (zlib.crc32(celda.nombre.encode()) % 100) / 100)
        while not self.detener.is_set():
            ahora_activa = celda.esta_activa()
            if ahora_activa != activa:
                activa = ahora_activa
                self._publicar(celda.alarma_enlace(caido=not activa, cuando=self.reloj.ahora()))
            if activa:
                medicion, alarmas = celda.medir(self.reloj.ahora())
                self._publicar(medicion)
                for alarma in alarmas:
                    log.info("Alarma %s en %s: %s", alarma["codigo"], celda.nombre, alarma["detalle"])
                    self._publicar(alarma)
            self.detener.wait(self.intervalo)

    # --- Comandos del monitor ----------------------------------------------------------

    def _al_recibir(self, mensaje: dict):
        if mensaje["tipo"] != "comando":
            return
        nombre, accion = mensaje["celda"], mensaje["accion"]
        celda = self.celdas.get(nombre)
        if celda is None:
            self.conexion.enviar({"tipo": "respuesta", "celda": nombre, "accion": accion, "ok": False,
                                  "detalle": "Celda desconocida"})
            return
        if accion == "reiniciar":
            celda.reiniciar(self.segundos_reinicio)
            detalle = f"Reiniciando ({self.segundos_reinicio:.0f} s)"
        else:  # estado
            if celda.esta_activa():
                medicion, _ = celda.medir(self.reloj.ahora())
                self._publicar(medicion)
                detalle = "Medición enviada"
            else:
                detalle = "La celda está reiniciándose"
        log.info("Comando %s para %s: %s", accion, nombre, detalle)
        self.conexion.enviar({"tipo": "respuesta", "celda": nombre, "accion": accion, "ok": True, "detalle": detalle})
