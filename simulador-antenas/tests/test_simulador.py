"""Pruebas: python -m unittest discover -s tests -v"""

import io
import os
import sqlite3
import sys
import tempfile
import time
import unittest
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from simulador import protocolo, reportes  # noqa: E402
from simulador.almacen import Almacen  # noqa: E402
from simulador.celda import Celda  # noqa: E402
from simulador.monitor_consola import MonitorConsola  # noqa: E402
from simulador.simulador import Reloj, Simulador  # noqa: E402


def esperar(condicion, timeout=5.0):
    limite = time.monotonic() + timeout
    while time.monotonic() < limite:
        if condicion():
            return True
        time.sleep(0.05)
    return False


class TestProtocolo(unittest.TestCase):
    def test_ida_y_vuelta(self):
        m = {"tipo": "comando", "celda": "CABA-001", "accion": "reiniciar"}
        datos = protocolo.codificar(m)
        self.assertTrue(datos.endswith(b"\n"))
        self.assertEqual(protocolo.decodificar(datos.strip()), m)

    def test_rechaza_json_roto_tipo_desconocido_y_campos_faltantes(self):
        for linea in (b"{roto", b"[1,2]", b'{"tipo":"otro"}', b'{"tipo":"medicion","celda":"X"}',
                      b'{"tipo":"comando","celda":"X","accion":"borrar_todo"}', b"\xff\xfe"):
            with self.subTest(linea=linea):
                with self.assertRaises(protocolo.MensajeInvalido):
                    protocolo.decodificar(linea)

    def test_lector_arma_lineas_partidas_y_juntas(self):
        lector = protocolo.LectorDeLineas()
        self.assertEqual(lector.agregar(b'{"a":'), [])
        self.assertEqual(lector.agregar(b'1}\n{"b":2}\n{"c"'), [b'{"a":1}', b'{"b":2}'])
        self.assertEqual(lector.agregar(b':3}\n'), [b'{"c":3}'])

    def test_lector_descarta_lineas_gigantes(self):
        lector = protocolo.LectorDeLineas()
        lector.agregar(b"x" * (protocolo.LARGO_MAXIMO + 10))
        self.assertEqual(lector.descartadas, 1)
        self.assertEqual(lector.agregar(b'{"ok":1}\n'), [b'{"ok":1}'])


class TestCelda(unittest.TestCase):
    def test_misma_semilla_mismos_datos(self):
        cuando = datetime(2026, 10, 1, 13, 0)
        a = [Celda("X", semilla=7).medir(cuando)[0] for _ in range(1)]
        b = [Celda("X", semilla=7).medir(cuando)[0] for _ in range(1)]
        self.assertEqual(a, b)

    def test_valores_dentro_de_rango(self):
        celda = Celda("X", semilla=1)
        for h in range(24 * 20):
            m, _ = celda.medir(datetime(2026, 10, 1, h % 24))
            self.assertTrue(-125 <= m["rsrp"] <= -70)
            self.assertTrue(0 <= m["usuarios"] <= 650)
            self.assertTrue(0 <= m["trafico_mbps"] <= 300)
            protocolo.decodificar(protocolo.codificar(m).strip())  # cumple el protocolo

    def test_hay_mas_usuarios_al_mediodia_que_de_madrugada(self):
        celda = Celda("X", semilla=3)
        madrugada = sum(celda.medir(datetime(2026, 10, 1, 4))[0]["usuarios"] for _ in range(30))
        mediodia = sum(celda.medir(datetime(2026, 10, 1, 13))[0]["usuarios"] for _ in range(30))
        self.assertGreater(mediodia, madrugada * 3)

    def test_falla_de_ventilacion_abre_y_cierra_una_sola_alarma(self):
        celda = Celda("X", semilla=5)
        celda.probabilidad_falla = 0
        celda.forzar_falla("ventilacion", 25)
        codigos = []
        for _ in range(80):
            codigos += [a["codigo"] for a in celda.medir(datetime(2026, 10, 1, 13))[1]]
        self.assertEqual(codigos.count("TEMPERATURA_ALTA"), 1)
        self.assertEqual(codigos.count("TEMPERATURA_ALTA_NORMALIZADA"), 1)
        self.assertLess(codigos.index("TEMPERATURA_ALTA"), codigos.index("TEMPERATURA_ALTA_NORMALIZADA"))

    def test_reinicio_apaga_la_celda_un_tiempo(self):
        celda = Celda("X")
        celda.reiniciar(5, ahora=100)
        self.assertFalse(celda.esta_activa(ahora=103))
        self.assertTrue(celda.esta_activa(ahora=105))


