// Pruebas del monitor con Qt Test: protocolo, armado de líneas, modelo y servidor TCP real.

#include "lectorlineas.h"
#include "modelored.h"
#include "protocolo.h"
#include "servidortcp.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QtTest>

namespace {

QJsonObject medicion(const QString &celda, double rsrp, double temp = 40)
{
    return {{"tipo", "medicion"}, {"celda", celda}, {"ts", "2026-10-01T12:00:00"}, {"rsrp", rsrp},
            {"usuarios", 300}, {"trafico_mbps", 100.5}, {"temperatura", temp}};
}

QJsonObject alarma(const QString &celda, const QString &codigo, const QString &severidad)
{
    return {{"tipo", "alarma"}, {"celda", celda}, {"ts", "2026-10-01T12:00:05"}, {"codigo", codigo},
            {"severidad", severidad}, {"detalle", "prueba"}};
}

}  // namespace

class TestMonitor : public QObject
{
    Q_OBJECT

private slots:
    // --- Protocolo
    void protocolo_acepta_mensaje_valido()
    {
        const auto r = protocolo::decodificar(protocolo::codificar(medicion("A", -90)).trimmed());
        QVERIFY(r.mensaje.has_value());
        QCOMPARE(r.mensaje->value("celda").toString(), QString("A"));
    }

    void protocolo_rechaza_invalidos_data()
    {
        QTest::addColumn<QByteArray>("linea");
        QTest::newRow("json roto") << QByteArray("{roto");
        QTest::newRow("no objeto") << QByteArray("[1,2]");
        QTest::newRow("tipo desconocido") << QByteArray(R"({"tipo":"otro"})");
        QTest::newRow("faltan campos") << QByteArray(R"({"tipo":"medicion","celda":"A"})");
        QTest::newRow("rsrp no numerico") << QByteArray(
            R"({"tipo":"medicion","celda":"A","ts":"x","rsrp":"-90","usuarios":1,"trafico_mbps":1,"temperatura":1})");
        QTest::newRow("severidad rara") << QByteArray(
            R"({"tipo":"alarma","celda":"A","ts":"x","codigo":"X","severidad":"catastrofica"})");
        QTest::newRow("demasiado larga") << QByteArray(5000, 'x');
    }
    void protocolo_rechaza_invalidos()
    {
        QFETCH(QByteArray, linea);
        const auto r = protocolo::decodificar(linea);
        QVERIFY(!r.mensaje.has_value());
        QVERIFY(!r.error.isEmpty());
    }

    // --- Lector de líneas
    void lector_arma_mensajes_partidos_y_juntos()
    {
        LectorLineas lector;
        QVERIFY(lector.agregar(R"({"a":)").isEmpty());
        QCOMPARE(lector.agregar("1}\n{\"b\":2}\n{\"c\""), (QList<QByteArray>{R"({"a":1})", R"({"b":2})"}));
        QCOMPARE(lector.agregar(":3}\n"), QList<QByteArray>{R"({"c":3})"});
    }

    void lector_descarta_basura_sin_salto_de_linea()
    {
        LectorLineas lector(100);
        lector.agregar(QByteArray(150, 'x'));
        QCOMPARE(lector.descartadas(), 1);
        QCOMPARE(lector.agregar("{}\n"), QList<QByteArray>{"{}"});
    }

