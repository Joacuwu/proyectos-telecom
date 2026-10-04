"""Monitor mínimo por consola: servidor TCP multihilo que muestra lo que llega.

Sirve para probar el simulador sin el monitor en Qt, y como referencia del lado servidor del protocolo.
Uso: python -m simulador.monitor_consola --puerto 5050
Escribí `reiniciar CABA-001` o `estado CABA-001` y Enter para mandar un comando.
"""

import argparse
import socket
import sys
import threading

from . import protocolo


class MonitorConsola:
    def __init__(self, host="127.0.0.1", puerto=5050, salida=sys.stdout):
        self.host, self.puerto, self.salida = host, puerto, salida
        self.clientes: dict[socket.socket, set] = {}  # socket -> celdas que maneja
        self._lock = threading.Lock()
        self.recibidos = []
        self.detener = threading.Event()
        self._servidor = None

    def iniciar(self) -> "MonitorConsola":
        self._servidor = socket.create_server((self.host, self.puerto), reuse_port=False)
        self._servidor.settimeout(0.5)
        self.puerto = self._servidor.getsockname()[1]  # útil con puerto 0 en las pruebas
        threading.Thread(target=self._aceptar, name="aceptar", daemon=True).start()
        return self

    def cerrar(self):
        self.detener.set()
        if self._servidor:
            self._servidor.close()
        with self._lock:
            for s in list(self.clientes):
                s.close()
            self.clientes.clear()

    def enviar_comando(self, celda: str, accion: str) -> bool:
        msg = protocolo.codificar({"tipo": "comando", "celda": celda, "accion": accion})
        with self._lock:
            for sock, celdas in self.clientes.items():
                if celda in celdas:
                    sock.sendall(msg)
                    return True
        return False

    def _aceptar(self):
        while not self.detener.is_set():
            try:
                sock, direccion = self._servidor.accept()
            except (socket.timeout, OSError):
                continue
            with self._lock:
                self.clientes[sock] = set()
            # Un hilo por cliente: un simulador lento no frena a los demás
            threading.Thread(target=self._atender, args=(sock, direccion), daemon=True).start()

    def _atender(self, sock, direccion):
        lector = protocolo.LectorDeLineas()
        try:
            while not self.detener.is_set():
                datos = sock.recv(4096)
                if not datos:
                    break
                for linea in lector.agregar(datos):
                    try:
                        msg = protocolo.decodificar(linea)
                    except protocolo.MensajeInvalido as e:
                        print(f"[{direccion[0]}] descartado: {e}", file=self.salida)
                        continue
                    self._procesar(sock, msg)
        except OSError:
            pass
        finally:
            with self._lock:
                self.clientes.pop(sock, None)
            sock.close()

    def _procesar(self, sock, msg):
        with self._lock:
            self.recibidos.append(msg)
            if msg["tipo"] == "hola":
                self.clientes[sock] = set(msg.get("celdas", []))
        t = msg["tipo"]
        if t == "medicion":
            print(f"{msg['ts']}  {msg['celda']:<12} RSRP {msg['rsrp']:>6} dBm  {msg['usuarios']:>4} usuarios  "
                  f"{msg['trafico_mbps']:>6} Mbps  {msg['temperatura']:>5} °C", file=self.salida)
        elif t == "alarma":
            print(f"{msg['ts']}  {msg['celda']:<12} ALARMA {msg['severidad'].upper()} {msg['codigo']}: "
                  f"{msg.get('detalle', '')}", file=self.salida)
        else:
            print(f"<- {msg}", file=self.salida)


def main():
    p = argparse.ArgumentParser(description="Monitor de consola")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--puerto", type=int, default=5050)
    args = p.parse_args()
    mon = MonitorConsola(args.host, args.puerto).iniciar()
    print(f"Escuchando en {args.host}:{mon.puerto}. Comandos: 'reiniciar CELDA', 'estado CELDA', 'salir'.")
    try:
        salir = False
        for linea in sys.stdin:
            partes = linea.split()
            if partes == ["salir"]:
                salir = True
                break
            if len(partes) == 2 and partes[0] in protocolo.ACCIONES:
                if not mon.enviar_comando(partes[1], partes[0]):
                    print("Ningún simulador conectado maneja esa celda.")
        if not salir:
            mon.detener.wait()  # sin teclado (stdin cerrado): sigue escuchando hasta Ctrl+C
    except KeyboardInterrupt:
        pass
    mon.cerrar()


if __name__ == "__main__":
    main()
