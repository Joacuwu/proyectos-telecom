#include "modelored.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <algorithm>

namespace {

QColor colorEstado(EstadoCelda e)
{
    switch (e) {
    case EstadoCelda::Normal: return QColor(0xd1, 0xfa, 0xe5);
    case EstadoCelda::Advertencia: return QColor(0xfe, 0xf3, 0xc7);
    case EstadoCelda::Critica: return QColor(0xfe, 0xca, 0xca);
    case EstadoCelda::Caida: return QColor(0xe5, 0xe7, 0xeb);
    case EstadoCelda::SinDatos: return QColor(0xe0, 0xe7, 0xff);
    }
    return {};
}

QColor colorSeveridad(const QString &s)
{
    if (s == "critica") return QColor(0xfe, 0xca, 0xca);
    if (s == "mayor") return QColor(0xfe, 0xd7, 0xaa);
    return QColor(0xfe, 0xf9, 0xc3);
}

}  // namespace

QString textoEstado(EstadoCelda estado)
{
    switch (estado) {
    case EstadoCelda::Normal: return "Normal";
    case EstadoCelda::Advertencia: return "Advertencia";
    case EstadoCelda::Critica: return "Crítica";
    case EstadoCelda::Caida: return "Enlace caído";
    case EstadoCelda::SinDatos: return "Sin datos";
    }
    return {};
}

EstadoCelda Celda::estado() const
{
    if (enlaceCaido) return EstadoCelda::Caida;
    if (sinDatos) return EstadoCelda::SinDatos;
    if (std::any_of(alarmasActivas.cbegin(), alarmasActivas.cend(), [](const QString &s) { return s == "critica"; }))
        return EstadoCelda::Critica;
    if (!alarmasActivas.isEmpty()) return EstadoCelda::Advertencia;
    return EstadoCelda::Normal;
}

// ---------------------------------------------------------------- ModeloAlarmas

int ModeloAlarmas::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_alarmas.size(); }
int ModeloAlarmas::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : CantColumnas; }

QVariant ModeloAlarmas::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_alarmas.size())
        return {};
    const Alarma &a = m_alarmas.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColHora: return a.ts.mid(11, 8);
        case ColCelda: return a.celda;
        case ColCodigo: return a.codigo;
        case ColSeveridad: return a.severidad;
        case ColDetalle: return a.detalle;
        }
    }
    if (role == Qt::BackgroundRole && !a.reconocida && !a.codigo.endsWith("NORMALIZADA") && a.codigo != "ENLACE_RESTABLECIDO")
        return QBrush(colorSeveridad(a.severidad));
    if (role == Qt::FontRole && !a.reconocida) {
        QFont f;
        f.setBold(a.severidad == "critica");
        return f;
    }
    return {};
}

QVariant ModeloAlarmas::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const char *titulos[] = {"Hora", "Celda", "Código", "Severidad", "Detalle"};
    return QString::fromUtf8(titulos[section]);
}

void ModeloAlarmas::agregar(const Alarma &alarma)
{
    beginInsertRows({}, 0, 0);
    m_alarmas.prepend(alarma);
    endInsertRows();
    if (m_alarmas.size() > kMaximo) {
        beginRemoveRows({}, kMaximo, m_alarmas.size() - 1);
        while (m_alarmas.size() > kMaximo)
            m_alarmas.removeLast();
        endRemoveRows();
    }
}

void ModeloAlarmas::reconocerTodas()
{
    for (Alarma &a : m_alarmas)
        a.reconocida = true;
    if (!m_alarmas.isEmpty())
        emit dataChanged(index(0, 0), index(m_alarmas.size() - 1, CantColumnas - 1));
}

int ModeloAlarmas::sinReconocer() const
{
    return static_cast<int>(std::count_if(m_alarmas.cbegin(), m_alarmas.cend(), [](const Alarma &a) {
        return !a.reconocida && !a.codigo.endsWith("NORMALIZADA") && a.codigo != "ENLACE_RESTABLECIDO";
    }));
}

// ---------------------------------------------------------------- ModeloRed

ModeloRed::ModeloRed(QObject *parent) : QAbstractTableModel(parent), m_alarmas(this)
{
    m_inicio.start();
    m_reloj = [this] { return m_inicio.elapsed(); };
}

int ModeloRed::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_orden.size(); }
int ModeloRed::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : CantColumnas; }