    // --- Modelo
    void modelo_crea_celdas_en_orden_y_guarda_valores()
    {
        ModeloRed m;
        QVERIFY(m.aplicar(medicion("TIGRE-001", -95.5)));
        QVERIFY(m.aplicar(medicion("CABA-001", -88)));
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.nombreEnFila(0), QString("CABA-001"));
        QCOMPARE(m.celda("TIGRE-001")->rsrp, -95.5);
        QCOMPARE(m.data(m.index(1, ModeloRed::ColRsrp), Qt::DisplayRole).toString(), QString("-95.5 dBm"));
    }

    void modelo_abre_y_cierra_alarmas()
    {
        ModeloRed m;
        m.aplicar(medicion("A", -90));
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Normal);
        m.aplicar(alarma("A", "SENAL_DEBIL", "mayor"));
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Advertencia);
        m.aplicar(alarma("A", "TEMPERATURA_ALTA", "critica"));
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Critica);
        m.aplicar(alarma("A", "TEMPERATURA_ALTA_NORMALIZADA", "menor"));
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Advertencia);
        m.aplicar(alarma("A", "SENAL_DEBIL_NORMALIZADA", "menor"));
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Normal);
        QCOMPARE(m.alarmas()->rowCount(), 4);
        QCOMPARE(m.alarmas()->sinReconocer(), 2);  // las normalizaciones no cuentan
        m.alarmas()->reconocerTodas();
        QCOMPARE(m.alarmas()->sinReconocer(), 0);
    }

    void modelo_enlace_caido_y_restablecido()
    {
        ModeloRed m;
        m.aplicar(medicion("A", -90));
        m.aplicar(alarma("A", "ENLACE_CAIDO", "critica"));
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Caida);
        m.aplicar(alarma("A", "ENLACE_RESTABLECIDO", "menor"));
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Normal);
    }

    void modelo_detecta_celdas_silenciosas()
    {
        ModeloRed m;
        qint64 ahora = 0;
        m.setRelojMs([&ahora] { return ahora; });
        m.aplicar(medicion("A", -90));
        ahora = 4000;
        m.revisarSilencios(5000);
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Normal);
        ahora = 6000;
        m.revisarSilencios(5000);
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::SinDatos);
        m.aplicar(medicion("A", -90));
        QCOMPARE(m.celda("A")->estado(), EstadoCelda::Normal);
    }

    void modelo_historial_limitado()
    {
        ModeloRed m;
        for (int i = 0; i < ModeloRed::kHistorial + 25; ++i)
            m.aplicar(medicion("A", -90 - i % 10));
        QCOMPARE(m.celda("A")->historialRsrp.size(), ModeloRed::kHistorial);
        QCOMPARE(m.celda("A")->mediciones, ModeloRed::kHistorial + 25);
    }

    // --- Servidor TCP real (cada cliente en su hilo)
    void servidor_recibe_de_varios_clientes_y_enruta_comandos()
    {
        ServidorTcp servidor;
        ModeloRed modelo;
        connect(&servidor, &ServidorTcp::mensajeRecibido, &modelo, &ModeloRed::aplicar);
        QVERIFY(servidor.listen(QHostAddress::LocalHost, 0));

        QTcpSocket a, b;
        a.connectToHost(QHostAddress::LocalHost, servidor.serverPort());
        b.connectToHost(QHostAddress::LocalHost, servidor.serverPort());
        QVERIFY(a.waitForConnected(3000));
        QVERIFY(b.waitForConnected(3000));

        a.write(protocolo::codificar({{"tipo", "hola"}, {"origen", "sim"}, {"celdas", QJsonArray{"A-1", "A-2"}}}));
        b.write(protocolo::codificar({{"tipo", "hola"}, {"origen", "sim"}, {"celdas", QJsonArray{"B-1"}}}));
        // Un mensaje partido en dos escrituras y uno inválido en el medio
        const QByteArray m = protocolo::codificar(medicion("A-1", -101));
        a.write(m.left(20));
        a.flush();
        QTest::qWait(50);
        a.write(m.mid(20) + "{basura}\n" + protocolo::codificar(medicion("A-2", -95)));
        b.write(protocolo::codificar(medicion("B-1", -85)));

        QTRY_COMPARE_WITH_TIMEOUT(servidor.clientesConectados(), 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(modelo.celda("A-1") && modelo.celda("A-1")->mediciones == 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(modelo.celda("B-1") && modelo.celda("B-1")->mediciones == 1, 3000);
        QCOMPARE(modelo.celda("A-1")->rsrp, -101.0);

        // El comando para B-1 tiene que llegar solo al cliente b
        QVERIFY(servidor.enviarComando("B-1", "reiniciar"));
        QVERIFY(!servidor.enviarComando("NO-EXISTE", "reiniciar"));
        QVERIFY(b.waitForReadyRead(3000));
        const auto r = protocolo::decodificar(b.readLine().trimmed());
        QVERIFY(r.mensaje.has_value());
        QCOMPARE(r.mensaje->value("accion").toString(), QString("reiniciar"));
        QCOMPARE(r.mensaje->value("celda").toString(), QString("B-1"));
        QVERIFY(!a.waitForReadyRead(300));

        b.disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(servidor.clientesConectados(), 1, 3000);
        QVERIFY(!servidor.enviarComando("B-1", "estado"));  // ya no hay quien la maneje
    }
};

QTEST_GUILESS_MAIN(TestMonitor)
#include "test_monitor.moc"
