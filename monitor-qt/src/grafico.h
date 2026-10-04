#pragma once

#include <QVector>
#include <QWidget>

// Gráfico de línea simple dibujado con QPainter (sin depender de Qt Charts).
// Muestra el RSRP reciente de una celda con la franja de señal débil marcada.
class GraficoRsrp : public QWidget
{
    Q_OBJECT
public:
    explicit GraficoRsrp(QWidget *parent = nullptr);
    void setDatos(const QVector<double> &valores);
    QSize sizeHint() const override { return {360, 160}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_valores;
    static constexpr double kMin = -125.0;
    static constexpr double kMax = -70.0;
    static constexpr double kUmbral = -110.0;
};
