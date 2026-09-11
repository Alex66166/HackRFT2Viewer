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
#include "diagnostics.h"
int main(int argc,char *argv[])
{
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("HackRFT2Viewer");
    QCoreApplication::setApplicationName("HackRF T2 Viewer");
    QCoreApplication::setApplicationVersion(Diagnostics::version());
    qInstallMessageHandler(Diagnostics::messageHandler);
    qInfo().noquote() << Diagnostics::environment();
    QApplication::setFont(QFont("Segoe UI",10));
    MainWindow window;window.show();
    if(app.arguments().contains("--smoke-test"))QTimer::singleShot(1500,&app,&QCoreApplication::quit);
    return app.exec();
}
