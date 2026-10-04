#include "modelored.h"
#include "servidortcp.h"
#include "ventana.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("monitor-antenas");

    QCommandLineParser parser;
    parser.setApplicationDescription("Centro de monitoreo de antenas: recibe telemetría por TCP y la muestra en vivo.");
    parser.addHelpOption();
    QCommandLineOption puerto("puerto", "Puerto TCP donde escuchar (por defecto 5050).", "puerto", "5050");
    QCommandLineOption captura("captura", "Guarda una captura de la ventana en <archivo> y cierra (para la documentación).",
                               "archivo");
    QCommandLineOption capturaEn("captura-en", "Segundos a esperar antes de la captura (por defecto 8).", "segundos", "8");
    QCommandLineOption seleccionar("seleccionar", "Celda a seleccionar al iniciar.", "celda");
    parser.addOptions({puerto, captura, capturaEn, seleccionar});
    parser.process(app);

    bool ok = false;
    const quint16 numeroPuerto = parser.value(puerto).toUShort(&ok);
    if (!ok) {
        qCritical("Puerto inválido");
        return 1;
    }

    ModeloRed modelo;
    ServidorTcp servidor;
    QObject::connect(&servidor, &ServidorTcp::mensajeRecibido, &modelo, &ModeloRed::aplicar);
    if (!servidor.listen(QHostAddress::Any, numeroPuerto)) {
        qCritical("No se pudo escuchar en el puerto %u: %s", numeroPuerto, qPrintable(servidor.errorString()));
        return 1;
    }

    Ventana ventana(&servidor, &modelo, numeroPuerto);
    ventana.show();

    if (parser.isSet(captura)) {
        const QString archivo = parser.value(captura);
        const QString celda = parser.value(seleccionar);
        const int esperaMs = int(parser.value(capturaEn).toDouble() * 1000);
        // Se selecciona un segundo antes de capturar, cuando la celda ya llegó
        if (!celda.isEmpty())
            QTimer::singleShot(qMax(0, esperaMs - 1000), &ventana, [&ventana, celda] { ventana.seleccionarCelda(celda); });
        QTimer::singleShot(esperaMs, &ventana, [&ventana, archivo] {
            ventana.grab().save(archivo);
            QApplication::quit();
        });
    }
    return app.exec();
}
