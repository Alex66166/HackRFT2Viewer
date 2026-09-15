#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <random>
#include "DVB_T2/time_deinterleaver.h"
#include "fec_reference.h"
int main(int argc,char**argv)
{
    QCoreApplication app(argc,argv);
    const auto expected=Reference::bch(51840,51648,false);
    const auto cells=Reference::qam64(Reference::ldpc45(expected));
    QMutex mutex;
    {
        llr_demapper qam(&mutex);
        const auto encoded=Reference::ldpc45(expected);
        std::vector<complex> qpsk;
        for(int frame=0;frame<33;++frame)for(size_t bit=0;bit<encoded.size();bit+=2)
            qpsk.emplace_back((encoded[bit]?-1:1)*float(NORM_FACTOR_QPSK),
                              (encoded[bit+1]?-1:1)*float(NORM_FACTOR_QPSK));
        l1_postsignalling_plp plp;plp.plp_type=1;plp.plp_fec_type=FEC_FRAME_NORMAL;plp.plp_cod=C4_5;
        l1_postsignalling post;post.num_plp=1;post.plp=&plp;
        int observed=0;
        QObject::connect(qam.decoder,&ldpc_decoder::check,qam.decoder,[&](int n,uint8_t *data){
            for(int i=0;i<n;++i)assert(data[i]==expected[size_t(i)%expected.size()]);observed+=n;
        },Qt::DirectConnection);
        qam.execute(int(qpsk.size()),qpsk.data(),0,post);qam.flushPending();
        QMetaObject::invokeMethod(qam.decoder,[]{},Qt::BlockingQueuedConnection);
        assert(observed==33*int(expected.size()));
        qInfo()<<"Independent nonzero QPSK 4/5, parity bypass, 33 frames PASS";
    }
    {
        // Nonzero, noisy FEC input exercises soft decoding, not only the
        // all-valid/no-iteration path. Fixed seed makes it reproducible.
        llr_demapper qam(&mutex);std::vector<complex> noisy;
        std::mt19937 random(755);std::normal_distribution<float> noise(0, std::sqrt(.5f/100.0f));
        for(int frame=0;frame<32;++frame)for(auto cell:cells)noisy.push_back(cell+complex(noise(random),noise(random)));
        l1_postsignalling_plp plp;plp.plp_type=1;plp.plp_mod=MOD_64QAM;plp.plp_rotation=1;
        plp.plp_fec_type=FEC_FRAME_NORMAL;plp.plp_cod=C4_5;
        l1_postsignalling post;post.num_plp=1;post.plp=&plp;int observed=0;
        QObject::connect(qam.decoder,&ldpc_decoder::check,qam.decoder,[&](int n,uint8_t *data){
            for(int i=0;i<n;++i)assert(data[i]==expected[size_t(i)%expected.size()]);observed+=n;
        },Qt::DirectConnection);
        qam.execute(int(noisy.size()),noisy.data(),0,post);
        QMetaObject::invokeMethod(qam.decoder,[]{},Qt::BlockingQueuedConnection);
        assert(observed==32*int(expected.size()));qInfo()<<"64-QAM 4/5 at 20 dB AWGN, 32 frames PASS";
    }
    // Match the supplied broadcast's two PLPs (108 + 48 normal FEC,
    // rotated 64-QAM 4/5, three TI blocks). Distinct payloads expose an
    // accidental PLP switch or SIMD metadata mix, even when FEC is valid.
    {
        time_deinterleaver ti(&mutex);
        const std::vector<uint8_t> expectedWords[]={expected,Reference::bch(51840,51648,false,917)};
        l1_postsignalling_plp plps[2];dynamic_plp dyn[2];
        l1_postsignalling post;post.num_plp=2;post.plp=plps;post.dyn.plp=dyn;
        l1_presignalling pre;pre.l1_post_size=902;
        std::vector<complex> frame(L1_PRE_CELL+pre.l1_post_size);
        const int counts[]={108,48};int start=0,observed[2]{};
        for(int p=0;p<2;++p){
            auto &plp=plps[p];plp.id=p;plp.plp_type=1;plp.plp_mod=MOD_64QAM;plp.plp_rotation=1;
            plp.plp_fec_type=FEC_FRAME_NORMAL;plp.plp_cod=C4_5;plp.plp_num_blocks_max=counts[p];
            plp.time_il_length=3;plp.frame_interval=1;dyn[p].id=p;dyn[p].num_blocks=counts[p];dyn[p].start=start;
            auto encoded=Reference::interleave(Reference::qam64(Reference::ldpc45(expectedWords[p])),counts[p],3);
            frame.insert(frame.end(),encoded.begin(),encoded.end());start+=encoded.size();
        }
        auto *ldpc=ti.qam->decoder;auto *bch=ldpc->decoder;
        QObject::connect(ldpc,&ldpc_decoder::bit_bch,ldpc,[&](int *ids,l1_postsignalling meta,int n,uint8_t *data){
            assert(n%51840==0);
            for(int word=0;word<n/51840;++word){int p=ids[word];assert(p>=0&&p<2&&meta.plp[p].id==p);
                assert(std::equal(data+word*51840,data+(word+1)*51840,expectedWords[p].begin()));++observed[p];}
        },Qt::DirectConnection);
        ti.start(dvbt2_parameters{},pre,post);
        const int p2cells=22432;ti.l1_dyn_execute(post,p2cells,frame.data());
        for(int pos=p2cells;pos<int(frame.size());pos+=27200)ti.execute(std::min(27200,int(frame.size())-pos),frame.data()+pos);
        QMetaObject::invokeMethod(ti.qam,[&]{ti.qam->flushPending();},Qt::BlockingQueuedConnection);
        for(QObject *stage:{static_cast<QObject*>(ldpc),static_cast<QObject*>(bch),static_cast<QObject*>(bch->deheader)})
            QMetaObject::invokeMethod(stage,[]{},Qt::BlockingQueuedConnection);
        assert(observed[0]==108&&observed[1]==48);
        qInfo()<<"Two broadcast-profile PLPs / P2 offset 2742 / 108+48 distinct FEC words PASS";
    }
    for(int tiLength:{0,1,3}) {
        const int blocks=tiLength==3?108:33;
        time_deinterleaver ti(&mutex);
        l1_postsignalling_plp plp;plp.id=17;plp.plp_type=1;plp.plp_mod=MOD_64QAM;
        plp.plp_rotation=1;plp.plp_fec_type=FEC_FRAME_NORMAL;plp.plp_cod=C4_5;
        plp.plp_num_blocks_max=blocks;plp.time_il_length=tiLength;plp.frame_interval=1;
        dynamic_plp dyn;dyn.id=17;dyn.num_blocks=blocks;
        l1_postsignalling post;post.num_plp=1;post.plp=&plp;post.dyn.plp=&dyn;
        int observed=0;
        QObject::connect(ti.qam->decoder,&ldpc_decoder::check,ti.qam->decoder,[&](int n,uint8_t *data){
            for(int i=0;i<n;++i)assert(data[i]==expected[size_t(i)%expected.size()]);
            observed+=n;
        },Qt::DirectConnection);
        auto frame=Reference::interleave(cells,blocks,tiLength);
        frame.insert(frame.begin(),L1_PRE_CELL,std::complex<float>{});
        ti.start(dvbt2_parameters{},l1_presignalling{},post);
        ti.l1_dyn_execute(post,int(frame.size()),frame.data());
        QMetaObject::invokeMethod(ti.qam,[&]{ti.qam->flushPending();},Qt::BlockingQueuedConnection);
        auto *ldpc=ti.qam->decoder;auto *bch=ldpc->decoder;
        for(QObject *stage:{static_cast<QObject*>(ldpc),static_cast<QObject*>(bch),static_cast<QObject*>(bch->deheader)})
            QMetaObject::invokeMethod(stage,[]{},Qt::BlockingQueuedConnection);
        assert(observed==blocks*int(expected.size()));
        qInfo()<<"Independent nonzero 64-QAM 4/5, TI length"<<tiLength<<"FEC frames"<<blocks<<"PASS";
    }
}
