#include "DVB_T2/bch_corrector.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <array>
#include <random>
#include "fec_reference.h"
template<typename Field> void locatorChecks(){
 using V=typename Field::ValueType;using I=typename Field::IndexType;
 using Finder=CODE::RS::LocationFinder<24,Field>;
 std::array<V,25> polynomial;std::array<I,24> locations;
 auto check=[&](int degree){
  const bool split=Finder::splits_completely(polynomial.data(),degree);
  const int roots=CODE::RS::Chien<24,Field>::search(polynomial.data(),degree,locations.data());
  assert(split==(roots==degree));return split;
 };
 for(int degree:{3,4,8,12}){
  polynomial.fill(V(0));polynomial[0]=V(1);
  for(int i=0;i<degree;++i){
   const V root=value(I(17+i*31));
   for(int j=i+1;j>0;--j)polynomial[j]+=polynomial[j-1]*root;
  }
  assert(check(degree));
  // Normalization must also work for a non-monic locator.
  for(int j=0;j<=degree;++j)polynomial[j]*=V(37);
  assert(check(degree));
 }
 polynomial.fill(V(0));polynomial[0]=V(1);
 for(int i=0;i<3;++i){const V root=value(I(i<2?7:19));for(int j=i+1;j>0;--j)polynomial[j]+=polynomial[j-1]*root;}
 assert(!check(3)); // Repeated roots do not represent three error locations.
 std::mt19937 random(20260914);
 for(int trial=0;trial<12;++trial){
  int degree=3+trial%10;
  for(int i=0;i<=degree;++i)polynomial[i]=V(1+random()%Field::N);
  check(degree);
 }
}
int main(){
 BchCorrector decoder;
 locatorChecks<CODE::GaloisField<16,0x1002d,uint16_t>>();
 locatorChecks<CODE::GaloisField<14,0x402b,uint16_t>>();
 puts("BCH locator precheck agrees with exhaustive Chien search PASS");
 const int cases[][4]={{32400,32208,12,0},{38880,38688,12,0},{43200,43040,10,0},{48600,48408,12,0},{51840,51648,12,0},{54000,53840,10,0},{7200,7032,12,1},{9720,9552,12,1},{10800,10632,12,1},{11880,11712,12,1},{12600,12432,12,1},{13320,13152,12,1}};
 for(auto &c:cases)for(int errors=0;errors<=c[2];++errors){
  const auto expected=Reference::bch(c[0],c[1],c[3],c[0]);
  auto bits=expected;
  for(int i=0;i<errors;++i)bits[i==0?c[0]-1:(i-1)*523]^=1;
  assert(decoder.correct(bits.data(),c[0],c[1],c[3])==errors);
  assert(bits==expected);
 }
 for(auto &c:cases){
  auto expected=Reference::bch(c[0],c[1],c[3],c[0]);
  for(int errors:{c[2]+1,32,100}){
   auto bits=expected;for(int i=0;i<errors;++i)bits[(i*523)%c[0]]^=1;
   assert(decoder.correct(bits.data(),c[0],c[1],c[3])<0);
  }
 }
 puts("BCH all 12 FEC/rate combinations, 0..capacity errors PASS");
}
