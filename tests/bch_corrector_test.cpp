#include "DVB_T2/bch_corrector.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include "fec_reference.h"
int main(){
 BchCorrector decoder;
 const int cases[][4]={{32400,32208,12,0},{38880,38688,12,0},{43200,43040,10,0},{48600,48408,12,0},{51840,51648,12,0},{54000,53840,10,0},{7200,7032,12,1},{9720,9552,12,1},{10800,10632,12,1},{11880,11712,12,1},{12600,12432,12,1},{13320,13152,12,1}};
 for(auto &c:cases)for(int errors=0;errors<=c[2];++errors){
  const auto expected=Reference::bch(c[0],c[1],c[3],c[0]);
  auto bits=expected;
  for(int i=0;i<errors;++i)bits[i==0?c[0]-1:(i-1)*523]^=1;
  assert(decoder.correct(bits.data(),c[0],c[1],c[3])==errors);
  assert(bits==expected);
 }
 puts("BCH all 12 FEC/rate combinations, 0..capacity errors PASS");
}
