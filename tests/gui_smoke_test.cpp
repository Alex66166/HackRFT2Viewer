#include "main_window.h"
#include <QApplication>
#include <QTemporaryFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QComboBox>
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
  {
   // Verify GUI can start, retune and stop an independent OnAir CLI without
   // HackRF hardware. This *mock* proves process lifecycle only; it is not
   // a proof that physical Windows DVB-T2 audio/video playback works.
   const QString dir=QCoreApplication::applicationDirPath()+QStringLiteral("/OnAir");
   const QString cli=dir+QStringLiteral("/onair-cli.exe");
   if(!QFileInfo::exists(cli)){
    assert(QDir().mkpath(dir));
    QFile mock(cli);assert(mock.open(QIODevice::WriteOnly|QIODevice::Truncate));
    assert(mock.write("#!/bin/sh\\necho 'quality: 80% MOCK'\\nsleep 15\\n")>0);
    mock.close();assert(mock.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|
                                          QFile::ReadGroup|QFile::ExeGroup|
                                          QFile::ReadOther|QFile::ExeOther));
    window.m_iqPath.clear();
    window.m_backendCombo->setCurrentIndex(window.m_backendCombo->findData(1));
    window.startReceiver();events(150);assert(window.isOnAirActive());
    assert(window.m_running && window.m_receiver==nullptr);
    window.tuneFrequency(482000000ULL);events(150);assert(window.isOnAirActive());
    window.stopReceiver();assert(!window.isOnAirActive() && !window.m_running);
    assert(!window.m_onair);
    assert(QFile::remove(cli));assert(QDir().rmdir(dir));
    qInfo()<<"Mock external OnAir lifecycle and retune PASS";
   }
  }
  window.updateServices({"Test channel"},{17});assert(window.m_found->rowCount()>0);
  auto *tabs=window.findChild<QTabWidget*>();assert(tabs);
  for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);events(50);window.grab().save(QCoreApplication::applicationDirPath()+QString("/gui-smoke-%1.png").arg(i));}
  qInfo()<<"GUI start / I/Q playback / retune / stop x3 PASS";
 }
};
int main(int argc,char **argv){QApplication app(argc,argv);GuiSmokeTest::run();}
