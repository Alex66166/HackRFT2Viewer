#include <chrono>
#include <thread>
#include <QTemporaryDir>
#include <QFileInfo>
#include "fec_reference.h"
#define P2_NO_MAIN
#include "p2_acquisition_test.cpp"
void P2AcquisitionTest::fullFrame(double snr){
  // Full TS -> BB/BCH/LDPC -> rotated 64-QAM -> TI -> OFDM -> CS8 -> TS.
  // Pilot and frequency-address tables are shared with the receiver.
  fullPayload=true;
  const bool realtime=qEnvironmentVariableIsSet("REALTIME_RF");
  const int frameCount=realtime?12:4;
  std::vector<std::vector<uint8_t>> expected;
  std::vector<std::vector<complex>> encodedFrames;
  QByteArray expectedTs;
  for(int word=0;word<108;++word){
   QByteArray packet(188,char(0xff));packet[0]=0x47;packet[1]=0x01;packet[2]=0;packet[3]=0x10;
   std::mt19937 payloadRandom(755+word);QByteArray data;
   for(int i=0;i<34;++i){packet[3]=char(0x10|((word*34+i)&15));for(int j=4;j<188;++j)packet[j]=char(payloadRandom());expectedTs+=packet;data+=packet.mid(1);}
   QByteArray head=QByteArray::fromHex("f00000000000470000");head[4]=char(data.size()*8>>8);head[5]=char(data.size()*8);
   uint8_t crc=0;for(unsigned char b:head){crc^=b;for(int j=0;j<8;++j)crc=uint8_t((crc<<1)^((crc&128)?0xd5:0));}head.append(char(crc^1));head+=data;head.append(QByteArray(6456-head.size(),0));
   std::vector<uint8_t> info(51648);int sr=0x4a80;for(int i=0;i<51648;++i){int bit=(sr^(sr>>1))&1;info[i]=((uint8_t(head[i/8])>>(7-i%8))&1)^bit;sr=(sr>>1)|(bit<<14);}
   expected.push_back(Reference::bch(51840,51648,false,1,&info));encodedFrames.push_back(Reference::qam64(Reference::ldpc45(expected.back())));
  }
  const auto &fec=encodedFrames.front();auto payloadCells=Reference::interleave(fec,108,3,&encodedFrames);
  dvbt2_parameters params{};params.fft_mode=FFTSIZE_32K;params.preamble=T2_SISO;params.carrier_mode=1;params.pilot_pattern=PP4;params.guard_interval_mode=GI_1_16;params.n_data=63;
  dvbt2_p2_parameters_init(params);dvbt2_bwt_ext_parameters_init(params);dvbt2_data_parameters_init(params);
  pilot_generator pilot;address_freq_deinterleaver address;address.init(params);p2_symbol p;p.init(params,&pilot,&address);pilot.data_generator(params);address.data_address_freq_deinterleaver(params);
  std::vector<complex> p2cells=encoded(GI_1_16);auto post=postCells(0,false);p2cells.insert(p2cells.end(),post.begin(),post.end());size_t payloadOffset=0;
  while(int(p2cells.size())<params.c_p2)p2cells.push_back(payloadCells[payloadOffset++]);
  std::vector<complex> freq(32768),unshifted(32768),time(32768),frame;
  auto plan=fftwf_plan_dft_1d(32768,reinterpret_cast<fftwf_complex*>(unshifted.data()),reinterpret_cast<fftwf_complex*>(time.data()),FFTW_BACKWARD,FFTW_ESTIMATE);
  p1_symbol p1;auto prefix=P1AcquisitionTest::waveform(p1,.1f);frame.insert(frame.end(),prefix.begin()+3000,prefix.begin()+5048);
  auto appendSymbol=[&]{for(int k=0;k<32768;++k)unshifted[k]=freq[(k+16384)%32768];fftwf_execute(plan);for(auto &c:time)c*=.1f/std::sqrt(float(params.k_total));frame.insert(frame.end(),time.end()-2048,time.end());frame.insert(frame.end(),time.begin(),time.end());};
  int d=0;for(int k=0;k<p.k_total;++k){complex value{};if(p.p2_carrier_map[k]==DATA_CARRIER)value=p2cells[p.h_odd_p2[d++]];else if(p.p2_carrier_map[k]==P2CARRIER||p.p2_carrier_map[k]==P2CARRIER_INVERTED)value={p.amp_p2*p.p2_pilot_refer[0][k],0};freq[p.left_nulls+k]=value;}appendSymbol();
  std::mt19937 random(755);for(int symbol=1;symbol<=63;++symbol){
   std::fill(freq.begin(),freq.end(),complex{});
   if(symbol<63){std::vector<complex> cells(params.c_data);for(auto &c:cells)c=payloadOffset<payloadCells.size()?payloadCells[payloadOffset++]:fec[random()%fec.size()];
    int *h=symbol%2?address.h_even_data:address.h_odd_data;int d=0;
    for(int k=0;k<params.k_total;++k){complex value{};if(pilot.data_carrier_map[symbol-1][k]==DATA_CARRIER)value=cells[h[d++]];else value={pilot.data_pilot_refer[symbol-1][k],0};freq[params.l_nulls+k]=value;}assert(d==params.c_data);
   }else{for(int k=0;k<params.k_total;++k)freq[params.l_nulls+k]=pilot.fc_carrier_map[k]==DATA_CARRIER?fec[random()%fec.size()]:complex(pilot.fc_pilot_refer[k],0);}
   appendSymbol();
  }fftwf_destroy_plan(plan);assert(payloadOffset==payloadCells.size());
  HackRfSettings settings;settings.sampleRateHz=10000000;RxHackRfPro rx(settings);rx.m_running.store(true);rx.m_metricsTimer.start();rx.m_settleSamples=0;auto &demod=*rx.m_demodulator;auto *ti=demod.deinterleaver;auto *qam=ti->qam;auto *ldpc=qam->decoder;auto *bch=ldpc->decoder;auto *bb=bch->deheader;
  int mismatch=0,words=0;QObject::connect(ldpc,&ldpc_decoder::check,ldpc,[&](int n,uint8_t *bits){for(int i=0;i<n;){int errors=0;const auto &reference=expected[words%108];for(size_t j=0;j<reference.size();++j,++i)errors+=bits[i]!=reference[j];if(errors)qInfo()<<"RF word errors"<<words<<errors;mismatch+=errors;++words;}},Qt::DirectConnection);
  QTemporaryDir dir;auto path=dir.filePath("known.ts");QMetaObject::invokeMethod(bb,[&]{bb->set_network_output(false,7654);bb->set_recording(true,path);},Qt::BlockingQueuedConnection);
  std::normal_distribution<float> noise(0,float(.1*std::sqrt(.5/std::pow(10.,snr/10))));signal_estimate signal;
  std::vector<complex> input(3000);for(int i=0;i<frameCount;++i)input.insert(input.end(),frame.begin(),frame.end());input.resize(input.size()+50000);
  for(auto &c:input)c+=complex(noise(random),noise(random));
  if(qEnvironmentVariableIsSet("DIRECT_RF")){
   for(int pos=0;pos<int(input.size());pos+=4096)demod.symbol_acquisition(std::min(4096,int(input.size())-pos),input.data()+pos,&signal);
  }else{
   // Independent 35-phase, 65-tap interpolation: 64/7 MS/s -> 10 MS/s.
   float bank[35][65];for(int p=0;p<35;++p)for(int k=-32;k<=32;++k){double x=p/35.-k;bank[p][k+32]=std::abs(x)>=32?0:float((std::abs(x)<1e-12?1:std::sin(M_PI*x)/(M_PI*x))*(.5+.5*std::cos(M_PI*x/32)));}
   const qint64 samples=qint64(input.size())*35/32;
   QByteArray raw(int(samples*2),0);
   for(qint64 i=0;i<samples;++i){
    const qint64 position=i*32;const int center=position/35,phase=position%35;
    complex c{};
    for(int k=-32;k<=32;++k)
     if(center+k>=0 && center+k<int(input.size()))c+=input[center+k]*bank[phase][k+32];
    raw[int(2*i)]=char(qBound(-127,int(std::lround(c.real()*128)),127));
    raw[int(2*i+1)]=char(qBound(-127,int(std::lround(c.imag()*128)),127));
   }
   rx.m_metricsTimer.restart();
   const auto start=std::chrono::steady_clock::now();
   for(qint64 offset=0;offset<samples;offset+=131072){
    const int count=int(std::min<qint64>(131072,samples-offset));
    if(realtime)std::this_thread::sleep_until(start+std::chrono::nanoseconds(offset*100));
    rx.processSamples(reinterpret_cast<const uint8_t*>(raw.constData()+offset*2),count*2);
    if(!realtime)QMetaObject::invokeMethod(&demod,[]{},Qt::BlockingQueuedConnection);
   }
   QMetaObject::invokeMethod(&demod,[]{},Qt::BlockingQueuedConnection);

  }
  QMetaObject::invokeMethod(ti,[]{},Qt::BlockingQueuedConnection);QMetaObject::invokeMethod(qam,[&]{qam->flushPending();},Qt::BlockingQueuedConnection);
  for(QObject *stage:{static_cast<QObject*>(ldpc),static_cast<QObject*>(bch),static_cast<QObject*>(bb)})QMetaObject::invokeMethod(stage,[]{},Qt::BlockingQueuedConnection);
  TransportMetrics m;QMetaObject::invokeMethod(bb,[&]{bb->set_recording(false,QString());m=bb->snapshotMetrics();},Qt::BlockingQueuedConnection);
  qInfo()<<"FULL RF"<<snr<<"P1/pre/post"<<demod.p1Matches<<demod.l1PreMatches<<demod.l1PostMatches<<"words"<<words<<"mismatch"<<mismatch<<"BCH"<<m.bchFrames<<"failed"<<m.bchFailedFrames<<"TS"<<QFileInfo(path).size()<<"CP quality dB"<<demod.guardRepeatabilityDb<<"drops"<<rx.m_queueDrops.load()<<"realtime"<<realtime;
  assert(rx.m_queueDrops.load()==0);
  rx.stop();
  if(snr>=20){assert(words==(frameCount-1)*108);assert(mismatch==0);assert(m.bchFailedFrames==0);QFile output(path);assert(output.open(QIODevice::ReadOnly));QByteArray all=output.readAll();assert(all==expectedTs.repeated(words/108));}
 }

int main(int argc,char **argv){QCoreApplication app(argc,argv);P2AcquisitionTest::fullFrame(argc>1?QString(argv[1]).toDouble():20.);}
