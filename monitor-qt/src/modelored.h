#pragma once

// Estado de la red armado a partir de los mensajes recibidos. Vive en el hilo principal
// (los hilos de red le pasan los mensajes por señales), así que no necesita locks.

#include <QAbstractTableModel>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <functional>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QVector>

enum class EstadoCelda { Normal, Advertencia, Critica, Caida, SinDatos };

QString textoEstado(EstadoCelda estado);

struct Celda {
    QString nombre;
    double rsrp = 0;
    int usuarios = 0;
    double traficoMbps = 0;
    double temperatura = 0;
    QString ultimaMedicion;               // ts que informó el simulador
    qint64 recibidaMs = -1;               // reloj del monitor, para detectar silencios
    bool enlaceCaido = false;
    bool sinDatos = false;
    QMap<QString, QString> alarmasActivas;  // código -> severidad
    QVector<double> historialRsrp;        // últimas N mediciones, para el gráfico
    int mediciones = 0;

    EstadoCelda estado() const;
};

struct Alarma {
    QString ts;
    QString celda;
    QString codigo;
    QString severidad;
    QString detalle;
    bool reconocida = false;
};

// Tabla de alarmas para la vista (la más nueva primero).
class ModeloAlarmas : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Columna { ColHora, ColCelda, ColCodigo, ColSeveridad, ColDetalle, CantColumnas };

    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void agregar(const Alarma &alarma);
    void reconocerTodas();
    int sinReconocer() const;
    const QList<Alarma> &alarmas() const { return m_alarmas; }

private:
    QList<Alarma> m_alarmas;
    static constexpr int kMaximo = 500;
};

class ModeloRed : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Columna { ColCelda, ColEstado, ColRsrp, ColUsuarios, ColTrafico, ColTemperatura, ColUltima, CantColumnas };
    static constexpr int kHistorial = 60;

    explicit ModeloRed(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    // Aplica un mensaje ya validado. Devuelve false si no corresponde a este modelo.
    bool aplicar(const QJsonObject &mensaje);

    // Marca "sin datos" las celdas que no informan hace más de `umbralMs`.
    void revisarSilencios(qint64 umbralMs);

    const Celda *celda(const QString &nombre) const;
    QString nombreEnFila(int fila) const;
    QStringList nombres() const { return m_orden; }
    ModeloAlarmas *alarmas() { return &m_alarmas; }
    int cantidadEn(EstadoCelda estado) const;
    qint64 mensajesProcesados() const { return m_procesados; }

    // Permite simular el paso del tiempo en las pruebas.
    void setRelojMs(std::function<qint64()> reloj) { m_reloj = std::move(reloj); }

signals:
    void celdaActualizada(const QString &nombre);
    void alarmaNueva(const Alarma &alarma);

private:
    Celda &obtener(const QString &nombre);
    void filaCambiada(const QString &nombre);

    QHash<QString, Celda> m_celdas;
    QStringList m_orden;  // orden alfabético estable para la tabla
    ModeloAlarmas m_alarmas;
    qint64 m_procesados = 0;
    QElapsedTimer m_inicio;
    std::function<qint64()> m_reloj;
};
