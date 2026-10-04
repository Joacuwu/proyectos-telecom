#pragma once

// Mensajes del protocolo de telemetría (ver ../PROTOCOLO.md): un objeto JSON por línea.

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace protocolo {

constexpr int kLargoMaximo = 4096;

struct Resultado {
    std::optional<QJsonObject> mensaje;  // vacío si la línea no es válida
    QString error;
};

// Valida JSON, tipo y campos obligatorios.
Resultado decodificar(const QByteArray &linea);

// Devuelve la línea JSON terminada en '\n'.
QByteArray codificar(const QJsonObject &mensaje);

QJsonObject comando(const QString &celda, const QString &accion);

}  // namespace protocolo
