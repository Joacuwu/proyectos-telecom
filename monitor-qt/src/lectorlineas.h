#pragma once

#include <QByteArray>
#include <QList>

// TCP es un flujo de bytes: un read() puede traer medio mensaje o varios juntos.
// Esta clase acumula lo recibido y devuelve solo las líneas completas.
class LectorLineas
{
public:
    explicit LectorLineas(int largoMaximo = 4096);

    QList<QByteArray> agregar(const QByteArray &datos);
    int descartadas() const { return m_descartadas; }

private:
    QByteArray m_buffer;
    int m_largoMaximo;
    int m_descartadas = 0;
};
