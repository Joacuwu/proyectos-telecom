#pragma once

#include "lectorlineas.h"

#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>

// Atiende a un simulador conectado. Cada instancia vive en su propio QThread:
// lee y valida los mensajes fuera del hilo de la interfaz, que solo recibe objetos ya armados.
class ConexionCliente : public QObject
{
    Q_OBJECT
public:
    ConexionCliente(qintptr descriptor, int id);
    ~ConexionCliente() override;

public slots:
    void iniciar();                         // se llama ya dentro del hilo del cliente
    void enviar(const QByteArray &datos);   // se invoca desde otro hilo con Qt::QueuedConnection
    void cerrar();

signals:
    void mensajeRecibido(int idCliente, const QJsonObject &mensaje);
    void mensajeDescartado(int idCliente, const QString &motivo);
    void desconectado(int idCliente);

private slots:
    void leer();

private:
    qintptr m_descriptor;
    int m_id;
    QTcpSocket *m_socket = nullptr;
    LectorLineas m_lector;
};
