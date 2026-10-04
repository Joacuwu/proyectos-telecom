#include "ventana.h"

#include "grafico.h"
#include "modelored.h"
#include "servidortcp.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

Ventana::Ventana(ServidorTcp *servidor, ModeloRed *modelo, quint16 puerto, QWidget *parent)
    : QMainWindow(parent), m_servidor(servidor), m_modelo(modelo), m_puerto(puerto)
{
    setWindowTitle("Monitor de antenas");
    armarInterfaz();

    connect(m_modelo, &ModeloRed::celdaActualizada, this, [this](const QString &nombre) {
        if (nombre == m_seleccionada)
            actualizarDetalle();
    });
    connect(m_modelo->alarmas(), &QAbstractItemModel::rowsInserted, this, [this] {
        m_sinReconocer->setText(QString("%1 sin reconocer").arg(m_modelo->alarmas()->sinReconocer()));
    });
    connect(m_servidor, &ServidorTcp::clientesCambiaron, this, &Ventana::actualizarResumen);
    connect(m_servidor, &ServidorTcp::mensajeDescartado, this,
            [this](const QString &motivo) { statusBar()->showMessage("Mensaje descartado: " + motivo, 5000); });

    // Cada segundo: detectar celdas silenciosas y recalcular el resumen
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        m_modelo->revisarSilencios(5000);
        const qint64 total = m_modelo->mensajesProcesados();
        m_mensajesPorSegundo = double(total - m_procesadosAntes);
        m_procesadosAntes = total;
        actualizarResumen();
    });
    timer->start(1000);
    actualizarResumen();
}

void Ventana::armarInterfaz()
{
    auto *central = new QWidget;
    auto *raiz = new QVBoxLayout(central);

    m_resumen = new QLabel;
    m_resumen->setStyleSheet("QLabel { background:#0f172a; color:#e2e8f0; padding:8px 12px; border-radius:6px; }");
    raiz->addWidget(m_resumen);

    // --- Celdas + detalle
    m_tablaCeldas = new QTableView;
    m_tablaCeldas->setModel(m_modelo);
    m_tablaCeldas->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tablaCeldas->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tablaCeldas->verticalHeader()->hide();
    m_tablaCeldas->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tablaCeldas->setAlternatingRowColors(true);
    connect(m_tablaCeldas->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &actual) { seleccionar(m_modelo->nombreEnFila(actual.row())); });

    auto *detalle = new QGroupBox("Detalle");
    auto *dl = new QVBoxLayout(detalle);
    m_tituloDetalle = new QLabel("Elegí una celda");
    m_tituloDetalle->setStyleSheet("font-size:16px; font-weight:600;");
    m_valoresDetalle = new QLabel;
    m_valoresDetalle->setTextFormat(Qt::RichText);
    m_grafico = new GraficoRsrp;
    m_alarmasDetalle = new QLabel;
    m_alarmasDetalle->setWordWrap(true);
    auto *botones = new QHBoxLayout;
    m_btnReiniciar = new QPushButton("Reiniciar celda");
    m_btnEstado = new QPushButton("Pedir medición");
    m_btnReiniciar->setEnabled(false);
    m_btnEstado->setEnabled(false);
    connect(m_btnReiniciar, &QPushButton::clicked, this, [this] { mandarComando("reiniciar"); });
    connect(m_btnEstado, &QPushButton::clicked, this, [this] { mandarComando("estado"); });
    botones->addWidget(m_btnReiniciar);
    botones->addWidget(m_btnEstado);
    dl->addWidget(m_tituloDetalle);
    dl->addWidget(m_valoresDetalle);
    dl->addWidget(m_grafico, 1);
    dl->addWidget(m_alarmasDetalle);
    dl->addLayout(botones);

    auto *arriba = new QSplitter(Qt::Horizontal);
    arriba->addWidget(m_tablaCeldas);
    arriba->addWidget(detalle);
    arriba->setStretchFactor(0, 3);
    arriba->setStretchFactor(1, 2);

    // --- Alarmas
    auto *cajaAlarmas = new QGroupBox("Registro de alarmas");
    auto *al = new QVBoxLayout(cajaAlarmas);
    m_tablaAlarmas = new QTableView;
    m_tablaAlarmas->setModel(m_modelo->alarmas());
    m_tablaAlarmas->verticalHeader()->hide();
    m_tablaAlarmas->horizontalHeader()->setStretchLastSection(true);
    m_tablaAlarmas->setSelectionMode(QAbstractItemView::NoSelection);
    m_tablaAlarmas->setColumnWidth(0, 80);
    m_tablaAlarmas->setColumnWidth(1, 120);
    m_tablaAlarmas->setColumnWidth(2, 230);
    m_tablaAlarmas->setColumnWidth(3, 90);
    auto *pie = new QHBoxLayout;
    m_sinReconocer = new QLabel("0 sin reconocer");
    auto *btnReconocer = new QPushButton("Reconocer todas");
    connect(btnReconocer, &QPushButton::clicked, this, [this] {
        m_modelo->alarmas()->reconocerTodas();
        m_sinReconocer->setText("0 sin reconocer");
    });
    pie->addWidget(m_sinReconocer);
    pie->addStretch();
    pie->addWidget(btnReconocer);
    al->addWidget(m_tablaAlarmas);
    al->addLayout(pie);

    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(arriba);
    vertical->addWidget(cajaAlarmas);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 2);
    raiz->addWidget(vertical, 1);

    setCentralWidget(central);
    resize(1280, 820);
}

