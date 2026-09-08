/*
 *  Copyright 2020 Oleg Malyutin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "llr_demapper.h"

//#include <QDebug>
#include <immintrin.h>

#if defined(_MSC_VER)
#define ALIGNED_(x) __declspec(align(x))
#else
#if defined(__GNUC__)
#define ALIGNED_(x) __attribute__ ((aligned(x)))
#endif
#endif

//------------------------------------------------------------------------------------------
llr_demapper::llr_demapper(QMutex* _mutex, QObject* parent) :
    QObject(parent),
    mutex_in(_mutex)
{
    derotate_qpsk.real(cos(-ROT_QPSK));
    derotate_qpsk.imag(sin(-ROT_QPSK));
    derotate_qam16.real(cos(-ROT_QAM16));
    derotate_qam16.imag(sin(-ROT_QAM16));
    derotate_qam64.real(cos(-ROT_QAM64));
    derotate_qam64.imag(sin(-ROT_QAM64));
    derotate_qam256.real(cos(-ROT_QAM256));
    derotate_qam256.imag(sin(-ROT_QAM256));

    //address column twist deinterleaved and demultiplexer
    int column, row;
    column = 2025;
    row = 8;
    address_qam16_fecshort = new int[FEC_SIZE_SHORT];
    address_generator(column, row, address_qam16_fecshort, tc_qam16_short, demux_16);
    column = 8100;
    address_qam16_fecnormal = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam16_fecnormal, tc_qam16_normal, demux_16);
    address_qam16_fecnormal_3_5 = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam16_fecnormal_3_5, tc_qam16_normal, demux_16_fec_size_normal_code_3_5);
    column = 1350;
    row = 12;
    address_qam64_fecshort = new int[FEC_SIZE_SHORT];
    address_generator(column, row, address_qam64_fecshort, tc_qam64_short, demux_64);
    column = 5400;
    address_qam64_fecnormal = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam64_fecnormal, tc_qam64_normal, demux_64);
    address_qam64_fecnormal_3_5 = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam64_fecnormal_3_5, tc_qam64_normal, demux_64_fec_size_normal_code_3_5);
    column = 2025;
    row = 8;
    address_qam256_fecshort = new int[FEC_SIZE_SHORT];
    address_generator(column, row, address_qam256_fecshort, tc_qam256_short, demux_256_fec_size_short);
    column = 4050;
    row = 16;
    address_qam256_fecnormal = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam256_fecnormal, tc_qam256_normal, demux_256_fec_size_normal);
    address_qam256_fecnormal_3_5 = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam256_fecnormal_3_5, tc_qam256_normal, demux_256_fec_size_normal_3_5);
    address_qam256_fecnormal_2_3 = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam256_fecnormal_2_3, tc_qam256_normal, demux_256_fec_size_normal_2_3);

    buffer_a = new int8_t[FEC_SIZE_NORMAL * SIZEOF_SIMD];
    buffer_b = new int8_t[FEC_SIZE_NORMAL * SIZEOF_SIMD];

    decoder = new ldpc_decoder;
    thread = new QThread;
    thread->setObjectName("ldpc_decoder");
    decoder->moveToThread(thread);
    connect(this, &llr_demapper::soft_multiplexer_de_twist, decoder, &ldpc_decoder::execute, Qt::BlockingQueuedConnection);
    connect(thread, &QThread::finished, decoder, &QObject::deleteLater);
    thread->start();
    // Ensure its event loop is running before immediate stop/restart can occur.
    QMetaObject::invokeMethod(decoder,[]{},Qt::BlockingQueuedConnection);
}
//------------------------------------------------------------------------------------------
llr_demapper::~llr_demapper()
{
    thread->quit(); thread->wait(); delete thread;
    delete [] buffer_a;
    delete [] buffer_b;
    delete [] address_qam16_fecshort;
    delete [] address_qam16_fecnormal;
    delete [] address_qam16_fecnormal_3_5;
    delete [] address_qam64_fecshort;
    delete [] address_qam64_fecnormal;
    delete [] address_qam64_fecnormal_3_5;
    delete [] address_qam256_fecshort;
    delete [] address_qam256_fecnormal;
    delete [] address_qam256_fecnormal_2_3;
    delete [] address_qam256_fecnormal_3_5;
}
//------------------------------------------------------------------------------------------
void llr_demapper::address_generator(int _column, int _row, int* _address, const int* _tc,
                                     const int* _demux)
{
    int address[FEC_SIZE_NORMAL];
    for(int c = 0; c < _column; ++c){
        for(int r = 0; r < _row; ++r){
            address[c * _row + r] = _column * r + (c + _column - _tc[r]) % _column;
        }
    }
    int k = 0;
    int n = 0;
    int frame = _column * _row;
    for(int i = 0; i < frame; ++i){
        _address[i] =  address[_demux[n] + k];
        ++n;
        if(n == _row) {
            n = 0;
            k += _row;
        }
    }
}
//------------------------------------------------------------------------------------------
void llr_demapper::execute(int count, complex *cells, int index, l1_postsignalling post)
{
    if(!cells || !post.plp || index<0 || index>=post.num_plp) return;
    const auto &plp=post.plp[index];
    if(plp.plp_mod<0 || plp.plp_mod>3 || plp.plp_cod<0 || plp.plp_cod>5) return;
    const int bitsPerCell=2*(plp.plp_mod+1);
    const bool shortFrame=plp.plp_fec_type==FECFRAME_SHORT;
    const int fec=shortFrame?FEC_SIZE_SHORT:FEC_SIZE_NORMAL;
    const int cellsPerFec=fec/bitsPerCell;
    if(count<=0 || count%cellsPerFec) return;
    const float norms[]={NORM_FACTOR_QPSK,NORM_FACTOR_QAM16,NORM_FACTOR_QAM64,NORM_FACTOR_QAM256};
    const complex rotations[]={derotate_qpsk,derotate_qam16,derotate_qam64,derotate_qam256};
    const float norm=norms[plp.plp_mod];
    const complex rotation=plp.plp_rotation?rotations[plp.plp_mod]:complex(1,0);
    const int maxLevel=(1<<(bitsPerCell/2))-1;
    double signal=0,noise=0;
    for(int i=0;i<qMin(count,2048);++i) {
        const complex sample=cells[i]*rotation;
        for(float component:{sample.real(),sample.imag()}) {
            if(!std::isfinite(component)) return;
            float nearest=qBound(-float(maxLevel),2*std::round((component/norm-1)/2)+1,float(maxLevel))*norm;
            signal+=nearest*nearest; noise+=(component-nearest)*(component-nearest);
        }
    }
    const double ratio=signal/qMax(noise,1.0e-9);
    emit signal_noise_ratio(float(10*std::log10(qMax(ratio,1.0e-9))));
    const float precision=float(qBound(0.1,8.0*norm*ratio,10000.0));
    int *address=nullptr;
    if(plp.plp_mod==MOD_16QAM) address=shortFrame?address_qam16_fecshort:plp.plp_cod==C3_5?address_qam16_fecnormal_3_5:address_qam16_fecnormal;
    if(plp.plp_mod==MOD_64QAM) address=shortFrame?address_qam64_fecshort:plp.plp_cod==C3_5?address_qam64_fecnormal_3_5:address_qam64_fecnormal;
    if(plp.plp_mod==MOD_256QAM) address=shortFrame?address_qam256_fecshort:plp.plp_cod==C3_5?address_qam256_fecnormal_3_5:plp.plp_cod==C2_3?address_qam256_fecnormal_2_3:address_qam256_fecnormal;
    const int totalBlocks=count/cellsPerFec;
    for(int first=0;first<totalBlocks;first+=SIZEOF_SIMD) {
        const int blocks=qMin(SIZEOF_SIMD,totalBlocks-first);
        int indices[SIZEOF_SIMD]{};
        for(int block=0;block<blocks;++block) {
            indices[block]=index;
            int8_t *output=buffer_a+block*fec;
            int bit=0;
            for(int c=0;c<cellsPerFec;++c) {
                const complex sample=cells[(first+block)*cellsPerFec+c]*rotation;
                float i=sample.real(),q=sample.imag();
                for(int pair=0;pair<bitsPerCell/2;++pair) {
                    auto quantizeSafe=[precision](float value) {
                        if(!std::isfinite(value)) return int8_t(0);
                        return int8_t(std::round(qBound(-127.0f,value*precision,127.0f)));
                    };
                    output[address?address[bit]:bit]=quantizeSafe(i); ++bit;
                    output[address?address[bit]:bit]=quantizeSafe(q); ++bit;
                    const float threshold=norm*float(1<<(bitsPerCell/2-1-pair));
                    i=std::abs(i)-threshold; q=std::abs(q)-threshold;
                }
            }
        }
        emit soft_multiplexer_de_twist(indices,post,blocks*fec,buffer_a);
    }
}
void llr_demapper::stop()
{
    emit finished();
}
//------------------------------------------------------------------------------------------
