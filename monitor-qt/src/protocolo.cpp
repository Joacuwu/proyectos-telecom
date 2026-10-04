#include "protocolo.h"

#include <QHash>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>

namespace protocolo {

namespace {

const QHash<QString, QStringList> &camposObligatorios()
{
    static const QHash<QString, QStringList> campos = {
        {"hola", {"origen"}},
        {"medicion", {"celda", "ts", "rsrp", "usuarios", "trafico_mbps", "temperatura"}},
        {"alarma", {"celda", "ts", "codigo", "severidad"}},
        {"comando", {"celda", "accion"}},
        {"respuesta", {"celda", "accion", "ok"}},
    };
    return campos;
}

Resultado error(const QString &texto)
{
    return Resultado{std::nullopt, texto};
}

}  // namespace

Resultado decodificar(const QByteArray &linea)
{
    if (linea.size() > kLargoMaximo)
        return error("línea demasiado larga");

    QJsonParseError parseo{};
    const QJsonDocument doc = QJsonDocument::fromJson(linea, &parseo);
    if (parseo.error != QJsonParseError::NoError)
        return error("JSON inválido: " + parseo.errorString());
    if (!doc.isObject())
        return error("se esperaba un objeto JSON");

    const QJsonObject obj = doc.object();
    const QString tipo = obj.value("tipo").toString();
    const auto it = camposObligatorios().constFind(tipo);
    if (it == camposObligatorios().constEnd())
        return error("tipo desconocido: " + tipo);

    QStringList faltan;
    for (const QString &campo : it.value())
        if (!obj.contains(campo))
            faltan << campo;
    if (!faltan.isEmpty())
        return error(QString("faltan campos en '%1': %2").arg(tipo, faltan.join(", ")));

    if (tipo == "medicion") {
        for (const char *numerico : {"rsrp", "usuarios", "trafico_mbps", "temperatura"})
            if (!obj.value(numerico).isDouble())
                return error(QString("'%1' debe ser numérico").arg(numerico));
    }
    if (tipo == "alarma") {
        static const QSet<QString> severidades = {"menor", "mayor", "critica"};
        if (!severidades.contains(obj.value("severidad").toString()))
            return error("severidad desconocida");
    }
    return Resultado{obj, {}};
}

QByteArray codificar(const QJsonObject &mensaje)
{
    return QJsonDocument(mensaje).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject comando(const QString &celda, const QString &accion)
{
    return QJsonObject{{"tipo", "comando"}, {"celda", celda}, {"accion", accion}};
}

}  // namespace protocolo
