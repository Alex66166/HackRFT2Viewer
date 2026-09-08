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
#include "ldpc_decoder.h"
#include <cstring>

// #include <iostream>


constexpr int DVB_T2_TABLE_NORMAL_C1_2::DEG[];
constexpr int DVB_T2_TABLE_NORMAL_C1_2::LEN[];
constexpr int DVB_T2_TABLE_NORMAL_C1_2::POS[];

constexpr int DVB_T2_TABLE_NORMAL_C3_5::DEG[];
constexpr int DVB_T2_TABLE_NORMAL_C3_5::LEN[];
constexpr int DVB_T2_TABLE_NORMAL_C3_5::POS[];

constexpr int DVB_T2_TABLE_NORMAL_C2_3::DEG[];
constexpr int DVB_T2_TABLE_NORMAL_C2_3::LEN[];
constexpr int DVB_T2_TABLE_NORMAL_C2_3::POS[];

constexpr int DVB_T2_TABLE_NORMAL_C3_4::DEG[];
constexpr int DVB_T2_TABLE_NORMAL_C3_4::LEN[];
constexpr int DVB_T2_TABLE_NORMAL_C3_4::POS[];

constexpr int DVB_T2_TABLE_NORMAL_C4_5::DEG[];
constexpr int DVB_T2_TABLE_NORMAL_C4_5::LEN[];
constexpr int DVB_T2_TABLE_NORMAL_C4_5::POS[];

constexpr int DVB_T2_TABLE_NORMAL_C5_6::DEG[];
constexpr int DVB_T2_TABLE_NORMAL_C5_6::LEN[];
constexpr int DVB_T2_TABLE_NORMAL_C5_6::POS[];

constexpr int DVB_T2_TABLE_SHORT_C1_4::DEG[];
constexpr int DVB_T2_TABLE_SHORT_C1_4::LEN[];
constexpr int DVB_T2_TABLE_SHORT_C1_4::POS[];

constexpr int DVB_T2_TABLE_SHORT_C1_2::DEG[];
constexpr int DVB_T2_TABLE_SHORT_C1_2::LEN[];
constexpr int DVB_T2_TABLE_SHORT_C1_2::POS[];

constexpr int DVB_T2_TABLE_SHORT_C3_5::DEG[];
constexpr int DVB_T2_TABLE_SHORT_C3_5::LEN[];
constexpr int DVB_T2_TABLE_SHORT_C3_5::POS[];

constexpr int DVB_T2_TABLE_SHORT_C2_3::DEG[];
constexpr int DVB_T2_TABLE_SHORT_C2_3::LEN[];
constexpr int DVB_T2_TABLE_SHORT_C2_3::POS[];

constexpr int DVB_T2_TABLE_SHORT_C3_4::DEG[];
constexpr int DVB_T2_TABLE_SHORT_C3_4::LEN[];
constexpr int DVB_T2_TABLE_SHORT_C3_4::POS[];

constexpr int DVB_T2_TABLE_SHORT_C4_5::DEG[];
constexpr int DVB_T2_TABLE_SHORT_C4_5::LEN[];
constexpr int DVB_T2_TABLE_SHORT_C4_5::POS[];

constexpr int DVB_T2_TABLE_SHORT_C5_6::DEG[];
constexpr int DVB_T2_TABLE_SHORT_C5_6::LEN[];
constexpr int DVB_T2_TABLE_SHORT_C5_6::POS[];

constexpr int DVB_T2_TABLE_B8::DEG[];
constexpr int DVB_T2_TABLE_B8::LEN[];
constexpr int DVB_T2_TABLE_B8::POS[];

