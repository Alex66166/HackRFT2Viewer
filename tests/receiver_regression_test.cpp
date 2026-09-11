#define P1_NO_MAIN
#include "p1_acquisition_test.cpp"
#include "rx_hackrf_pro.h"
#include <QMutex>
#include <QMetaObject>
class ReceiverRegressionTest {
public:
 static void run(){
  iq_correct<int8_t> iq(7,.04f,.02f);std::vector<int8_t> zero(2048),nonzero(2048,12);std::vector<complex> corrected(1024);int gain=0;
  iq.execute(size_t(1024),zero.data(),corrected.data(),gain);iq.execute(size_t(1024),nonzero.data(),corrected.data(),gain);
  for(auto c:corrected)assert(std::isfinite(c.real())&&std::isfinite(c.imag()));
  for(double rate:{8000000.,10000000.,12500000.,16000000.,20000000.}){
   HackRfSettings settings;settings.sampleRateHz=rate;RxHackRfPro rx(settings);p1_symbol p;auto reference=P1AcquisitionTest::waveform(p,.1f);
   int count=int(reference.size()*rate/SAMPLE_RATE);QByteArray bytes(count*2,0);
   for(int i=0;i<count;++i){
    double t=i*SAMPLE_RATE/rate;int center=int(std::floor(t));complex sample{};
    for(int k=center-32;k<=center+32;++k){
     double x=t-k;if(k<0||k>=int(reference.size())||std::abs(x)>=32)continue;
     double sinc=std::abs(x)<1e-10?1:std::sin(M_PI*x)/(M_PI*x);
     sample+=reference[k]*float(sinc*(.5+.5*std::cos(M_PI*x/32)));
    }
    bytes[2*i]=char(qBound(-127,int(std::lround(sample.real()*128)),127));
    bytes[2*i+1]=char(qBound(-127,int(std::lround(sample.imag()*128)),127));
   }
   rx.m_running.store(true);rx.m_metricsTimer.start();rx.m_settleSamples=0;
   for(int pos=0;pos<bytes.size();pos+=4094){int n=qMin(4094,bytes.size()-pos);rx.processSamples(reinterpret_cast<uint8_t*>(bytes.data()+pos),n);std::fill(bytes.begin()+pos,bytes.begin()+pos+n,char(0x7f));}
   QMetaObject::invokeMethod(rx.m_demodulator,[]{},Qt::BlockingQueuedConnection);
   assert(rx.m_pendingBlocks.load()==0);assert(rx.m_demodulator->p1Matches>0);
   qInfo()<<"Owned I/Q queue P1 at"<<rate<<"PASS";rx.stop();
  }
  {
   HackRfSettings settings;RxHackRfPro rx(settings);QSemaphore entered,release;
   rx.m_running.store(true);rx.m_metricsTimer.start();rx.m_settleSamples=0;
   QMetaObject::invokeMethod(rx.m_demodulator,[&]{entered.release();release.acquire();},Qt::QueuedConnection);
   entered.acquire();QByteArray bytes(4096,0);
   for(int i=0;i<40;++i)rx.processSamples(reinterpret_cast<const uint8_t*>(bytes.constData()),bytes.size());
   assert(rx.m_queueDrops.load()==24);assert(rx.m_pendingBlocks.load()==16);
   release.release();QMetaObject::invokeMethod(rx.m_demodulator,[]{},Qt::BlockingQueuedConnection);
   assert(rx.m_pendingBlocks.load()==0);rx.stop();
   qInfo()<<"Bounded queue sheds 24 stale blocks and drains remaining 16 PASS";
  }
  QMutex mutex;
  {
   time_deinterleaver ti(&mutex);QObject::disconnect(&ti,&time_deinterleaver::ti_block,ti.qam,&llr_demapper::execute);
   l1_postsignalling_plp plp;plp.id=17;plp.plp_num_blocks_max=1;plp.time_il_length=1;plp.frame_interval=1;
   dynamic_plp dyn;dyn.id=17;dyn.num_blocks=1;l1_postsignalling post;post.num_plp=1;post.plp=&plp;post.dyn.plp=&dyn;
   ti.start(dvbt2_parameters{},l1_presignalling{},post);ti.l1_dyn_execute(post,0,nullptr);assert(ti.slice_end[0]==8099);
   bool emitted=false;
   QObject::connect(&ti,&time_deinterleaver::ti_block,&ti,[&](int n,complex *data,int index,l1_postsignalling){
    assert(index==0&&n==8100);for(int i=0;i<n;++i)assert(data[i].imag()==2*data[i].real());emitted=true;
   },Qt::DirectConnection);
   std::vector<complex> data(L1_PRE_CELL+8100);for(int i=0;i<int(data.size());++i)data[i]=complex(float(i+1),float(2*(i+1)));
   ti.execute(data.size(),data.data());assert(emitted);qInfo()<<"Single PLP ID17 slice and non-rotated I/Q pairing PASS";
  }
  qInfo()<<"TI destruction PASS; constructing QAM";
  {
   llr_demapper qam(&mutex);QObject::disconnect(&qam,&llr_demapper::soft_multiplexer_de_twist,qam.decoder,&ldpc_decoder::execute);
   qInfo()<<"QAM construction PASS";
   for(int type=0;type<2;++type)for(int mod=0;mod<4;++mod){qInfo()<<"QAM begin"<<type<<mod;
    int fec=type?64800:16200,total=0,calls=0;
    l1_postsignalling_plp plps[2];plps[1].id=203;plps[1].plp_fec_type=type;plps[1].plp_mod=mod;
    l1_postsignalling post;post.num_plp=2;post.plp=plps;
    auto connection=QObject::connect(&qam,&llr_demapper::soft_multiplexer_de_twist,&qam,[&](int *ids,l1_postsignalling,int n,int8_t *bits){
     assert(n>0&&n<=32*fec&&n%fec==0);for(int i=0;i<n/fec;++i)assert(ids[i]==1);for(int i=0;i<n;++i)assert(bits[i]!=0);total+=n;++calls;
    },Qt::DirectConnection);
    const float norm[]={float(NORM_FACTOR_QPSK),float(NORM_FACTOR_QAM16),float(NORM_FACTOR_QAM64),float(NORM_FACTOR_QAM256)};
    std::vector<complex> cells(33*fec/(2*(mod+1)),complex(norm[mod],norm[mod]));
    qam.execute(cells.size(),cells.data(),1,post);qam.flushPending();assert(total==33*fec&&calls==2);QObject::disconnect(connection);
   }
   qInfo()<<"All constellations, short/normal FEC, 32+1 lanes PLP index1 PASS";
   QMetaObject::invokeMethod(qam.decoder,[]{},Qt::BlockingQueuedConnection);
   int ids[32]={1};std::vector<int8_t> bits(16200,48);bool delivered=false;
   l1_postsignalling_plp plps[2];plps[1].id=17;l1_postsignalling post;post.num_plp=2;post.plp=plps;
   QObject::connect(qam.decoder,&ldpc_decoder::bit_bch,&qam,[&](int *indexes,l1_postsignalling,int n,uint8_t *data){
    assert(indexes[0]==1&&n==7200);for(int i=0;i<n;++i)assert(data[i]==0);delivered=true;
   },Qt::DirectConnection);
   QMetaObject::invokeMethod(qam.decoder,[&]{qam.decoder->execute(ids,post,bits.size(),bits.data());},Qt::BlockingQueuedConnection);
   assert(delivered);qInfo()<<"LDPC single lane through BCH PASS";
  }
 }
};
int main(int argc,char **argv){QCoreApplication app(argc,argv);ReceiverRegressionTest::run();qInfo()<<"receiver_regression_test PASS";}
