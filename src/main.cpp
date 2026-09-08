/* HackRF T2 Viewer, GPL-3.0-or-later */
#include "main_window.h"
#include <QApplication>
#include <QFont>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QDebug>
static QFile runtimeLog;
static QFile perfLog;
static QMutex logMutex;
static void logMessage(QtMsgType type,const QMessageLogContext &,const QString &message)
{
    QMutexLocker lock(&logMutex);
    const QString stamp=QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    if(runtimeLog.isOpen()){
        QTextStream out(&runtimeLog);out.setCodec("UTF-8");out<<stamp<<" ["<<int(type)<<"] "<<message<<"\n";out.flush();
    }
    if(perfLog.isOpen() && (message.startsWith(QStringLiteral("PERF-")) || message.startsWith(QStringLiteral("FEC-")))){
        QTextStream out(&perfLog);out.setCodec("UTF-8");out<<stamp<<" "<<message<<"\n";out.flush();
    }
}
int main(int argc,char *argv[])
{
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("HackRFT2Viewer");
    QCoreApplication::setApplicationName("HackRF T2 Viewer");
    QCoreApplication::setApplicationVersion("1.4.1-DIAG");
    runtimeLog.setFileName(QCoreApplication::applicationDirPath()+"/HackRFT2Viewer.log");
    perfLog.setFileName(QCoreApplication::applicationDirPath()+"/HackRFT2_FEC_PERF.log");
    runtimeLog.open(QIODevice::WriteOnly|QIODevice::Text);
    perfLog.open(QIODevice::WriteOnly|QIODevice::Text);
    qInstallMessageHandler(logMessage);
    qInfo()<<"HackRF T2 Viewer 1.4.1-DIAG";
    QApplication::setFont(QFont("Segoe UI",10));
    MainWindow window;window.show();
    if(app.arguments().contains("--smoke-test"))QTimer::singleShot(1500,&app,&QCoreApplication::quit);
    return app.exec();
}