constexpr int DVB_T2_TABLE_B9::DEG[];
constexpr int DVB_T2_TABLE_B9::LEN[];
constexpr int DVB_T2_TABLE_B9::POS[];
//------------------------------------------------------------------------------------------
ldpc_decoder::ldpc_decoder(QObject *parent) : QObject(parent)
{
    ldpc_fec_normal_cod_1_2 = new LDPC<DVB_T2_TABLE_NORMAL_C1_2>();
    ldpc_fec_normal_cod_3_4 = new LDPC<DVB_T2_TABLE_NORMAL_C3_4>();
    ldpc_fec_normal_cod_2_3 = new LDPC<DVB_T2_TABLE_NORMAL_C2_3>();
    ldpc_fec_normal_cod_3_5 = new LDPC<DVB_T2_TABLE_NORMAL_C3_5>();
    ldpc_fec_normal_cod_4_5 = new LDPC<DVB_T2_TABLE_NORMAL_C4_5>();
    ldpc_fec_normal_cod_5_6 = new LDPC<DVB_T2_TABLE_NORMAL_C5_6>();

    ldpc_fec_short_cod_1_2 = new LDPC<DVB_T2_TABLE_SHORT_C1_2>();
    ldpc_fec_short_cod_3_4 = new LDPC<DVB_T2_TABLE_SHORT_C3_4>();
    ldpc_fec_short_cod_2_3 = new LDPC<DVB_T2_TABLE_SHORT_C2_3>();
    ldpc_fec_short_cod_3_5 = new LDPC<DVB_T2_TABLE_SHORT_C3_5>();
    ldpc_fec_short_cod_4_5 = new LDPC<DVB_T2_TABLE_SHORT_C4_5>();
    ldpc_fec_short_cod_5_6 = new LDPC<DVB_T2_TABLE_SHORT_C5_6>();

    decode_normal_cod_1_2.init(ldpc_fec_normal_cod_1_2);
    decode_normal_cod_3_4.init(ldpc_fec_normal_cod_3_4);
    decode_normal_cod_2_3.init(ldpc_fec_normal_cod_2_3);
    decode_normal_cod_3_5.init(ldpc_fec_normal_cod_3_5);
    decode_normal_cod_4_5.init(ldpc_fec_normal_cod_4_5);
    decode_normal_cod_5_6.init(ldpc_fec_normal_cod_5_6);

    decode_short_cod_1_2.init(ldpc_fec_short_cod_1_2);
    decode_short_cod_3_4.init(ldpc_fec_short_cod_3_4);
    decode_short_cod_2_3.init(ldpc_fec_short_cod_2_3);
    decode_short_cod_3_5.init(ldpc_fec_short_cod_3_5);
    decode_short_cod_4_5.init(ldpc_fec_short_cod_4_5);
    decode_short_cod_5_6.init(ldpc_fec_short_cod_5_6);

//    aligned_buffer = aligned_alloc(sizeof(simd_type), sizeof(simd_type) * FEC_SIZE_NORMAL);
//    simd = reinterpret_cast<simd_type*>(aligned_buffer);

    simd = new simd_type[FEC_SIZE_NORMAL];

    ldpc_fec = new uint8_t[FEC_SIZE_NORMAL * SIZEOF_SIMD];  // for fec size normal
    const unsigned int len_buffer = 54000 * SIZEOF_SIMD;    // for ldpc code 5/6
    buffer_a = new uint8_t[len_buffer];
    buffer_b = new uint8_t[len_buffer];
    bch_fec = buffer_a;

    decoder = new bch_decoder;
    thread = new QThread;
    thread->setObjectName("bch_decoder");
    decoder->moveToThread(thread);
    connect(this, &ldpc_decoder::bit_bch, decoder, &bch_decoder::execute, Qt::BlockingQueuedConnection);
    connect(thread, &QThread::finished, decoder, &QObject::deleteLater);
    thread->start();
    // Ensure its event loop is running before immediate stop/restart can occur.
    QMetaObject::invokeMethod(decoder,[]{},Qt::BlockingQueuedConnection);
}
//------------------------------------------------------------------------------------------
ldpc_decoder::~ldpc_decoder()
{
    thread->quit(); thread->wait(); delete thread;
    delete ldpc_fec_normal_cod_1_2;
    delete ldpc_fec_normal_cod_3_5;
    delete ldpc_fec_normal_cod_2_3;
    delete ldpc_fec_normal_cod_3_4;
    delete ldpc_fec_normal_cod_4_5;
    delete ldpc_fec_normal_cod_5_6;
    delete ldpc_fec_short_cod_1_2;
    delete ldpc_fec_short_cod_3_4;
    delete ldpc_fec_short_cod_2_3;
    delete ldpc_fec_short_cod_3_5;
    delete ldpc_fec_short_cod_4_5;
    delete ldpc_fec_short_cod_5_6;
    delete [] ldpc_fec;
    delete [] simd;
    delete [] buffer_a;
    delete [] buffer_b;
}
//------------------------------------------------------------------------------------------
void ldpc_decoder::execute(int* _idx_plp_simd, l1_postsignalling _l1_post, int _len_in, int8_t* _in)
{
    int* plp_id = _idx_plp_simd;
    l1_postsignalling l1_post = _l1_post;
    int8_t* in = _in;
    int len_in = _len_in;
    if(!plp_id || !in || !l1_post.plp || plp_id[0]<0 || plp_id[0]>=l1_post.num_plp) return;
    int k_ldpc=0,q_ldpc=0;
    dvbt2_fectype_t fec_type = static_cast<dvbt2_fectype_t>(l1_post.plp[plp_id[0]].plp_fec_type);
    dvbt2_code_rate_t code_rate = static_cast<dvbt2_code_rate_t>(l1_post.plp[plp_id[0]].plp_cod);
    int fec_size;

    if (fec_type == FEC_FRAME_NORMAL) {
        fec_size = FEC_SIZE_NORMAL;
      switch (code_rate) {
        case C1_2:
          p_decode = &decode_normal_cod_1_2;
          k_ldpc = 32400;
          q_ldpc = 90;
          break;
        case C3_5:
          p_decode = &decode_normal_cod_3_5;
          k_ldpc = 38880;
          q_ldpc = 72;
          break;
        case C2_3:
          p_decode = &decode_normal_cod_2_3;
          k_ldpc = 43200;
          q_ldpc = 60;
          break;
        case C3_4:
          p_decode = &decode_normal_cod_3_4;
          k_ldpc = 48600;
          q_ldpc = 45;
          break;
        case C4_5:
          p_decode = &decode_normal_cod_4_5;
          k_ldpc = 51840;
          q_ldpc = 36;
          break;
        case C5_6:
          p_decode = &decode_normal_cod_5_6;
          k_ldpc = 54000;
          q_ldpc = 30;
          break;
      }
    }
    else{
        fec_size = FEC_SIZE_SHORT;
      switch (code_rate) {
        case C1_2:
          p_decode = &decode_short_cod_1_2;
          k_ldpc = 7200;
          q_ldpc = 25;
          break;
        case C3_5:
          p_decode = &decode_short_cod_3_5;
          k_ldpc = 9720;
          q_ldpc = 18;
          break;
        case C2_3:
          p_decode = &decode_short_cod_2_3;
          k_ldpc = 10800;
          q_ldpc = 15;
          break;
        case C3_4:
          p_decode = &decode_short_cod_3_4;
          k_ldpc = 11880;
          q_ldpc = 12;
          break;
        case C4_5:
          p_decode = &decode_short_cod_4_5;
          k_ldpc = 12600;
          q_ldpc = 10;
          break;
        case C5_6:
          p_decode = &decode_short_cod_5_6;
          k_ldpc = 13320;
          q_ldpc = 8;
          break;
      }
    }

    if(k_ldpc<=0 || len_in<=0 || len_in%fec_size) return;
    const int blocks=len_in/fec_size;
    if(blocks>SIZEOF_SIMD) return;
    std::memset(simd,0,sizeof(simd_type)*fec_size);
    int k = 0;
    for(int j = 0; j < len_in; j += fec_size) {
        for (int i = 0; i < k_ldpc; ++i) {
            reinterpret_cast<code_type*>(simd + i)[k] = in[j + i];
        }
        for (int t = 0; t < q_ldpc; ++t) {
            for (int s = 0; s < 360; ++s) {
                reinterpret_cast<code_type*>(simd + k_ldpc + q_ldpc * s + t)[k] =
                        in[j + k_ldpc + 360 * t + s];
            }
        }
        ++k;
    }

    int trials = TRIALS;
    (*p_decode)(simd,simd+k_ldpc,trials,blocks);
    int8_t *s;
    for(int j = 0; j < blocks; ++j) {
        for (int i = 0; i < k_ldpc; ++i) {
            s = reinterpret_cast<code_type*>(simd + i);
            if(s[j] < 0) *bch_fec++ = 1;
            else         *bch_fec++ = 0;
        }
    }

    int len_out = k_ldpc * blocks;
    if(swap_buffer) {
        swap_buffer = false;

        emit bit_bch(plp_id, l1_post, len_out, buffer_a);

        bch_fec = buffer_b;
    }
    else {
        swap_buffer = true;

        emit bit_bch(plp_id, l1_post, len_out, buffer_b);

        bch_fec = buffer_a;
    }

}
//------------------------------------------------------------------------------------------
void ldpc_decoder::stop()
{
    emit finished();
}
//------------------------------------------------------------------------------------------
