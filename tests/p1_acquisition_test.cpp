#include <QCoreApplication>
#include <QDebug>
#include <vector>
#include <cassert>
#include "DVB_T2/p1_symbol.h"
class P1AcquisitionTest {
public:
 static std::vector<complex> waveform(p1_symbol &p,float amplitude,int s2=10){
  std::vector<complex> freq(1024),time(1024),shifted(1024),output(20000);
  uint8_t pattern[48];std::copy(p.s1_patterns[0],p.s1_patterns[0]+8,pattern);
  std::copy(p.s2_patterns[s2],p.s2_patterns[s2]+32,pattern+8);
  std::copy(p.s1_patterns[0],p.s1_patterns[0]+8,pattern+40);
  int state=1;
  for(int i=0;i<384;++i){if((pattern[i/8]>>(7-i%8))&1)state=-state;freq[(p.p1_active_carriers[i]+86+512)%1024]=float(state*p.p1_randomize[i]);}
  auto plan=fftwf_plan_dft_1d(1024,reinterpret_cast<fftwf_complex*>(freq.data()),reinterpret_cast<fftwf_complex*>(time.data()),FFTW_BACKWARD,FFTW_ESTIMATE);
  fftwf_execute(plan);fftwf_destroy_plan(plan);
  for(int i=0;i<1024;++i){time[i]*=amplitude/std::sqrt(384.0f);shifted[i]=time[i]*std::polar(1.0f,float(2*M_PI*i/1024));}
  std::copy(shifted.begin(),shifted.begin()+542,output.begin()+3000);
  std::copy(time.begin(),time.end(),output.begin()+3542);
  std::copy(shifted.begin()+542,shifted.end(),output.begin()+4566);return output;
 }
 static void run(){
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
