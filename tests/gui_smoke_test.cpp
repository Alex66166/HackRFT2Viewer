#include "main_window.h"
#include <QApplication>
#include <QTemporaryFile>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QTableWidget>
#include <QTabWidget>
#include <QLabel>
#include <cassert>
class GuiSmokeTest {
 static void events(int ms){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
public:
 static void run(){
  QTemporaryFile input;assert(input.open());assert(input.write(QByteArray(16000000,0))==16000000);input.flush();
  MainWindow window;window.show();window.m_iqPath=input.fileName();
  for(int i=0;i<3;++i){window.startReceiver();events(800);assert(window.m_running&&window.m_radioMetrics.callbackBlocks>0);assert(window.m_lockValue->text()!=QStringLiteral("СТОП"));window.tuneFrequency(474000000ULL+i*8000000ULL);events(650);assert(window.m_running);window.stopReceiver();events(50);assert(!window.m_receiver&&!window.m_radioThread.isRunning());}
  window.updateServices({"Test channel"},{17});assert(window.m_found->rowCount()>0);
  auto *tabs=window.findChild<QTabWidget*>();assert(tabs);
  for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);events(50);window.grab().save(QCoreApplication::applicationDirPath()+QString("/gui-smoke-%1.png").arg(i));}
  qInfo()<<"GUI start / I/Q playback / retune / stop x3 PASS";
 }
};
int main(int argc,char **argv){QApplication app(argc,argv);GuiSmokeTest::run();}
