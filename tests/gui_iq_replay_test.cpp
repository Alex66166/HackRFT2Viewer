// GUI integration against the independent GNU Radio fixture. Unlike the CLI
// replay, this uses MainWindow and the normal file timer/backpressure path.
#include "main_window.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QThread>
#include <QDebug>

class GuiSmokeTest {
    static void events(int ms) {
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < ms) {
            QCoreApplication::processEvents(); QThread::msleep(2);
        }
    }
public:
    static int run(const QString &input, const QString &output) {
        QFile metadata(QFileInfo(input).absolutePath() + "/fixture.json");
        if (!metadata.open(QIODevice::ReadOnly) || !QFileInfo::exists(input) ||
            QFileInfo(input).absoluteFilePath() == QFileInfo(output).absoluteFilePath()) return 2;
        const auto fixture = QJsonDocument::fromJson(metadata.readAll()).object();
        const int expectedPackets = fixture["end_packet"].toInt() - fixture["first_packet"].toInt();
        const int expectedFec = fixture["expected_fec"].toInt();
        if (expectedPackets <= 0 || expectedFec <= 0) return 2;

        MainWindow window; window.show(); window.m_iqPath = input;
        window.m_sampleRate->setCurrentIndex(window.m_sampleRate->findData(10000000.0));
        window.m_udpEnabled->setChecked(false);
        window.startReceiver();
        auto *transport = window.m_transport;
        QMetaObject::invokeMethod(transport, [&]{ transport->set_recording(true, output); },
                                  Qt::BlockingQueuedConnection);
        TransportMetrics metrics;
        QElapsedTimer deadline; deadline.start();
        // Let the application's own partial-batch timer flush the tail. Do not
        // bypass the GUI input path or manually flush the DSP as the CLI does.
        while (deadline.elapsed() < 20000) {
            events(100);
            QMetaObject::invokeMethod(transport, [&]{ metrics = transport->snapshotMetrics(); },
                                      Qt::BlockingQueuedConnection);
            if (metrics.packets >= quint64(expectedPackets)) break;
        }
        events(300); // Include any last queued SNR/UI update after service discovery.
        QMetaObject::invokeMethod(transport, [&]{
            metrics = transport->snapshotMetrics(); transport->set_recording(false, QString());
        }, Qt::BlockingQueuedConnection);
        bool valid = metrics.packets == quint64(expectedPackets) &&
                     metrics.bchFrames == quint64(expectedFec) && metrics.bchFailedFrames == 0 &&
                     QFileInfo(output).size() == qint64(expectedPackets) * 188;
        const auto expectedService = qEnvironmentVariable("EXPECTED_SERVICE");
        if (!expectedService.isEmpty()) {
            bool found = false;
            for (int i = 0; i < window.m_serviceCombo->count(); ++i)
                found |= window.m_serviceCombo->itemText(i).contains(expectedService);
            valid &= found && window.m_lockValue->text() == QStringLiteral("TS LOCK");
        }
        qInfo() << "GUI I/Q replay" << (valid ? "PASS" : "FAIL")
                << "BCH total/failed" << metrics.bchFrames << metrics.bchFailedFrames
                << "TS packets" << metrics.packets << "services" << window.m_serviceCount
                << "status" << window.m_lockValue->text();
        window.stopReceiver();
        return valid ? 0 : 1;
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 3) return 2;
    return GuiSmokeTest::run(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
}
