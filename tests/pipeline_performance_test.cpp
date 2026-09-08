// A measured acquisition workload, not a hardware or full video throughput test.
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QSemaphore>
#include <QTemporaryDir>
#include <cassert>
#include "rx_hackrf_pro.h"
class ReceiverRegressionTest {
public:
 static void run(){
  HackRfSettings settings;settings.sampleRateHz=10000000;RxHackRfPro rx(settings);
  QByteArray bytes(262144,0);unsigned rng=319;
  for(auto&v:bytes){rng=rng*1664525u+1013904223u;v=char(int((rng>>24)&31)-16);}
  rx.m_running.store(true);rx.m_metricsTimer.start();rx.m_settleSamples=0;
  QElapsedTimer timer;timer.start();
  const int blocks=160;for(int i=0;i<blocks;++i){rx.processSamples(reinterpret_cast<uint8_t*>(bytes.data()),bytes.size());if(i%8==7)QMetaObject::invokeMethod(rx.m_demodulator,[]{},Qt::BlockingQueuedConnection);}
  QMetaObject::invokeMethod(rx.m_demodulator,[]{},Qt::BlockingQueuedConnection);
  double rate=blocks*131072.0/timer.nsecsElapsed()*1000;
  assert(rx.m_queueDrops.load()==0);qInfo()<<"Acquisition DSP throughput:"<<rate<<"MS/s; samples:"<<rx.m_demodulator->processedSamples;
  // Force DSP backlog. A raw capture must retain bytes also rejected by that queue.
  QTemporaryDir directory;assert(directory.isValid());QString path=directory.filePath("capture.cs8");
  rx.captureIq(path);QSemaphore entered,release;
  QMetaObject::invokeMethod(rx.m_demodulator,[&]{entered.release();release.acquire();},Qt::QueuedConnection);entered.acquire();
  QByteArray raw(262144,char(0x24));int total=0;const int expected=60000000;
  while(total<expected){const int n=qMin(raw.size(),expected-total);rx.processSamples(reinterpret_cast<uint8_t*>(raw.data()),n);total+=n;}
  assert(rx.m_queueDrops.load()>0);release.release();QMetaObject::invokeMethod(rx.m_demodulator,[]{},Qt::BlockingQueuedConnection);
  rx.processSamples(reinterpret_cast<uint8_t*>(raw.data()),raw.size());QMetaObject::invokeMethod(rx.m_demodulator,[]{},Qt::BlockingQueuedConnection);QCoreApplication::processEvents();
  QFile file(path);assert(file.open(QIODevice::ReadOnly));auto captured=file.readAll();assert(captured.size()==expected);for(auto b:captured)assert(b==char(0x24));
  qInfo()<<"Continuous 3s capture survives intentional DSP drops; stream reset PASS";rx.stop();
 }
};
int main(int argc,char**argv){QCoreApplication app(argc,argv);ReceiverRegressionTest::run();}
