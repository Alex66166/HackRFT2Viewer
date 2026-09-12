// Independent forward test encoder. No receiver permutation/demapper helpers.
// ETSI EN 302 755 sections 6.1, 6.2, 6.3; GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <cassert>
#include <complex>
#include <cstdint>
#include <set>
#include <vector>
#include "DVB_T2/LDPC/ldpc.hh"
#include "DVB_T2/LDPC/dvb_t2_tables.hh"
namespace Reference {
inline std::vector<uint8_t> bch(int n, int k, bool shortFrame, int seed = 1, const std::vector<uint8_t>* payload = nullptr)
{
    const int m = shortFrame ? 14 : 16, order = (1 << m) - 1;
    const int primitive = shortFrame ? 0x402b : 0x1002d;
    const int capacity = (n-k == 160) ? 10 : 12;
    std::vector<int> exp(2*order), log(order+1);
    for(int i=0,x=1;i<order;++i) {
        exp[i]=exp[i+order]=x;log[x]=i;x<<=1;if(x & (1<<m))x^=primitive;
    }
    auto multiply=[&](int a,int b){return a&&b?exp[log[a]+log[b]]:0;};
    std::set<int> roots;
    for(int i=1;i<=2*capacity;++i) {
        int r=i;do {roots.insert(r);r=(r*2)%order;}while(r!=i);
    }
    std::vector<int> generator{1};
    for(int root:roots) {
        std::vector<int> next(generator.size()+1);
        for(size_t j=0;j<generator.size();++j) {next[j]^=multiply(generator[j],exp[root]);next[j+1]^=generator[j];}
        generator.swap(next);
    }
    assert(int(generator.size())==n-k+1);
    for(int c:generator)assert(c==0||c==1);
    std::vector<uint8_t> bits(n),remainder;
    uint32_t random=uint32_t(seed);
    for(int i=0;i<k;++i){random^=random<<13;random^=random>>17;random^=random<<5;bits[i]=random&1;}
    if(payload) {assert(int(payload->size())==k);std::copy(payload->begin(),payload->end(),bits.begin());}
    remainder=bits;
    for(int i=0;i<k;++i)if(remainder[i])
        for(int j=0;j<=n-k;++j)remainder[i+j]^=uint8_t(generator[n-k-j]);
    std::copy(remainder.begin()+k,remainder.end(),bits.begin()+k);
    return bits;
}
inline std::vector<uint8_t> ldpc45(const std::vector<uint8_t> &systematic)
{
    assert(systematic.size()==51840);
    std::vector<uint8_t> bits(64800);std::copy(systematic.begin(),systematic.end(),bits.begin());
    LDPC<DVB_T2_TABLE_NORMAL_C4_5> matrix;matrix.first_bit();
    for(int i=0;i<51840;++i){
        if(bits[i])for(int j=0;j<matrix.bit_deg();++j)bits[51840+matrix.acc_pos()[j]]^=1;
        if(i!=51839)matrix.next_bit();
    }
    for(int i=51841;i<64800;++i)bits[i]^=bits[i-1];
    return bits;
}
inline std::vector<std::complex<float>> qam64(const std::vector<uint8_t>& coded)
{
    std::vector<uint8_t> parity(64800);std::copy(coded.begin(),coded.begin()+51840,parity.begin());
    for(int t=0;t<36;++t)for(int s=0;s<360;++s)parity[51840+t*360+s]=coded[51840+s*36+t];
    const int twist[]={0,0,2,2,3,4,4,5,5,7,8,9};
    const int wire[]={11,8,5,2,10,7,4,1,9,6,3,0};
    const int levels[]={7,5,1,3,-7,-5,-1,-3};
    std::vector<std::complex<float>> out;out.reserve(10800);
    const auto rotation=std::polar(1.0f,float(8.6*3.141592653589793/180));
    for(int row=0;row<5400;++row){
        uint8_t label[12];for(int j=0;j<12;++j){int col=wire[j];label[j]=parity[col*5400+(row+5400-twist[col])%5400];}
        for(int off:{0,6}){int i=label[off]*4+label[off+2]*2+label[off+4];int q=label[off+1]*4+label[off+3]*2+label[off+5];out.push_back(std::complex<float>(levels[i],levels[q])/std::sqrt(42.0f)*rotation);}
    }
    return out;
}
inline std::vector<std::complex<float>> interleave(const std::vector<std::complex<float>>& fec, int blocks, int tiLength, const std::vector<std::vector<std::complex<float>>>* frames = nullptr)
{
    const int size=int(fec.size()),rows=size/5;
    int degree=0;while((1<<degree)<size)++degree;
    assert(degree==14); // this fixture exercises normal 64-QAM
    std::vector<int> perm;
    int state=0;
    for(int i=0;i<(1<<degree);++i){
        if(i<2)state=0;else if(i==2)state=1;else {int f=0;for(int tap:{0,1,4,5,9,11})f^=(state>>tap)&1;state=((state&8191)>>1)|(f<<12);}
        state=(state&8191)|((i&1)<<13);if(state<size)perm.push_back(state);
    }
    assert(int(perm.size())==size);
    std::vector<std::complex<float>> out;
    const int tiBlocks=tiLength?tiLength:blocks;
    int frameIndex=0;
    for(int t=0;t<tiBlocks;++t){
        const int count=blocks/tiBlocks+(t>=tiBlocks-blocks%tiBlocks?1:0);
        std::vector<std::complex<float>> cells(count*size);
        int shiftCandidate=0;
        for(int b=0;b<count;++b){
            int shift;
            do {int x=shiftCandidate++;shift=0;for(int i=0;i<degree;++i){shift=(shift|(x&1))<<1;x>>=1;}}while(shift>=size);
            const auto &source=frames?(*frames)[frameIndex++]:fec;
            for(int c=0;c<size;++c)cells[b*size+(perm[c]+shift)%size]={source[c].real(),source[(c+size-1)%size].imag()};
        }
        if(tiLength){int columns=count*5;for(int row=0;row<rows;++row)for(int col=0;col<columns;++col)out.push_back(cells[col*rows+row]);}
        else out.insert(out.end(),cells.begin(),cells.end());
    }
    return out;
}
}
