#include "grafico.h"

#include <QPainter>
#include <QPainterPath>

GraficoRsrp::GraficoRsrp(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(140);
}

void GraficoRsrp::setDatos(const QVector<double> &valores)
{
    m_valores = valores;
    update();
}

void GraficoRsrp::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF area = rect().adjusted(46, 10, -10, -20);
    const auto y = [&](double v) { return area.bottom() - (v - kMin) / (kMax - kMin) * area.height(); };

    p.fillRect(rect(), palette().base());

    // Franja de señal débil (por debajo de -110 dBm)
    p.fillRect(QRectF(area.left(), y(kUmbral), area.width(), area.bottom() - y(kUmbral)), QColor(254, 226, 226));

    // Grilla y etiquetas del eje
    p.setPen(QColor(203, 213, 225));
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    for (double v : {-120.0, -110.0, -100.0, -90.0, -80.0}) {
        p.setPen(QColor(226, 232, 240));
        p.drawLine(QPointF(area.left(), y(v)), QPointF(area.right(), y(v)));
        p.setPen(QColor(100, 116, 139));
        p.drawText(QRectF(0, y(v) - 8, area.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', 0));
    }
    p.drawText(QRectF(area.left(), area.bottom() + 2, area.width(), 18), Qt::AlignCenter,
               QString("RSRP (dBm) · últimas %1 mediciones").arg(m_valores.size()));

    if (m_valores.size() < 2)
        return;

    QPainterPath linea;
    const double paso = area.width() / (m_valores.size() - 1);
    for (int i = 0; i < m_valores.size(); ++i) {
        const QPointF punto(area.left() + i * paso, y(qBound(kMin, m_valores[i], kMax)));
        i == 0 ? linea.moveTo(punto) : linea.lineTo(punto);
    }
    p.setPen(QPen(QColor(37, 99, 235), 2));
    p.drawPath(linea);

    // Último valor destacado
    const double ultimo = m_valores.last();
    const QPointF fin(area.right(), y(qBound(kMin, ultimo, kMax)));
    p.setBrush(ultimo < kUmbral ? QColor(220, 38, 38) : QColor(37, 99, 235));
    p.setPen(Qt::NoPen);
    p.drawEllipse(fin, 4, 4);
}