QVariant ModeloRed::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_orden.size())
        return {};
    const Celda &c = m_celdas[m_orden.at(index.row())];
    const bool sinMedicion = c.mediciones == 0;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColCelda: return c.nombre;
        case ColEstado: return textoEstado(c.estado());
        case ColRsrp: return sinMedicion ? QVariant() : QString::number(c.rsrp, 'f', 1) + " dBm";
        case ColUsuarios: return sinMedicion ? QVariant() : QVariant(c.usuarios);
        case ColTrafico: return sinMedicion ? QVariant() : QString::number(c.traficoMbps, 'f', 1) + " Mbps";
        case ColTemperatura: return sinMedicion ? QVariant() : QString::number(c.temperatura, 'f', 1) + " °C";
        case ColUltima: return c.ultimaMedicion.mid(11, 8);
        }
    }
    if (role == Qt::BackgroundRole && index.column() == ColEstado)
        return QBrush(colorEstado(c.estado()));
    if (role == Qt::ForegroundRole && index.column() == ColRsrp && c.rsrp < -110 && !sinMedicion)
        return QBrush(QColor(0xb9, 0x1c, 0x1c));
    if (role == Qt::ForegroundRole && index.column() == ColTemperatura && c.temperatura > 70)
        return QBrush(QColor(0xb9, 0x1c, 0x1c));
    if (role == Qt::TextAlignmentRole && index.column() >= ColRsrp && index.column() <= ColTemperatura)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ToolTipRole && index.column() == ColEstado && !c.alarmasActivas.isEmpty())
        return "Alarmas activas: " + c.alarmasActivas.keys().join(", ");
    return {};
}

QVariant ModeloRed::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    static const char *titulos[] = {"Celda", "Estado", "RSRP", "Usuarios", "Tráfico", "Temperatura", "Última"};
    return QString::fromUtf8(titulos[section]);
}

Celda &ModeloRed::obtener(const QString &nombre)
{
    auto it = m_celdas.find(nombre);
    if (it != m_celdas.end())
        return it.value();

    // Inserta manteniendo el orden alfabético
    const auto pos = std::lower_bound(m_orden.begin(), m_orden.end(), nombre);
    const int fila = static_cast<int>(pos - m_orden.begin());
    beginInsertRows({}, fila, fila);
    m_orden.insert(fila, nombre);
    Celda nueva;
    nueva.nombre = nombre;
    it = m_celdas.insert(nombre, nueva);
    endInsertRows();
    return it.value();
}

void ModeloRed::filaCambiada(const QString &nombre)
{
    const int fila = static_cast<int>(m_orden.indexOf(nombre));
    if (fila >= 0)
        emit dataChanged(index(fila, 0), index(fila, CantColumnas - 1));
    emit celdaActualizada(nombre);
}

bool ModeloRed::aplicar(const QJsonObject &m)
{
    const QString tipo = m.value("tipo").toString();

    if (tipo == "hola") {
        for (const auto &v : m.value("celdas").toArray())
            obtener(v.toString());
        ++m_procesados;
        return true;
    }

    if (tipo == "medicion") {
        Celda &c = obtener(m.value("celda").toString());
        c.rsrp = m.value("rsrp").toDouble();
        c.usuarios = m.value("usuarios").toInt();
        c.traficoMbps = m.value("trafico_mbps").toDouble();
        c.temperatura = m.value("temperatura").toDouble();
        c.ultimaMedicion = m.value("ts").toString();
        c.recibidaMs = m_reloj();
        c.sinDatos = false;
        c.enlaceCaido = false;  // si llega una medición, el enlace está arriba
        c.historialRsrp.append(c.rsrp);
        if (c.historialRsrp.size() > kHistorial)
            c.historialRsrp.removeFirst();
        ++c.mediciones;
        ++m_procesados;
        filaCambiada(c.nombre);
        return true;
    }

    if (tipo == "alarma") {
        Celda &c = obtener(m.value("celda").toString());
        Alarma a{m.value("ts").toString(), c.nombre, m.value("codigo").toString(),
                 m.value("severidad").toString(), m.value("detalle").toString()};

        if (a.codigo == "ENLACE_CAIDO") {
            c.enlaceCaido = true;
        } else if (a.codigo == "ENLACE_RESTABLECIDO") {
            c.enlaceCaido = false;
        } else if (a.codigo.endsWith("_NORMALIZADA")) {
            c.alarmasActivas.remove(a.codigo.chopped(int(sizeof("_NORMALIZADA")) - 1));
        } else {
            c.alarmasActivas.insert(a.codigo, a.severidad);
        }
        m_alarmas.agregar(a);
        ++m_procesados;
        filaCambiada(c.nombre);
        emit alarmaNueva(a);
        return true;
    }

    if (tipo == "respuesta") {
        ++m_procesados;
        return true;
    }
    return false;
}

void ModeloRed::revisarSilencios(qint64 umbralMs)
{
    const qint64 ahora = m_reloj();
    for (auto it = m_celdas.begin(); it != m_celdas.end(); ++it) {
        Celda &c = it.value();
        const bool silenciosa = c.recibidaMs >= 0 && !c.enlaceCaido && ahora - c.recibidaMs > umbralMs;
        if (silenciosa != c.sinDatos) {
            c.sinDatos = silenciosa;
            filaCambiada(c.nombre);
        }
    }
}

const Celda *ModeloRed::celda(const QString &nombre) const
{
    auto it = m_celdas.constFind(nombre);
    return it == m_celdas.constEnd() ? nullptr : &it.value();
}

QString ModeloRed::nombreEnFila(int fila) const
{
    return fila >= 0 && fila < m_orden.size() ? m_orden.at(fila) : QString();
}

int ModeloRed::cantidadEn(EstadoCelda estado) const
{
    return static_cast<int>(std::count_if(m_celdas.cbegin(), m_celdas.cend(),
                                          [estado](const Celda &c) { return c.estado() == estado; }));
}
