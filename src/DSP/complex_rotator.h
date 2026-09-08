// Phase-continuous oscillator without a trigonometric call per sample.
#ifndef COMPLEX_ROTATOR_H
#define COMPLEX_ROTATOR_H
#include <complex>
#include <cmath>
#include <algorithm>
inline void rotate_samples(const std::complex<float>* input,std::complex<float>* output,int count,
                           double &phase,double step,double phaseOffset=0)
{
 if(count<=0)return;
 if(std::abs(step)<1e-18 && std::abs(phase-phaseOffset)<1e-12){
  if(input!=output)std::copy(input,input+count,output);return;
 }
 for(int first=0;first<count;first+=256){
  const int length=std::min(256,count-first);
  double angle=phase+step-phaseOffset;
  float real=std::cos(angle),imag=std::sin(angle);
  const float dr=std::cos(step),di=std::sin(step);
  for(int j=0;j<length;++j){
   const auto sample=input[first+j];
   output[first+j]={sample.real()*real-sample.imag()*imag,sample.imag()*real+sample.real()*imag};
   const float next=real*dr-imag*di;imag=imag*dr+real*di;real=next;
  }
  phase=std::remainder(phase+length*step,2*M_PI);
 }
}
#endif
