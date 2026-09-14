#include <QCoreApplication>
#include <QDebug>
#include <vector>
#include <cassert>
#include <array>
#include <random>
#include "DVB_T2/p1_symbol.h"
class P1AcquisitionTest {
public:
 static std::vector<complex> waveform(p1_symbol &p,float amplitude,int s2=10,const std::vector<int> &damagedBits={}){
  std::vector<complex> freq(1024),time(1024),shifted(1024),output(20000);
  uint8_t pattern[48];std::copy(p.s1_patterns[0],p.s1_patterns[0]+8,pattern);
  std::copy(p.s2_patterns[s2],p.s2_patterns[s2]+32,pattern+8);
  std::copy(p.s1_patterns[0],p.s1_patterns[0]+8,pattern+40);
  for(int bit:damagedBits)pattern[bit/8]^=uint8_t(1u<<(7-bit%8));
  int state=1;
  for(int i=0;i<384;++i){if((pattern[i/8]>>(7-i%8))&1)state=-state;freq[(p.p1_active_carriers[i]+86+512)%1024]=float(state*p.p1_randomize[i]);}
  auto plan=fftwf_plan_dft_1d(1024,reinterpret_cast<fftwf_complex*>(freq.data()),reinterpret_cast<fftwf_complex*>(time.data()),FFTW_BACKWARD,FFTW_ESTIMATE);
  fftwf_execute(plan);fftwf_destroy_plan(plan);
  for(int i=0;i<1024;++i){time[i]*=amplitude/std::sqrt(384.0f);shifted[i]=time[i]*std::polar(1.0f,float(2*M_PI*i/1024));}
  std::copy(shifted.begin(),shifted.begin()+542,output.begin()+3000);
  std::copy(time.begin(),time.end(),output.begin()+3542);
  std::copy(shifted.begin()+542,shifted.end(),output.begin()+4566);return output;
 }
 static void signalling(){
  p1_symbol p;std::array<uint8_t,48> bytes{};uint8_t actualS1=0,actualS2=0;
  const auto fill=[&](int s1,int s2){
   std::copy_n(p.s1_patterns[s1],8,bytes.begin());
   std::copy_n(p.s2_patterns[s2],32,bytes.begin()+8);
   std::copy_n(p.s1_patterns[s1],8,bytes.begin()+40);
  };
  const auto flip=[&](int bit){bytes[bit/8]^=uint8_t(1u<<(7-bit%8));};
  for(int s1=0;s1<8;++s1)for(int s2=0;s2<16;++s2){
   for(int errors:{0,1,8,16}){
    fill(s1,s2);
    for(int i=0;i<errors;++i)flip(i%2?320+i/2:i/2);
    for(int i=0;i<errors*2;++i)flip(64+(i*7)%256);
    assert(p.decode_signalling(bytes.data(),actualS1,actualS2));
    assert(actualS1==s1 && actualS2==s2);
   }
   fill(s1,s2);for(int i=0;i<17;++i)flip(i);
   assert(!p.decode_signalling(bytes.data(),actualS1,actualS2));
   fill(s1,s2);for(int i=0;i<33;++i)flip(64+i);
   assert(!p.decode_signalling(bytes.data(),actualS1,actualS2));
  }
  // Matching prefixes alone do not identify a P1 sequence.
  fill(0,10);for(int i=80;i<320;++i)flip(i);
  assert(!p.decode_signalling(bytes.data(),actualS1,actualS2));
  std::mt19937 random(20260914);
  for(int trial=0;trial<2000;++trial){for(auto &b:bytes)b=uint8_t(random());assert(!p.decode_signalling(bytes.data(),actualS1,actualS2));}
  auto input=waveform(p,.1f,10,{0,12,326,71,188,294});
  std::vector<complex> buffer(50000);dvbt2_parameters params{};int index=0;double offset=0;bool reset=false,found=false;
  for(int pos=0;pos<int(input.size());pos+=257){int count=std::min(257,int(input.size())-pos),consumed=0;
   while(consumed<count){bool decoded=false;p.execute(.02f,count,input.data()+pos,consumed,false,buffer.data(),index,params,offset,decoded,reset);found|=decoded;}
  }
  assert(found && params.fft_mode==FFTSIZE_32K);
  qInfo()<<"P1 full S1/S2 codewords / bounded correction / noise rejection / damaged RF PASS";
 }
 static void run(){
  signalling();
  for(float amp:{.5f,.1f,.02f,.005f}){
   p1_symbol p;auto input=waveform(p,amp);std::vector<complex> buffer(50000);dvbt2_parameters params{};
   int index=0;double offset=0;bool decoded=false,reset=false,found=false;
   for(int pos=0;pos<int(input.size());pos+=257){int count=std::min(257,int(input.size())-pos),consumed=0;
    while(consumed<count){p.execute(.02f,count,input.data()+pos,consumed,false,buffer.data(),index,params,offset,decoded,reset);found|=decoded;}
   }
   assert(found && params.fft_mode==FFTSIZE_32K);qInfo()<<"P1 amplitude"<<amp<<"PASS";
  }
 }
};
#ifndef P1_NO_MAIN
int main(int argc,char **argv){QCoreApplication app(argc,argv);P1AcquisitionTest::run();}
#endif
