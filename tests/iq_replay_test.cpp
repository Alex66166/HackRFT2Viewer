// Offline end-to-end diagnostic replay, with producer backpressure.
// This is a correctness/throughput check, not a USB real-time stress test.
#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QFileInfo>
#include "rx_hackrf_pro.h"
#include "diagnostics.h"
class ReceiverRegressionTest {
public:
    static int run(const QString &path,const QString &output,double rate) {
        QFile input(path);
        if(QFileInfo(path).absoluteFilePath()==QFileInfo(output).absoluteFilePath() ||
           !input.open(QIODevice::ReadOnly) || input.size()==0 || (input.size()&1)) {
            qCritical()<<"Input must be a nonempty cs8 file; output must be a different path.";return 2;
        }
        HackRfSettings settings;settings.sampleRateHz=rate;RxHackRfPro rx(settings);
        auto *demod=rx.demodulator();auto *ti=demod->deinterleaver;auto *qam=ti->qam;
        auto *ldpc=qam->decoder;auto *bch=ldpc->decoder;auto *bb=bch->deheader;
        TransportMetrics metrics;
        QObject::connect(&rx,&RxHackRfPro::radioError,[](QString s){qWarning()<<s;});
        QObject::connect(demod,&dvbt2_demodulator::receiver_stage,[](QString s){qInfo()<<s;});
        QObject::connect(demod,&dvbt2_demodulator::l1_dyn_execute,demod,[](l1_postsignalling p,int,complex*){
            for(int i=0;i<p.num_plp;++i){const auto &s=p.plp[i];qInfo()<<"PLP"<<s.id<<"type/mod/rotation/fec/rate/ti"<<s.plp_type<<s.plp_mod<<s.plp_rotation<<s.plp_fec_type<<s.plp_cod<<s.time_il_length<<"blocks"<<p.dyn.plp[i].num_blocks;}
        },Qt::DirectConnection);
        QObject::connect(bb,&bb_de_header::transport_metrics,bb,[&](TransportMetrics m){metrics=m;},Qt::DirectConnection);
        QObject::connect(bb,&bb_de_header::services_changed,[](QStringList s,QList<int>){qInfo()<<"SERVICES"<<s;});
        QMetaObject::invokeMethod(bb,[&]{bb->set_network_output(false,7654);bb->set_recording(true,output);},Qt::BlockingQueuedConnection);
        rx.m_running.store(true);rx.m_metricsTimer.start();rx.m_settleSamples=0;
        QElapsedTimer clock;clock.start();qint64 bytes=0;
        while(!input.atEnd()) {
            const auto block=input.read(262144);if(block.isEmpty())break;bytes+=block.size();
            rx.processSamples(reinterpret_cast<const uint8_t*>(block.constData()),block.size());
            QMetaObject::invokeMethod(demod,[]{},Qt::BlockingQueuedConnection);QCoreApplication::processEvents();
        }
        QMetaObject::invokeMethod(ti,[]{},Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(qam,[&]{qam->flushPending();},Qt::BlockingQueuedConnection);
        for(QObject *stage:{static_cast<QObject*>(ldpc),static_cast<QObject*>(bch),static_cast<QObject*>(bb)})
            QMetaObject::invokeMethod(stage,[]{},Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(bb,[&]{bb->set_recording(false,QString());metrics=bb->snapshotMetrics();},Qt::BlockingQueuedConnection);
        const qint64 tsBytes=QFileInfo(output).size();
        qInfo()<<"RESULT bytes"<<bytes<<"sample_rate"<<rate<<"input_ms"<<bytes*500.0/rate
               <<"elapsed_ms"<<clock.elapsed()<<"drops"<<rx.m_queueDrops.load()
               <<"P1/L1pre/L1post"<<demod->p1Matches<<demod->l1PreMatches<<demod->l1PostMatches
               <<"BCH total/failed"<<metrics.bchFrames<<metrics.bchFailedFrames<<"TSbytes"<<tsBytes;
        rx.stop();return tsBytes>0?0:3;
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(argc<3||argc>4){qInfo()<<"Usage: iq_replay_test INPUT.cs8 OUTPUT.ts [sample_rate_hz=10000000]; exit 3 means no TS.";return 2;}
    bool ok=true;const double rate=argc==4?QString::fromLocal8Bit(argv[3]).toDouble(&ok):1e7;
    if(!ok||rate<8e6||rate>20e6)return 2;
    qInfo().noquote()<<Diagnostics::environment();
    return ReceiverRegressionTest::run(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]),rate);
}
