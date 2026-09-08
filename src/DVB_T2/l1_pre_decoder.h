// L1-pre shortening/puncturing follows ETSI EN 302 755 and GNU Radio gr-dtv v3.10.12.
#ifndef L1_PRE_DECODER_H
#define L1_PRE_DECODER_H
#include <array>
#include <complex>
#include <cmath>
#include <algorithm>
#include "bch_corrector.h"
#include "LDPC/ldpc.hh"
#include "LDPC/dvb_t2_tables.hh"
#include "LDPC/generic.hh"
#include "LDPC/layered_decoder.hh"
class L1PreDecoder {
 using Algorithm=gnr::OffsetMinSumAlgorithm<float,gnr::NormalUpdate<float>,2>;
 LDPCDecoder<float,Algorithm> ldpc;
 BchCorrector bch;
 bool initialized=false;
 std::array<uint8_t,3240> bits{};
 std::array<float,16200> soft{};
 std::array<bool,12960> punctured{};
 static bool crcValid(const uint8_t *data){
  uint32_t crc=0xffffffff,expected=0;
  for(int i=0;i<168;++i){bool bit=data[i]^((crc>>31)&1);crc<<=1;if(bit)crc^=0x04c11db7;}
  for(int i=168;i<200;++i)expected=(expected<<1)|data[i];
  return crc==expected;
 }
 bool finish(uint8_t *output){
  int result=bch.correct(bits.data(),3240,3072,true);
  if(result<0 || !crcValid(bits.data()))return false;
  for(int i=200;i<3072;++i)if(bits[i])return false;
  correctedBits+=result;std::copy_n(bits.data(),200,output);return true;
 }
public:
 uint64_t correctedBits=0,ldpcRecovered=0,failed=0;
 bool decode(const std::complex<float>* cells,uint8_t *output){
  for(int i=0;i<200;++i)output[i]=cells[i].real()<0;
  if(crcValid(output))return true;
  bits.fill(0);std::copy_n(output,200,bits.data());
  for(int i=0;i<168;++i)bits[3072+i]=cells[200+i].real()<0;
  if(finish(output))return true;
  if(!initialized){
   LDPC<DVB_T2_TABLE_SHORT_C1_4> table;ldpc.init(&table);
   const int order[]={27,13,29,32,5,0,11,21,33,20,25,28,18,35,8,3,9,31,22,24,7,14,17,4,2,26,16,34,19,10,12,23,1,6,30,15};
   for(int c=0;c<32;++c)for(int j=0;j<(c==31?328:360);++j)punctured[j*36+order[c]]=true;
   initialized=true;
  }
  soft.fill(0);for(int i=200;i<3072;++i)soft[i]=1000;
  auto llr=[](float x){return std::isfinite(x)?std::max(-40.f,std::min(40.f,8*x)):0.f;};
  for(int i=0;i<200;++i)soft[i]=llr(cells[i].real());
  for(int i=0;i<168;++i)soft[3072+i]=llr(cells[200+i].real());
  int index=368;for(int i=0;i<12960;++i)if(!punctured[i])soft[3240+i]=llr(cells[index++].real());
  ldpc(soft.data(),soft.data()+3240,30,1);
  for(int i=0;i<3240;++i)bits[i]=soft[i]<0;
  if(finish(output)){++ldpcRecovered;return true;}
  ++failed;return false;
 }
};
#endif