class TestAlmacenYReportes(unittest.TestCase):
    def test_guarda_desde_varios_hilos_y_los_reportes_responden(self):
        import threading
        with tempfile.TemporaryDirectory() as d:
            ruta = os.path.join(d, "t.db")
            almacen = Almacen(ruta).iniciar(["A", "B", "C", "D"])

            def cargar(nombre):
                celda = Celda(nombre, semilla=ord(nombre))
                celda.forzar_falla("interferencia", 40)
                for h in range(100):
                    m, alarmas = celda.medir(datetime(2026, 10, 1, h % 24, h % 60))
                    almacen.guardar(m)
                    for a in alarmas:
                        almacen.guardar(a)

            hilos = [threading.Thread(target=cargar, args=(n,)) for n in "ABCD"]
            for h in hilos:
                h.start()
            for h in hilos:
                h.join()
            almacen.cerrar()
            self.assertIsNone(almacen.error)

            con = sqlite3.connect(ruta)
            self.assertEqual(con.execute("SELECT COUNT(*) FROM mediciones").fetchone()[0], 400)
            self.assertGreater(con.execute("SELECT COUNT(*) FROM alarmas WHERE codigo='SENAL_DEBIL'").fetchone()[0], 0)
            con.close()

            r = reportes.ejecutar(ruta)
            self.assertEqual(len(r["Resumen por celda"][1]), 4)
            self.assertTrue(r["Alarmas por código y severidad"][1])
            # El filtro por fecha excluye todo si el rango no tiene datos
            vacio = reportes.ejecutar(ruta, "2030-01-01", "2030-01-02")
            self.assertEqual(vacio["Resumen por celda"][1], [])


class TestIntegracionTCP(unittest.TestCase):
    def test_simulador_envia_al_monitor_y_responde_comandos(self):
        monitor = MonitorConsola(puerto=0, salida=io.StringIO()).iniciar()
        sim = Simulador(["CABA-001", "MORON-001"], puerto=monitor.puerto, intervalo=0.1, base_datos=None,
                        reloj=Reloj(datetime(2026, 10, 1, 12), 60), semilla=1, segundos_reinicio=0.5).iniciar()
        try:
            self.assertTrue(sim.conexion.esperar_conexion(5))
            self.assertTrue(esperar(lambda: sum(m["tipo"] == "medicion" for m in monitor.recibidos) >= 10))
            self.assertEqual(monitor.recibidos[0]["tipo"], "hola")
            self.assertEqual({m["celda"] for m in monitor.recibidos if m["tipo"] == "medicion"},
                             {"CABA-001", "MORON-001"})

            self.assertTrue(monitor.enviar_comando("CABA-001", "reiniciar"))
            self.assertTrue(esperar(lambda: any(m["tipo"] == "respuesta" for m in monitor.recibidos)))
            self.assertTrue(esperar(lambda: any(m.get("codigo") == "ENLACE_CAIDO" for m in monitor.recibidos)))
            self.assertTrue(esperar(lambda: any(m.get("codigo") == "ENLACE_RESTABLECIDO" for m in monitor.recibidos)))
        finally:
            sim.detener_todo()
            monitor.cerrar()

    def test_el_simulador_se_reconecta_si_el_monitor_reinicia(self):
        monitor = MonitorConsola(puerto=0, salida=io.StringIO()).iniciar()
        puerto = monitor.puerto
        sim = Simulador(["CABA-001"], puerto=puerto, intervalo=0.1, base_datos=None).iniciar()
        try:
            self.assertTrue(sim.conexion.esperar_conexion(5))
            monitor.cerrar()
            self.assertTrue(esperar(lambda: not sim.conexion.conectado))
            monitor = MonitorConsola(puerto=puerto, salida=io.StringIO()).iniciar()
            self.assertTrue(esperar(lambda: any(m["tipo"] == "medicion" for m in monitor.recibidos), timeout=10))
        finally:
            sim.detener_todo()
            monitor.cerrar()


if __name__ == "__main__":
    unittest.main()
