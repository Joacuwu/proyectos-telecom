#include "lectorlineas.h"

LectorLineas::LectorLineas(int largoMaximo) : m_largoMaximo(largoMaximo) {}

QList<QByteArray> LectorLineas::agregar(const QByteArray &datos)
{
    m_buffer.append(datos);
    QList<QByteArray> lineas;

    qsizetype fin;
    while ((fin = m_buffer.indexOf('\n')) >= 0) {
        QByteArray linea = m_buffer.left(fin).trimmed();
        m_buffer.remove(0, fin + 1);
        if (linea.size() > m_largoMaximo) {
            ++m_descartadas;
            continue;
        }
        if (!linea.isEmpty())
            lineas.append(linea);
    }

    // Un cliente que nunca manda '\n' no puede hacer crecer la memoria sin límite
    if (m_buffer.size() > m_largoMaximo) {
        m_buffer.clear();
        ++m_descartadas;
    }
    return lineas;
}
