#include "conexioncliente.h"

#include "protocolo.h"

ConexionCliente::ConexionCliente(qintptr descriptor, int id) : m_descriptor(descriptor), m_id(id) {}

ConexionCliente::~ConexionCliente() = default;

void ConexionCliente::iniciar()
{
    // El socket se crea acá y no en el constructor para que pertenezca al hilo del cliente
    m_socket = new QTcpSocket(this);
    if (!m_socket->setSocketDescriptor(m_descriptor)) {
        emit desconectado(m_id);
        return;
    }
    connect(m_socket, &QTcpSocket::readyRead, this, &ConexionCliente::leer);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] { emit desconectado(m_id); });
}

void ConexionCliente::leer()
{
    const QList<QByteArray> lineas = m_lector.agregar(m_socket->readAll());
    for (const QByteArray &linea : lineas) {
        const protocolo::Resultado r = protocolo::decodificar(linea);
        if (r.mensaje)
            emit mensajeRecibido(m_id, *r.mensaje);
        else
            emit mensajeDescartado(m_id, r.error);
    }
}

void ConexionCliente::enviar(const QByteArray &datos)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(datos);
}

void ConexionCliente::cerrar()
{
    if (m_socket)
        m_socket->disconnectFromHost();
}
