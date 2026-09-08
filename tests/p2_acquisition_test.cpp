// Regression vectors: ETSI short BCH(3240,3072), LDPC 1/4, L1-pre puncturing;
// CP/FFT/P2 tests include a channel phase crossing -pi/pi and real CS8 resampling.
#define P1_NO_MAIN
#include "p1_acquisition_test.cpp"
#include "rx_hackrf_pro.h"
#include "DSP/complex_rotator.h"
#include "DSP/guard_acquisition.h"
#include <set>
#include <random>
#include <QMetaObject>
class P2AcquisitionTest {
 static void put(std::vector<uint8_t>& bits,uint32_t value,int n){for(int i=n-1;i>=0;--i)bits.push_back((value>>i)&1);}
 static std::vector<uint8_t> payload(int gi,int s2=5){
  std::vector<uint8_t>b;
  const int fields[][2]={{0,8},{0,1},{0,3},{s2,3},{0,1},{0,1},{gi,3},{0,4},{0,4},{0,2},{0,2},{1500,18},{318,18},{6,4},{0,8},{1,16},{0x3085,16},{0x8001,16},{2,8},{100,12},{0,3},{0,1},{1,3},{0,3},{1,4},{0,1},{0,1},{0,4}};
  for(auto &f:fields)put(b,f[0],f[1]);assert(b.size()==168);
  uint32_t crc=0xffffffff;for(int bit:b){bool x=((crc>>31)&1)^bit;crc<<=1;if(x)crc^=0x04c11db7;}put(b,crc,32);return b;
 }
 static int multiply(int a,int b){int c=0;while(b){if(b&1)c^=a;b>>=1;a<<=1;if(a&16384)a^=0x402b;}return c;}
 static std::vector<uint8_t> bchEncode(const std::vector<uint8_t>& data){
  std::set<int> roots;for(int i=1;i<=24;++i){int e=i;do{roots.insert(e);e=(2*e)%16383;}while(e!=i);}
  std::vector<int> poly(1,1),power(16383,1);for(int i=1;i<16383;++i)power[i]=multiply(power[i-1],2);
  for(int e:roots){std::vector<int> next(poly.size()+1);for(size_t i=0;i<poly.size();++i){next[i]^=multiply(poly[i],power[e]);next[i+1]^=poly[i];}poly=next;}
  assert(poly.size()==169);for(int p:poly)assert(p==0||p==1);
  std::vector<uint8_t> out=data;const int k=data.size();out.resize(k+168);auto remainder=out;
  for(int i=0;i<k;++i)if(remainder[i])for(int j=0;j<=168;++j)remainder[i+j]^=poly[168-j];
  std::copy(remainder.begin()+k,remainder.end(),out.begin()+k);return out;
 }
 static std::vector<complex> encoded(int gi,int s2=5){
  auto original=payload(gi,s2);original.resize(3072);auto code=bchEncode(original);
  LDPC<DVB_T2_TABLE_SHORT_C1_4> ldpc;ldpc.first_bit();std::vector<uint8_t> parity(12960);
  for(int i=0;i<3240;++i){if(code[i])for(int j=0;j<ldpc.bit_deg();++j)parity[ldpc.acc_pos()[j]]^=1;if(i<3239)ldpc.next_bit();}
  for(int i=1;i<12960;++i)parity[i]^=parity[i-1];
  // GNU Radio dvbt2_framemapper_cc_impl::add_l1pre, Table 17.
  const int order[]={27,13,29,32,5,0,11,21,33,20,25,28,18,35,8,3,9,31,22,24,7,14,17,4,2,26,16,34,19,10,12,23,1,6,30,15};
  for(int i=0;i<32;++i)for(int j=0;j<(i==31?328:360);++j)parity[order[i]+36*j]=2;
  std::vector<complex> cells;for(int i=0;i<200;++i)cells.emplace_back(1-2*code[i],0);
  for(int i=3072;i<3240;++i)cells.emplace_back(1-2*code[i],0);
  for(auto bit:parity)if(bit!=2)cells.emplace_back(1-2*bit,0);assert(cells.size()==1840);return cells;
 }
 static std::vector<uint8_t> postPayload(){
  std::vector<uint8_t> b;
  const uint32_t fields[][2]={{1,15},{1,8},{0,4},{0,8},{0,3},{586000000,32},{17,8},{1,3},{3,5},{0,1},{0,3},{0,8},{1,8},{0,3},{0,3},{0,1},{0,2},{1,10},{1,8},{1,8},{0,1},{0,1},{0,1},{0,11},{0,2},{0,1},{0,1},{0,2},{0,30},{0,8},{0,22},{0,22},{0,8},{0,3},{0,8},{17,8},{0,22},{1,10},{0,8},{0,8}};
  for(auto &f:fields)put(b,f[0],f[1]);assert(b.size()==318);
  uint32_t crc=0xffffffff;for(auto bit:b){bool x=((crc>>31)&1)^bit;crc<<=1;if(x)crc^=0x04c11db7;}put(b,crc,32);return b;
 }
 static std::vector<uint8_t> postEncoded(int mod,bool scrambled){
  const int padding[3][20]={{18,17,16,15,14,13,12,11,4,10,9,8,3,2,7,6,5,1,19,0},{18,17,16,15,14,13,12,11,4,10,9,8,7,3,2,1,6,5,19,0},{18,17,16,4,15,14,13,12,3,11,10,9,2,8,7,1,6,5,19,0}};
  const int puncture[3][25]={{6,4,18,9,13,8,15,20,5,17,2,24,10,22,12,3,16,23,1,14,0,21,19,7,11},{6,4,13,9,18,8,15,20,5,17,2,22,24,7,12,1,16,23,14,0,21,10,19,11,3},{6,15,13,10,3,17,21,8,5,19,2,23,16,24,7,18,1,12,20,0,4,14,9,11,22}};
  const int eta[]={1,2,4,6};int kind=std::max(0,mod-1),np=(1500+2*eta[mod]-1)/(2*eta[mod])*(2*eta[mod]);
  auto info=postPayload();if(scrambled){int sr=0x4a80;for(auto &bit:info){int b=(sr^(sr>>1))&1;bit^=b;sr>>=1;if(b)sr|=0x4000;}}
  std::vector<bool> paddingMap(7032);for(int group=0;group<19;++group){int pos=padding[kind][group]*360;for(int j=0;j<(padding[kind][group]==19?192:360);++j)paddingMap[pos+j]=true;}
  for(int i=350;i<360;++i)paddingMap[i]=true;
  std::vector<uint8_t> data(7032);int index=0;for(int i=0;i<7032;++i)if(!paddingMap[i])data[i]=info[index++];assert(index==350);auto code=bchEncode(data);
  std::vector<uint8_t> parity(9000);LDPC<DVB_T2_TABLE_SHORT_C1_2> ldpc;ldpc.first_bit();
  for(int i=0;i<7200;++i){if(code[i])for(int j=0;j<ldpc.bit_deg();++j)parity[ldpc.acc_pos()[j]]^=1;if(i<7199)ldpc.next_bit();}for(int i=1;i<9000;++i)parity[i]^=parity[i-1];
  int numPuncture=9518-np;for(int i=0;i<numPuncture;++i)parity[(i%360)*25+puncture[kind][i/360]]=2;
  std::vector<uint8_t> out=info;out.insert(out.end(),code.begin()+7032,code.end());for(auto bit:parity)if(bit!=2)out.push_back(bit);assert(int(out.size())==np);return out;
 }
 static std::vector<complex> postCells(int mod,bool scrambled){
  auto encoded=postEncoded(mod,scrambled),interleaved=encoded;const int cols=mod<2?0:mod==2?8:12;
  if(cols)for(int row=0;row<int(encoded.size())/cols;++row)for(int col=0;col<cols;++col)interleaved[row*cols+col]=encoded[col*(encoded.size()/cols)+row];
  const int mux16[]={7,1,3,5,2,4,6,0},mux64[]={11,8,5,2,10,7,4,1,9,6,3,0};
  auto mapped=interleaved;if(cols)for(int i=0;i<int(encoded.size());i+=cols)for(int j=0;j<cols;++j)mapped[i+j]=interleaved[i+(mod==2?mux16[j]:mux64[j])];
  const int eta[]={1,2,4,6};std::vector<complex> cells;
  for(int i=0;i<int(mapped.size());i+=eta[mod]){
   if(mod==0)cells.emplace_back(1-2*mapped[i],0);
   else if(mod==1)cells.emplace_back((1-2*mapped[i])*float(M_SQRT1_2),(1-2*mapped[i+1])*float(M_SQRT1_2));
   else {float norm=mod==2?float(NORM_FACTOR_QAM16):float(NORM_FACTOR_QAM64);float a[2];
    for(int j=0;j<2;++j){int amp=mod==2?(mapped[i+2+j]?1:3):(mapped[i+2+j]?(mapped[i+4+j]?3:1):(mapped[i+4+j]?5:7));a[j]=(1-2*mapped[i+j])*amp*norm;}
    cells.emplace_back(a[0],a[1]);
   }
  }return cells;
 }
 static void postFec(){
  auto expected=postPayload();
  for(int mod=0;mod<4;++mod)for(bool scrambled:{false,true}){
   auto code=postEncoded(mod,scrambled);L1PostDecoder decoder;uint8_t out[7032];
   for(int n:{0,1,12,20}){auto damaged=code;std::vector<float> llr(code.size());for(int i=0;i<int(code.size());++i)llr[i]=code[i]?-8.f:8.f;for(int i=0;i<n;++i){int pos=(i*17)%350;damaged[pos]^=1;llr[pos]*=-.08f;}assert(decoder.decode(damaged.data(),damaged.size(),318,mod,scrambled,out,llr.data()));assert(std::equal(out,out+350,expected.begin()));}
   qInfo()<<"L1 POST FEC modulation"<<mod<<"scrambled"<<scrambled<<"PASS";
  }
  dvbt2_parameters params{};params.fft_mode=FFTSIZE_32K;params.preamble=T2_SISO;dvbt2_p2_parameters_init(params);
  pilot_generator pilot;address_freq_deinterleaver address;address.init(params);p2_symbol p;p.init(params,&pilot,&address);
  for(int mod=0;mod<4;++mod)for(bool scrambled:{false,true}){
   auto cells=postCells(mod,scrambled);std::copy(cells.begin(),cells.end(),p.deinterleaved_cell+1840);p.l1_pre.l1_post_mod=mod;p.l1_pre.l1_post_size=cells.size();p.l1_pre.l1_post_info_size=318;p.l1_pre.t2_version=2;p.l1_pre.l1_post_scrambled=scrambled;
   p.l1_pre.num_rf=1;p.l1_post.rf=new l1_postsignalling_rf[7]{};
   assert(p.l1_post_info());assert(p.l1_post.num_plp==1&&p.l1_post.plp[0].id==17&&p.l1_post.dyn.plp[0].id==17);delete[] p.l1_post.rf;p.l1_post.rf=nullptr;
  }qInfo()<<"L1 POST demapping / deinterleaving / descrambling / PLP parsing PASS";
 }
 static std::vector<complex> p2freq(p2_symbol& p,int gi,int offset=0){
  auto cells=encoded(gi,p.fft_size==16384?4:5);cells.resize(p.c_p2);std::mt19937 random(3456);
  auto post=postCells(0,false);std::copy(post.begin(),post.end(),cells.begin()+1840);
  for(int i=1840+post.size();i<p.c_p2;++i)cells[i]=complex((random()&1)?1:-1,(random()&1)?1:-1)*float(M_SQRT1_2);
  std::vector<complex> freq(p.fft_size);int d=0;
  for(int k=0;k<p.k_total;++k){complex value;
   if(p.p2_carrier_map[k]==DATA_CARRIER)value=cells[p.h_odd_p2[d++]];
   else if(p.p2_carrier_map[k]==P2CARRIER || p.p2_carrier_map[k]==P2CARRIER_INVERTED)value=complex(p.amp_p2*p.p2_pilot_refer[0][k],0);
   value*=std::polar(1.f,float(.71+2*M_PI*offset*k/p.fft_size));freq[p.left_nulls+k]=value;
  }assert(d==p.c_p2);return freq;
 }
 static int guard(int gi){const int g[]={1024,2048,4096,8192,256,4864,2432};return g[gi];}
 static std::vector<complex> waveform(int gi,double cfo,int fftMode=FFTSIZE_32K){
  dvbt2_parameters params{};params.fft_mode=fftMode;params.preamble=T2_SISO;dvbt2_p2_parameters_init(params);
  pilot_generator pilot;address_freq_deinterleaver address;address.init(params);p2_symbol p;p.init(params,&pilot,&address);
  auto freq=p2freq(p,gi),unshifted=freq,time=freq;
  for(int i=0;i<p.fft_size;++i)unshifted[i]=freq[(i+p.fft_size/2)%p.fft_size];
  auto plan=fftwf_plan_dft_1d(p.fft_size,reinterpret_cast<fftwf_complex*>(unshifted.data()),reinterpret_cast<fftwf_complex*>(time.data()),FFTW_BACKWARD,FFTW_ESTIMATE);
  fftwf_execute(plan);fftwf_destroy_plan(plan);for(auto &c:time)c*=.1f/std::sqrt(float(p.k_total));
  p1_symbol p1;auto out=P1AcquisitionTest::waveform(p1,.1f,p.fft_size==16384?8:10);out.resize(5048+p.fft_size+p.fft_size/4+2000);
  std::copy(time.end()-guard(gi)*p.fft_size/32768,time.end(),out.begin()+5048);
  std::copy(time.begin(),time.end(),out.begin()+5048+guard(gi)*p.fft_size/32768);
  for(int i=0;i<int(out.size());++i)out[i]*=std::polar(1.f,float(2*M_PI*cfo*i/SAMPLE_RATE));return out;
 }
 static void fec(){
  auto expected=payload(GI_1_128);auto cells=encoded(GI_1_128);L1PreDecoder decoder;uint8_t out[200];
  for(int n:{0,1,6,12,20}){auto noisy=cells;for(int i=0;i<n;++i)noisy[(i*17)%200]*=-.08f;assert(decoder.decode(noisy.data(),out));assert(std::equal(out,out+200,expected.begin()));qInfo()<<"L1 pre corrected errors"<<n<<"PASS";}
  assert(decoder.ldpcRecovered>0);std::mt19937 random(23);for(auto& c:cells)c=complex((random()&1)?1:-1,0);assert(!decoder.decode(cells.data(),out));
 }
 static void equalizer(){
  dvbt2_parameters params{};params.fft_mode=FFTSIZE_32K;params.preamble=T2_SISO;dvbt2_p2_parameters_init(params);
  pilot_generator pilot;address_freq_deinterleaver address;address.init(params);p2_symbol p;p.init(params,&pilot,&address);
  for(int offset:{0,11,-11,230,-230}){auto freq=p2freq(p,GI_1_128,offset);int index=0;l1_presignalling pre;l1_postsignalling post;bool preok=false,postok=false,sync=false;float sample=0,phase=0;
   p.execute(params,false,index,freq.data(),pre,post,preok,postok,sample,phase,sync);assert(preok&&postok);assert(post.num_plp==1&&post.plp[0].id==17);assert(pre.network_id==0x3085&&pre.guard_interval==GI_1_128);qInfo()<<"P2 equalization timing offset"<<offset<<"PASS";
  }
 }
 static void acquisition(){
  for(int gi=0;gi<7;++gi){auto input=waveform(gi,40);auto cp=acquire_guard(input.data()+5048,32768);assert(cp.samples==guard(gi));assert(cp.confidence>.99f);
   dvbt2_demodulator demod(.02f,10000000);signal_estimate signal;
   for(int pos=0;pos<int(input.size());pos+=257)demod.symbol_acquisition(std::min(257,int(input.size())-pos),input.data()+pos,&signal);
   qInfo()<<"P1->P2 GI"<<gi<<"P1"<<demod.p1Matches<<"P2"<<demod.p2Attempts<<"PRE"<<demod.l1PreMatches<<"CP"<<demod.cpConfidence<<"measured"<<demod.measuredGuard<<"CFO"<<signal.coarse_freq_offset;
   assert(demod.l1PreMatches==1&&demod.l1PostMatches==1);assert(demod.measuredGuard==guard(gi));
  }
 }
 static void frontend(){
  for(int fftMode:{FFTSIZE_32K,FFTSIZE_16K})for(double rate:{8000000.,10000000.,12500000.,16000000.,20000000.}){
   auto input=waveform(GI_1_128,-40,fftMode);int count=int(input.size()*rate/SAMPLE_RATE);QByteArray bytes(count*2,0);
   for(int i=0;i<count;++i){double t=i*SAMPLE_RATE/rate;int center=int(t);complex c;
    for(int k=center-32;k<=center+32;++k){double x=t-k;if(k<0||k>=int(input.size())||std::abs(x)>=32)continue;double sinc=std::abs(x)<1e-10?1:std::sin(M_PI*x)/(M_PI*x);c+=input[k]*float(sinc*(.5+.5*std::cos(M_PI*x/32)));}
    bytes[2*i]=char(qBound(-127,int(std::lround(c.real()*128)),127));bytes[2*i+1]=char(qBound(-127,int(std::lround(c.imag()*128)),127));
   }
   HackRfSettings settings;settings.sampleRateHz=rate;RxHackRfPro rx(settings);rx.m_running.store(true);rx.m_metricsTimer.start();rx.m_settleSamples=0;
   for(int pos=0;pos<bytes.size();pos+=8192){rx.processSamples(reinterpret_cast<uint8_t*>(bytes.data()+pos),qMin(8192,bytes.size()-pos));}
   QMetaObject::invokeMethod(rx.m_demodulator,[]{},Qt::BlockingQueuedConnection);
   qInfo()<<"CS8->P1->P2 rate"<<rate<<"P1"<<rx.m_demodulator->p1Matches<<"PRE"<<rx.m_demodulator->l1PreMatches<<"CP"<<rx.m_demodulator->cpConfidence;
   assert(rx.m_demodulator->l1PreMatches==1&&rx.m_demodulator->l1PostMatches==1);rx.stop();
  }
 }
 static void frequencyCorrection(){
  for(double cfo:{-2200.,2200.,-18000.,18000.}){
   p1_symbol p;auto input=P1AcquisitionTest::waveform(p,.1f);for(int i=0;i<int(input.size());++i)input[i]*=std::polar(1.f,float(2*M_PI*cfo*i/SAMPLE_RATE));
   std::vector<complex> buffer(50000);dvbt2_parameters params;int index=0,consumed=0;double offset=0;bool found=false,reset=false;
   while(consumed<int(input.size())&&!found)p.execute(.02f,input.size(),input.data(),consumed,false,buffer.data(),index,params,offset,found,reset);
   assert(found);qInfo()<<"P1 CFO expected"<<cfo<<"measured"<<offset;assert(std::abs(cfo-offset)<2.0);
  }
 }
 static void oscillator(){
  std::vector<complex> input(100001,complex(.31f,-.7f)),out(input.size());double phase=.17,step=.01713;
  for(int pos=0;pos<int(input.size());pos+=777)rotate_samples(input.data()+pos,out.data()+pos,std::min(777,int(input.size())-pos),phase,step,.03);
  for(int i=0;i<int(out.size());++i)assert(std::abs(out[i]-input[i]*std::polar(1.f,float(std::remainder(.14+(i+1)*step,2*M_PI))))<1e-5f);
 }
public:static void run(){oscillator();frequencyCorrection();fec();postFec();equalizer();acquisition();frontend();}
};
int main(int argc,char **argv){QCoreApplication app(argc,argv);P2AcquisitionTest::run();qInfo()<<"p2_acquisition_test PASS";}
