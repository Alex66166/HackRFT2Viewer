#include <chrono>
#include <cstdio>
#include <vector>
#include <cmath>
#include "DSP/iq_correct.hh"
#include "DSP/complex_rotator.h"
#include "DSP/interpolator_farrow.hh"
#include "DSP/filter_decimator.h"
int main(){
 constexpr int n=131072,loops=80;
 std::vector<int8_t> input(n*2);unsigned rng=12345;
 for(auto &v:input){rng=rng*1664525u+1013904223u;v=int8_t(int((rng>>24)&31)-16);}
 std::vector<complex> a(n),b(n),up(n*3),down(n*2);
 iq_correct<int8_t> iq(7,.04f,.02f);interpolator_farrow_4pt_3rd<complex,float> farrow;filter_decimator decimator;
 int gain=0,len=0,outlen=0;double ratio=10000000./(2*(64000000./7));
 auto run=[&](const char *name,auto work){auto start=std::chrono::steady_clock::now();for(int i=0;i<loops;++i)work();double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();printf("%s: %.3f s / %.1f MS/s\n",name,seconds,double(n)*loops/seconds/1e6);};
 long double sum=0;int peak=0;run("metrics long double",[&]{for(int i=0;i<2*n;++i){int v=input[i];sum+=double(v)*v;peak=std::max(peak,std::abs(v));}});
 uint64_t integerSum=0;run("metrics integer",[&]{for(int i=0;i<2*n;++i){int v=input[i];integerSum+=uint64_t(v*v);peak=std::max(peak,std::abs(v));}});
 run("IQ correction",[&]{iq.execute(size_t(n),input.data(),a.data(),gain);});
 float phase=0,offset=.0001f;
 run("NCO sin/cos",[&]{for(int i=0;i<n;++i){phase-=offset;if(phase< -6.2831853f)phase+=6.2831853f;float c=std::cos(phase),s=std::sin(phase);b[i]={a[i].real()*c-a[i].imag()*s,a[i].imag()*c+a[i].real()*s};}});
 double oscillatorPhase=0;run("NCO recurrence",[&]{rotate_samples(a.data(),b.data(),n,oscillatorPhase,-double(offset));});
 run("Farrow upsample",[&]{farrow(n,b.data(),ratio,len,up.data());});
 run("FIR decimation",[&]{decimator.execute(len,up.data(),outlen,down.data());});
 printf("checksum %.0Lf %.6f %d\n",sum+integerSum,down[outlen-1].real(),peak);
}
