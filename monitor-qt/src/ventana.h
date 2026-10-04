#pragma once

#include <QMainWindow>

class GraficoRsrp;
class ModeloRed;
class QLabel;
class QPushButton;
class QTableView;
class ServidorTcp;

class Ventana : public QMainWindow
{
    Q_OBJECT
public:
    Ventana(ServidorTcp *servidor, ModeloRed *modelo, quint16 puerto, QWidget *parent = nullptr);
    void seleccionarCelda(const QString &nombre);

private:
    void armarInterfaz();
    void seleccionar(const QString &celda);
    void actualizarDetalle();
    void actualizarResumen();
    void mandarComando(const QString &accion);

    ServidorTcp *m_servidor;
    ModeloRed *m_modelo;
    quint16 m_puerto;
    QString m_seleccionada;
    qint64 m_procesadosAntes = 0;
    double m_mensajesPorSegundo = 0;

    QTableView *m_tablaCeldas = nullptr;
    QTableView *m_tablaAlarmas = nullptr;
    QLabel *m_resumen = nullptr;
    QLabel *m_tituloDetalle = nullptr;
    QLabel *m_valoresDetalle = nullptr;
    QLabel *m_alarmasDetalle = nullptr;
    QLabel *m_sinReconocer = nullptr;
    GraficoRsrp *m_grafico = nullptr;
    QPushButton *m_btnReiniciar = nullptr;
    QPushButton *m_btnEstado = nullptr;
};
