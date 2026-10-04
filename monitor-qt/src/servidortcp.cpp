#include "servidortcp.h"

#include "conexioncliente.h"
#include "protocolo.h"

#include <QJsonArray>
#include <QMetaObject>

ServidorTcp::ServidorTcp(QObject *parent) : QTcpServer(parent)
{
    qRegisterMetaType<QJsonObject>("QJsonObject");
}

ServidorTcp::~ServidorTcp()
{
    detener();
}

void ServidorTcp::incomingConnection(qintptr descriptor)
{
    const int id = m_siguienteId++;
    auto *hilo = new QThread(this);
    hilo->setObjectName(QString("cliente-%1").arg(id));
    auto *conexion = new ConexionCliente(descriptor, id);
    conexion->moveToThread(hilo);

    connect(hilo, &QThread::started, conexion, &ConexionCliente::iniciar);
    connect(hilo, &QThread::finished, conexion, &QObject::deleteLater);
    // Estas conexiones cruzan de hilo: Qt encola la llamada y la ejecuta en el hilo del servidor
    connect(conexion, &ConexionCliente::mensajeRecibido, this, &ServidorTcp::alRecibir);
    connect(conexion, &ConexionCliente::mensajeDescartado, this,
            [this](int, const QString &motivo) { emit mensajeDescartado(motivo); });
    connect(conexion, &ConexionCliente::desconectado, this, &ServidorTcp::alDesconectar);

    m_clientes.insert(id, Cliente{hilo, conexion});
    hilo->start();
    emit clientesCambiaron(m_clientes.size());
}

void ServidorTcp::alRecibir(int id, const QJsonObject &mensaje)
{
    if (mensaje.value("tipo").toString() == "hola") {
        for (const auto &v : mensaje.value("celdas").toArray())
            m_celdaACliente.insert(v.toString(), id);
    }
    emit mensajeRecibido(mensaje);
}

void ServidorTcp::alDesconectar(int id)
{
    auto it = m_clientes.find(id);
    if (it == m_clientes.end())
        return;
    it->hilo->quit();
    it->hilo->wait(2000);
    it->hilo->deleteLater();
    m_clientes.erase(it);

    for (auto c = m_celdaACliente.begin(); c != m_celdaACliente.end();)
        c = (c.value() == id) ? m_celdaACliente.erase(c) : std::next(c);
    emit clientesCambiaron(m_clientes.size());
}

bool ServidorTcp::enviarComando(const QString &celda, const QString &accion)
{
    const auto idIt = m_celdaACliente.constFind(celda);
    if (idIt == m_celdaACliente.constEnd())
        return false;
    const auto cliente = m_clientes.constFind(idIt.value());
    if (cliente == m_clientes.constEnd())
        return false;

    // El socket pertenece al hilo del cliente: la escritura se encola en ese hilo
    QMetaObject::invokeMethod(cliente->conexion, "enviar", Qt::QueuedConnection,
                              Q_ARG(QByteArray, protocolo::codificar(protocolo::comando(celda, accion))));
    return true;
}

void ServidorTcp::detener()
{
    close();
    for (const Cliente &c : std::as_const(m_clientes)) {
        QMetaObject::invokeMethod(c.conexion, "cerrar", Qt::BlockingQueuedConnection);
        c.hilo->quit();
        c.hilo->wait(2000);
    }
    m_clientes.clear();
    m_celdaACliente.clear();
}
