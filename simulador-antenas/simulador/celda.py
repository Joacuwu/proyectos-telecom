"""Modelo de una celda (antena) que genera mediciones realistas y detecta alarmas."""

import random
import threading
import time
from datetime import datetime

UMBRAL_RSRP = -110.0        # dBm: por debajo, señal débil
UMBRAL_TEMPERATURA = 70.0   # °C
UMBRAL_USUARIOS = 550


def _acotar(valor, minimo, maximo):
    return max(minimo, min(maximo, valor))


def factor_horario(hora: int) -> float:
    """Carga relativa según la hora del día: baja de madrugada, picos a las 13 y a las 20."""
    curva = [0.15, 0.1, 0.08, 0.07, 0.08, 0.15, 0.35, 0.6, 0.75, 0.8, 0.82, 0.85,
             0.95, 1.0, 0.9, 0.85, 0.85, 0.9, 0.95, 1.0, 1.0, 0.9, 0.6, 0.3]
    return curva[hora % 24]


class Celda:
    """Estado de una antena. Es segura para usar desde varios hilos (lock interno)."""

    def __init__(self, nombre: str, semilla: int | None = None, capacidad: int = 500):
        self.nombre = nombre
        self.capacidad = capacidad
        self._azar = random.Random(semilla)
        self._lock = threading.Lock()
        self._rsrp = self._azar.uniform(-100, -85)
        self._temperatura = self._azar.uniform(32, 42)
        self._activas = set()            # alarmas abiertas, para no repetirlas en cada medición
        self._apagada_hasta = 0.0
        self._falla = None               # (tipo, mediciones restantes): simula fallas de equipo
        self.probabilidad_falla = 0.004
        self.mediciones = 0

    # --- Comandos ---------------------------------------------------------

    def reiniciar(self, segundos: float = 5.0, ahora: float | None = None) -> None:
        with self._lock:
            self._apagada_hasta = (ahora if ahora is not None else time.monotonic()) + segundos

    def esta_activa(self, ahora: float | None = None) -> bool:
        with self._lock:
            return (ahora if ahora is not None else time.monotonic()) >= self._apagada_hasta

    # --- Generación ---------------------------------------------------------

    def medir(self, cuando: datetime | None = None) -> tuple[dict, list[dict]]:
        """Devuelve (medicion, alarmas_nuevas). Las alarmas se informan solo al abrirse o cerrarse."""
        cuando = cuando or datetime.now()
        with self._lock:
            self._avanzar_falla()
            falla = self._falla[0] if self._falla else None

            # Paseo aleatorio: cada valor parte del anterior, así la serie no salta sin sentido.
            # Con interferencia la señal cae; al terminar vuelve de a poco hacia su zona normal.
            deriva = -1.5 if falla == "interferencia" else (1.2 if self._rsrp < -100 else 0.0)
            self._rsrp = _acotar(self._rsrp + deriva + self._azar.gauss(0, 1.5), -125, -70)
            carga = factor_horario(cuando.hour)
            usuarios = int(_acotar(self._azar.gauss(self.capacidad * carga, 30), 0, 650))
            trafico = round(_acotar(usuarios * self._azar.uniform(0.25, 0.45), 0, 300), 1)
            objetivo_temp = 30 + 30 * (usuarios / 650) + (40 if falla == "ventilacion" else 0)
            self._temperatura = _acotar(self._temperatura + (objetivo_temp - self._temperatura) * 0.2
                                        + self._azar.gauss(0, 0.8), 15, 95)
            self.mediciones += 1

            medicion = {
                "tipo": "medicion",
                "celda": self.nombre,
                "ts": cuando.isoformat(timespec="seconds"),
                "rsrp": round(self._rsrp, 1),
                "usuarios": usuarios,
                "trafico_mbps": trafico,
                "temperatura": round(self._temperatura, 1),
            }
            return medicion, self._evaluar_alarmas(medicion)

    def _avanzar_falla(self):
        if self._falla:
            tipo, restantes = self._falla
            self._falla = (tipo, restantes - 1) if restantes > 1 else None
        elif self._azar.random() < self.probabilidad_falla:
            self._falla = (self._azar.choice(["ventilacion", "interferencia"]), self._azar.randint(10, 25))

    def forzar_falla(self, tipo: str, mediciones: int = 15) -> None:
        """Para pruebas y demos: provoca una falla conocida."""
        with self._lock:
            self._falla = (tipo, mediciones)

    def _evaluar_alarmas(self, m: dict) -> list[dict]:
        condiciones = {
            "SENAL_DEBIL": (m["rsrp"] < UMBRAL_RSRP, "mayor", f"RSRP {m['rsrp']} dBm"),
            "TEMPERATURA_ALTA": (m["temperatura"] > UMBRAL_TEMPERATURA, "critica", f"{m['temperatura']} °C"),
            "SATURACION": (m["usuarios"] > UMBRAL_USUARIOS, "menor", f"{m['usuarios']} usuarios"),
        }
        alarmas = []
        for codigo, (activa, severidad, detalle) in condiciones.items():
            if activa and codigo not in self._activas:
                self._activas.add(codigo)
                alarmas.append(self._alarma(m["ts"], codigo, severidad, detalle))
            elif not activa and codigo in self._activas:
                self._activas.discard(codigo)
                alarmas.append(self._alarma(m["ts"], codigo + "_NORMALIZADA", "menor", detalle))
        return alarmas

    def alarma_enlace(self, caido: bool, cuando: datetime | None = None) -> dict:
        ts = (cuando or datetime.now()).isoformat(timespec="seconds")
        if caido:
            return self._alarma(ts, "ENLACE_CAIDO", "critica", "La celda dejó de transmitir")
        return self._alarma(ts, "ENLACE_RESTABLECIDO", "menor", "La celda volvió a transmitir")

    def _alarma(self, ts, codigo, severidad, detalle):
        return {"tipo": "alarma", "celda": self.nombre, "ts": ts,
                "codigo": codigo, "severidad": severidad, "detalle": detalle}