void Ventana::seleccionarCelda(const QString &nombre)
{
    const int fila = static_cast<int>(m_modelo->nombres().indexOf(nombre));
    if (fila >= 0)
        m_tablaCeldas->selectRow(fila);
}

void Ventana::seleccionar(const QString &celda)
{
    m_seleccionada = celda;
    m_btnReiniciar->setEnabled(!celda.isEmpty());
    m_btnEstado->setEnabled(!celda.isEmpty());
    actualizarDetalle();
}

void Ventana::actualizarDetalle()
{
    const Celda *c = m_modelo->celda(m_seleccionada);
    if (!c) {
        m_tituloDetalle->setText("Elegí una celda");
        m_valoresDetalle->clear();
        m_grafico->setDatos({});
        m_alarmasDetalle->clear();
        return;
    }
    m_tituloDetalle->setText(QString("%1 · %2").arg(c->nombre, textoEstado(c->estado())));
    m_valoresDetalle->setText(QString("<table cellspacing=6>"
                                      "<tr><td>Señal (RSRP)</td><td align=right><b>%1 dBm</b></td>"
                                      "<td width=24></td><td>Usuarios</td><td align=right><b>%2</b></td></tr>"
                                      "<tr><td>Tráfico</td><td align=right><b>%3 Mbps</b></td>"
                                      "<td></td><td>Temperatura</td><td align=right><b>%4 °C</b></td></tr></table>")
                                  .arg(c->rsrp, 0, 'f', 1)
                                  .arg(c->usuarios)
                                  .arg(c->traficoMbps, 0, 'f', 1)
                                  .arg(c->temperatura, 0, 'f', 1));
    m_grafico->setDatos(c->historialRsrp);
    m_alarmasDetalle->setText(c->alarmasActivas.isEmpty()
                                  ? "Sin alarmas activas"
                                  : "Alarmas activas: " + c->alarmasActivas.keys().join(", "));
}

void Ventana::actualizarResumen()
{
    m_resumen->setText(QString("Escuchando en el puerto %1   ·   %2 simulador(es)   ·   %3 celdas   ·   "
                               "<span style='color:#fca5a5'>%4 críticas</span>   ·   "
                               "<span style='color:#fcd34d'>%5 con advertencia</span>   ·   %6 caídas o sin datos   ·   %7 msg/s")
                           .arg(m_puerto)
                           .arg(m_servidor->clientesConectados())
                           .arg(m_modelo->rowCount())
                           .arg(m_modelo->cantidadEn(EstadoCelda::Critica))
                           .arg(m_modelo->cantidadEn(EstadoCelda::Advertencia))
                           .arg(m_modelo->cantidadEn(EstadoCelda::Caida) + m_modelo->cantidadEn(EstadoCelda::SinDatos))
                           .arg(m_mensajesPorSegundo, 0, 'f', 0));
}

void Ventana::mandarComando(const QString &accion)
{
    if (m_seleccionada.isEmpty())
        return;
    if (m_servidor->enviarComando(m_seleccionada, accion))
        statusBar()->showMessage(QString("Comando '%1' enviado a %2").arg(accion, m_seleccionada), 4000);
    else
        statusBar()->showMessage("Ningún simulador conectado maneja " + m_seleccionada, 4000);
}
