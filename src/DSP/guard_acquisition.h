// DVB-T2 cyclic-prefix measurement, independent of the L1 guard field.
#ifndef GUARD_ACQUISITION_H
#define GUARD_ACQUISITION_H
#include <complex>
#include <cmath>
#include <algorithm>
struct GuardEstimate {int samples=0;float confidence=0;double radiansPerSample=0;};
inline GuardEstimate acquire_guard(const std::complex<float>* samples,int fftSize)
{
 const int lengths[]={fftSize/128,fftSize/32,fftSize/16,fftSize*19/256,fftSize/8,fftSize*19/128,fftSize/4};
 GuardEstimate best,estimates[7];double highestConfidence=0,energyA=0,energyB=0;std::complex<double> sum{};
 int candidate=0;
 for(int i=0;i<fftSize/4;++i){
  const std::complex<double> a=samples[i],b=samples[i+fftSize];sum+=b*std::conj(a);energyA+=std::norm(a);energyB+=std::norm(b);
  if(i+1==lengths[candidate]){
   double confidence=std::norm(sum)/std::max(1e-24,energyA*energyB);
   estimates[candidate]={i+1,float(confidence),std::arg(sum)/fftSize};
   highestConfidence=std::max(highestConfidence,confidence);
   ++candidate;
  }
 }
 for(const auto& estimate:estimates)if(estimate.confidence>=highestConfidence*.9)best=estimate;
 return best;
}
#endif
