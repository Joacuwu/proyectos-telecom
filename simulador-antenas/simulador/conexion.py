"""Conexión TCP con el monitor: envío seguro entre hilos, lectura de comandos y reconexión."""

import logging
import socket
import threading

from . import protocolo

log = logging.getLogger(__name__)


class Conexion:
    """Mantiene la conexión con el monitor.

    - `enviar()` se llama desde los hilos de las celdas: un lock evita que dos mensajes se mezclen en el socket.
    - Un hilo lector recibe comandos y los pasa a `al_recibir`.
    - Si la conexión se corta, se reintenta con espera creciente (1, 2, 4… hasta 30 s).
    """

    def __init__(self, host: str, puerto: int, presentacion: dict, al_recibir, detener: threading.Event,
                 espera_maxima: float = 30.0):
        self.host = host
        self.puerto = puerto
        self.presentacion = presentacion
        self.al_recibir = al_recibir
        self.detener = detener
        self.espera_maxima = espera_maxima
        self._sock = None
        self._lock = threading.Lock()
        self._conectado = threading.Event()
        self.enviados = 0
        self.perdidos = 0
        self._hilo = threading.Thread(target=self._mantener, name="conexion", daemon=True)

    def iniciar(self) -> "Conexion":
        self._hilo.start()
        return self

    @property
    def conectado(self) -> bool:
        return self._conectado.is_set()

    def esperar_conexion(self, timeout: float) -> bool:
        return self._conectado.wait(timeout)

    def enviar(self, mensaje: dict) -> bool:
        """Devuelve False si no hay conexión (el mensaje se cuenta como perdido; queda guardado en SQLite)."""
        datos = protocolo.codificar(mensaje)
        with self._lock:
            if self._sock is None:
                self.perdidos += 1
                return False
            try:
                self._sock.sendall(datos)
                self.enviados += 1
                return True
            except OSError as e:
                log.warning("Error al enviar: %s", e)
                self._cerrar_socket()
                self.perdidos += 1
                return False

    def cerrar(self):
        self.detener.set()
        with self._lock:
            self._cerrar_socket()
        self._hilo.join(2)

    # --- Internos ---------------------------------------------------------------

    def _cerrar_socket(self):
        """Llamar con el lock tomado."""
        if self._sock is not None:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self._sock.close()
            self._sock = None
        self._conectado.clear()

    def _mantener(self):
        espera = 1.0
        while not self.detener.is_set():
            try:
                sock = socket.create_connection((self.host, self.puerto), timeout=5)
                sock.settimeout(1.0)  # para revisar `detener` periódicamente mientras lee
                sock.sendall(protocolo.codificar(self.presentacion))
            except OSError as e:
                log.info("Sin conexión con %s:%s (%s). Reintento en %.0f s", self.host, self.puerto, e, espera)
                self.detener.wait(espera)
                espera = min(espera * 2, self.espera_maxima)
                continue

            log.info("Conectado a %s:%s", self.host, self.puerto)
            with self._lock:
                self._sock = sock
                self._conectado.set()
            espera = 1.0
            self._leer(sock)
            with self._lock:
                if self._sock is sock:
                    self._cerrar_socket()
            if not self.detener.is_set():
                log.warning("Se cortó la conexión con el monitor")

    def _leer(self, sock):
        lector = protocolo.LectorDeLineas()
        while not self.detener.is_set():
            try:
                datos = sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            if not datos:
                return  # el monitor cerró la conexión
            for linea in lector.agregar(datos):
                try:
                    self.al_recibir(protocolo.decodificar(linea))
                except protocolo.MensajeInvalido as e:
                    log.warning("Mensaje descartado: %s", e)
