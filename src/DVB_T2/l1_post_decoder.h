// Single-block L1-post FEC. Shortening/puncturing tables from GNU Radio gr-dtv
// v3.10.12.0, Copyright Ron Economos et al., GPL-3.0-or-later.
#ifndef L1_POST_DECODER_H
#define L1_POST_DECODER_H
#include "l1_pre_decoder.h"
class L1PostDecoder {
 using Algorithm=gnr::OffsetMinSumAlgorithm<float,gnr::NormalUpdate<float>,2>;
 LDPCDecoder<float,Algorithm> ldpc;BchCorrector bch;bool initialized=false;
 std::array<uint8_t,7200> bits{};std::array<float,16200> soft{};
 std::array<bool,7032> shortened{};std::array<bool,9000> punctured{};
 static constexpr int padding[3][20]={
  {18,17,16,15,14,13,12,11,4,10,9,8,3,2,7,6,5,1,19,0},
  {18,17,16,15,14,13,12,11,4,10,9,8,7,3,2,1,6,5,19,0},
  {18,17,16,4,15,14,13,12,3,11,10,9,2,8,7,1,6,5,19,0}};
 static constexpr int puncture[3][25]={
  {6,4,18,9,13,8,15,20,5,17,2,24,10,22,12,3,16,23,1,14,0,21,19,7,11},
  {6,4,13,9,18,8,15,20,5,17,2,22,24,7,12,1,16,23,14,0,21,10,19,11,3},
  {6,15,13,10,3,17,21,8,5,19,2,23,16,24,7,18,1,12,20,0,4,14,9,11,22}};
 static void descramble(uint8_t* out,int size,bool scrambled){
  if(!scrambled)return;int sr=0x4a80;
  for(int i=0;i<size;++i){int b=(sr^(sr>>1))&1;out[i]^=b;sr>>=1;if(b)sr|=0x4000;}
 }
 static bool crcValid(const uint8_t* data,int size){
  uint32_t crc=0xffffffff,expected=0;
  for(int i=0;i<size-32;++i){bool b=data[i]^((crc>>31)&1);crc<<=1;if(b)crc^=0x04c11db7;}
  for(int i=size-32;i<size;++i)expected=(expected<<1)|data[i];return crc==expected;
 }
 bool finish(uint8_t* out,int size,bool scrambled){
  int result=bch.correct(bits.data(),7200,7032,true);if(result<0)return false;
  int index=0;for(int i=0;i<7032;++i){if(!shortened[i])out[index++]=bits[i];else if(bits[i])return false;}
  if(index!=size)return false;descramble(out,size,scrambled);if(!crcValid(out,size))return false;
  correctedBits+=result;return true;
 }
public:
 uint64_t correctedBits=0,ldpcRecovered=0,failed=0;
 bool decode(const uint8_t* input,int count,int infoSize,int modulation,bool scrambled,uint8_t* out,const float* inputLlr=nullptr){
  const int size=infoSize+32,kind=std::max(0,modulation-1),punc=9168+size-count;
  if(size<32 || size>7032 || modulation<0 || modulation>3 || punc<0 || punc>9000 || count<size+168)return false;
  std::copy_n(input,size,out);descramble(out,size,scrambled);if(crcValid(out,size))return true;
  shortened.fill(false);const int m=size<=360?19:(7032-size)/360,last=size<=360?360-size:7032-size-360*m;
  for(int i=0;i<m;++i){int group=padding[kind][i];for(int j=0;j<(group==19?192:360);++j)shortened[group*360+j]=true;}
  if(last){int group=padding[kind][m],begin=group*360+(group==19?192:360)-last;for(int j=0;j<last;++j)shortened[begin+j]=true;}
  int index=0;bits.fill(0);for(int i=0;i<7032;++i)if(!shortened[i])bits[i]=input[index++];
  if(index!=size)return false;std::copy_n(input+size,168,bits.data()+7032);
  if(finish(out,size,scrambled))return true;
  if(!initialized){LDPC<DVB_T2_TABLE_SHORT_C1_2> table;ldpc.init(&table);initialized=true;}
  punctured.fill(false);for(int i=0;i<punc;++i)punctured[(i%360)*25+puncture[kind][i/360]]=true;
  auto llr=[&](int i){float value=inputLlr?inputLlr[i]:(input[i]?-8.f:8.f);return std::isfinite(value)?std::max(-40.f,std::min(40.f,value)):0.f;};
  soft.fill(0);index=0;for(int i=0;i<7032;++i)soft[i]=shortened[i]?1000.f:llr(index++);
  for(int i=0;i<168;++i)soft[7032+i]=llr(index++);
  for(int i=0;i<9000;++i)if(!punctured[i])soft[7200+i]=llr(index++);
  if(index!=count)return false;
  ldpc(soft.data(),soft.data()+7200,30,1);for(int i=0;i<7200;++i)bits[i]=soft[i]<0;
  if(finish(out,size,scrambled)){++ldpcRecovered;return true;}++failed;return false;
 }
};
#endif
