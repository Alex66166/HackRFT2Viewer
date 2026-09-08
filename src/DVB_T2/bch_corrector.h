// DVB-T2 outer BCH adapter, GPL-3.0-or-later.
#ifndef DVB_T2_BCH_CORRECTOR_H
#define DVB_T2_BCH_CORRECTOR_H
#include <cstdint>
#include <array>
#include "third_party/bch/galois_field.hh"
#include "third_party/bch/bose_chaudhuri_hocquenghem_decoder.hh"
class BchCorrector {
 using Normal=CODE::GaloisField<16,0x1002d,uint16_t>;
 using Short=CODE::GaloisField<14,0x402b,uint16_t>;
 static int initializeFields() { static Normal normal; static Short shortFrame; (void)normal; (void)shortFrame; return 1; }
 int initialized=initializeFields();
 CODE::BoseChaudhuriHocquenghemDecoder<24,1,65343,Normal> n12;
 CODE::BoseChaudhuriHocquenghemDecoder<20,1,65375,Normal> n10;
 CODE::BoseChaudhuriHocquenghemDecoder<24,1,16215,Short> s12;
 std::array<uint8_t,6750> packed{};
public:
 int correct(uint8_t* bits,int n,int k,bool shortFrame) {
  if(!bits || k<=0 || n>54000 || (k&7) || (n&7)) return -1;
  if((shortFrame && n-k!=168) || (!shortFrame && n-k!=160 && n-k!=192)) return -1;
  for(int i=0;i<n/8;++i) { uint8_t byte=0; for(int j=0;j<8;++j) byte=uint8_t((byte<<1)|(bits[8*i+j]&1)); packed[i]=byte; }
  auto *data=packed.data(), *parity=data+k/8;
  int fixed=shortFrame?s12(data,parity,nullptr,0,k):n-k==160?n10(data,parity,nullptr,0,k):n12(data,parity,nullptr,0,k);
  if(fixed<0) return -1;
  for(int i=0;i<n;++i) bits[i]=uint8_t(CODE::get_be_bit(data,i));
  return fixed;
 }
};
#endif
