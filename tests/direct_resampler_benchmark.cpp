#include "DSP/bandlimited_resampler.h"
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

int main(){
    constexpr double inRate=8.0e6;
    constexpr double outRate=64.0e6/7.0;
    constexpr double tone=1.0e6;
    constexpr int block=131072;
    constexpr int blocks=32; // 4.19M input samples
    bandlimited_resampler r(inRate,outRate);
    std::vector<std::complex<float>> in(block);
    std::vector<std::complex<float>> out(160000);
    double phase=0.0;
    const double d=2.0*M_PI*tone/inRate;
    std::uint64_t produced=0;
    double amp=0.0;
    std::complex<double> phaseStep(0,0);
    std::complex<float> prev{}; bool havePrev=false;
    auto t0=std::chrono::steady_clock::now();
    for(int b=0;b<blocks;++b){
        for(int i=0;i<block;++i){in[i]={float(std::cos(phase)),float(std::sin(phase))};phase+=d;if(phase>2*M_PI)phase-=2*M_PI;}
        int n=r.execute(block,in.data(),inRate/outRate,out.data(),int(out.size()));
        produced+=n;
        for(int i=0;i<n;++i){
            amp+=std::abs(out[i]);
            if(havePrev)phaseStep += std::complex<double>(std::conj(prev)*out[i]);
            prev=out[i];havePrev=true;
        }
    }
    auto t1=std::chrono::steady_clock::now();
    double sec=std::chrono::duration<double>(t1-t0).count();
    double input=double(block)*blocks;
    double expected=input*outRate/inRate;
    double meanAmp=produced?amp/produced:0;
    double measuredHz=std::arg(phaseStep)*outRate/(2*M_PI);
    std::printf("input %.3f M, output %.3f M, expected %.3f M\n",input/1e6,produced/1e6,expected/1e6);
    std::printf("mean amplitude %.6f, measured tone %.1f Hz\n",meanAmp,measuredHz);
    std::printf("elapsed %.3f s, %.2f MS/s input, %.2f MS/s output\n",sec,input/sec/1e6,produced/sec/1e6);
    if(std::abs(double(produced)-expected)>128.0)return 2;
    if(std::abs(meanAmp-1.0)>0.02)return 3;
    if(std::abs(measuredHz-tone)>200.0)return 4;
    return 0;
}
