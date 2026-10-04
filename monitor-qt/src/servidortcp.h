#pragma once

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QThread>

class ConexionCliente;

// Servidor TCP multihilo: cada simulador conectado se atiende en su propio hilo.
// Las señales hacia la interfaz cruzan de hilo con conexiones encoladas (Qt::QueuedConnection).
class ServidorTcp : public QTcpServer
{
    Q_OBJECT
public:
    explicit ServidorTcp(QObject *parent = nullptr);
    ~ServidorTcp() override;

    // Envía un comando al simulador que maneja esa celda. Devuelve false si nadie la maneja.
    bool enviarComando(const QString &celda, const QString &accion);
    int clientesConectados() const { return m_clientes.size(); }
    void detener();

signals:
    void mensajeRecibido(const QJsonObject &mensaje);
    void mensajeDescartado(const QString &motivo);
    void clientesCambiaron(int cantidad);

protected:
    void incomingConnection(qintptr descriptor) override;

private:
    struct Cliente {
        QThread *hilo = nullptr;
        ConexionCliente *conexion = nullptr;
    };

    void alRecibir(int id, const QJsonObject &mensaje);
    void alDesconectar(int id);

    QHash<int, Cliente> m_clientes;
    QHash<QString, int> m_celdaACliente;  // de "hola": qué simulador maneja cada celda
    int m_siguienteId = 1;
};
