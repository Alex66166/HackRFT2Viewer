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
