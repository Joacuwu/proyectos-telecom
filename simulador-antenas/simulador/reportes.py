"""Reportes SQL sobre la base de telemetría.

Uso: python -m simulador.reportes telemetria.db [--desde 2026-10-01] [--hasta 2026-10-02]
"""

import argparse
import sqlite3

CONSULTAS = {
    "Resumen por celda": """
        SELECT celda,
               COUNT(*)                     AS mediciones,
               ROUND(AVG(rsrp), 1)          AS rsrp_prom,
               ROUND(MIN(rsrp), 1)          AS rsrp_min,
               ROUND(AVG(usuarios))         AS usuarios_prom,
               MAX(usuarios)                AS usuarios_max,
               ROUND(SUM(trafico_mbps) / COUNT(*), 1) AS trafico_prom_mbps
        FROM mediciones
        WHERE ts BETWEEN :desde AND :hasta
        GROUP BY celda
        ORDER BY rsrp_prom ASC""",

    "Hora pico (usuarios promedio por hora del día)": """
        SELECT CAST(strftime('%H', ts) AS INTEGER) AS hora,
               ROUND(AVG(usuarios)) AS usuarios_prom,
               ROUND(AVG(trafico_mbps), 1) AS trafico_prom_mbps
        FROM mediciones
        WHERE ts BETWEEN :desde AND :hasta
        GROUP BY hora
        ORDER BY usuarios_prom DESC
        LIMIT 5""",

    "Alarmas por código y severidad": """
        SELECT codigo, severidad, COUNT(*) AS cantidad, COUNT(DISTINCT celda) AS celdas
        FROM alarmas
        WHERE ts BETWEEN :desde AND :hasta AND codigo NOT LIKE '%_NORMALIZADA' AND codigo <> 'ENLACE_RESTABLECIDO'
        GROUP BY codigo, severidad
        ORDER BY cantidad DESC""",

    "Celdas con más alarmas críticas": """
        SELECT c.nombre AS celda,
               COUNT(a.id) AS criticas,
               MAX(a.ts) AS ultima
        FROM celdas c
        LEFT JOIN alarmas a ON a.celda = c.nombre AND a.severidad = 'critica'
                           AND a.ts BETWEEN :desde AND :hasta
        GROUP BY c.nombre
        HAVING COUNT(a.id) > 0
        ORDER BY criticas DESC, celda""",

    "Mediciones con señal débil (% por celda)": """
        SELECT celda,
               ROUND(100.0 * SUM(CASE WHEN rsrp < -110 THEN 1 ELSE 0 END) / COUNT(*), 1) AS pct_senal_debil
        FROM mediciones
        WHERE ts BETWEEN :desde AND :hasta
        GROUP BY celda
        HAVING pct_senal_debil > 0
        ORDER BY pct_senal_debil DESC""",
}


def ejecutar(ruta: str, desde: str = "0000", hasta: str = "9999") -> dict[str, tuple[list[str], list[tuple]]]:
    con = sqlite3.connect(f"file:{ruta}?mode=ro", uri=True)  # solo lectura: un reporte nunca modifica datos
    try:
        resultados = {}
        for titulo, sql in CONSULTAS.items():
            cur = con.execute(sql, {"desde": desde, "hasta": hasta + "￿"})
            resultados[titulo] = ([d[0] for d in cur.description], cur.fetchall())
        return resultados
    finally:
        con.close()


def imprimir_tabla(columnas, filas):
    if not filas:
        print("  (sin datos)\n")
        return
    anchos = [max(len(str(c)), *(len(str(f[i])) for f in filas)) for i, c in enumerate(columnas)]
    print("  " + "  ".join(str(c).ljust(a) for c, a in zip(columnas, anchos)))
    print("  " + "  ".join("-" * a for a in anchos))
    for f in filas:
        print("  " + "  ".join(str(v).rjust(a) if isinstance(v, (int, float)) else str(v).ljust(a)
                                for v, a in zip(f, anchos)))
    print()


def main():
    p = argparse.ArgumentParser(description="Reportes de telemetría")
    p.add_argument("db", nargs="?", default="telemetria.db")
    p.add_argument("--desde", default="0000", help="ej. 2026-10-01")
    p.add_argument("--hasta", default="9999", help="ej. 2026-10-02 (incluye todo ese día)")
    args = p.parse_args()
    for titulo, (columnas, filas) in ejecutar(args.db, args.desde, args.hasta).items():
        print(f"== {titulo}")
        imprimir_tabla(columnas, filas)


if __name__ == "__main__":
    main()
