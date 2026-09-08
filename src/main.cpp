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
static QMutex logMutex;
static void logMessage(QtMsgType type,const QMessageLogContext &,const QString &message)
{
    QMutexLocker lock(&logMutex);
    if(runtimeLog.isOpen()){QTextStream out(&runtimeLog);out.setCodec("UTF-8");out<<QDateTime::currentDateTime().toString(Qt::ISODateWithMs)<<" ["<<int(type)<<"] "<<message<<"\n";out.flush();}
}
int main(int argc,char *argv[])
{
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("HackRFT2Viewer");
    QCoreApplication::setApplicationName("HackRF T2 Viewer");
    QCoreApplication::setApplicationVersion("1.4.0");
    runtimeLog.setFileName(QCoreApplication::applicationDirPath()+"/HackRFT2Viewer.log");
    runtimeLog.open(QIODevice::WriteOnly|QIODevice::Text);qInstallMessageHandler(logMessage);
    qInfo()<<"HackRF T2 Viewer 1.4.0";
    QApplication::setFont(QFont("Segoe UI",10));
    MainWindow window;window.show();
    if(app.arguments().contains("--smoke-test"))QTimer::singleShot(1500,&app,&QCoreApplication::quit);
    return app.exec();
}
