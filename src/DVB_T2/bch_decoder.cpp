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
#include "bch_decoder.h"
#include "async_pipeline_payload.h"

namespace {
QSemaphore &bbQueueSlots()
{
    static QSemaphore slots(16);
    return slots;
}
}

//------------------------------------------------------------------------------------------
bch_decoder::bch_decoder(QObject *parent) : QObject(parent)
{
    buffer_a = new uint8_t[53840];
    buffer_b = new uint8_t[53840];
    out = buffer_a;

    init_descrambler();

    deheader = new bb_de_header;
    thread = new QThread;
    thread->setObjectName("bb_de_header");
    deheader->moveToThread(thread);
    connect(this, &bch_decoder::bit_descramble, deheader, &bb_de_header::execute, Qt::BlockingQueuedConnection);
    connect(thread, &QThread::finished, deheader, &QObject::deleteLater);
    thread->start(QThread::HighPriority);
    // Ensure its event loop is running before immediate stop/restart can occur.
    QMetaObject::invokeMethod(deheader,[]{},Qt::BlockingQueuedConnection);
}
//------------------------------------------------------------------------------------------
bch_decoder::~bch_decoder()
{
    thread->quit(); thread->wait(); delete thread;
    delete [] buffer_a;
    delete [] buffer_b;
}
//------------------------------------------------------------------------------------------
void bch_decoder::init_descrambler()
    {
      int sr = 0x4A80;
      for (int i = 0; i < 54000; i++) {
        uint8_t b = ((sr) ^ (sr >> 1)) & 1;
        descrambler[i] = b;
        sr >>= 1;
        if(b) {
          sr |= 0x4000;
        }
      }
    }
//------------------------------------------------------------------------------------------
void bch_decoder::execute(int *_idx_plp_simd, l1_postsignalling _l1_post, int _len_in, uint8_t* _in)
{
    int* plp_id = _idx_plp_simd;
    l1_postsignalling l1_post = _l1_post;
    int len_in = _len_in;
    uint8_t* in = _in;
    if(!plp_id || !in || !l1_post.plp || plp_id[0]<0 || plp_id[0]>=l1_post.num_plp) return;
    int k_bch=0,n_bch=0;
    dvbt2_fectype_t fec_type = static_cast<dvbt2_fectype_t>(l1_post.plp[plp_id[0]].plp_fec_type);
    dvbt2_code_rate_t code_rate = static_cast<dvbt2_code_rate_t>(l1_post.plp[plp_id[0]].plp_cod);
    if(fec_type == FEC_FRAME_NORMAL){
        switch(code_rate){
        case C1_2:
            k_bch = 32208;
            n_bch = 32400;
            break;
        case C3_5:
            k_bch = 38688;
            n_bch = 38880;
            break;
        case C2_3:
            k_bch = 43040;
            n_bch = 43200;
            break;
        case C3_4:
            k_bch = 48408;
            n_bch = 48600;
            break;
        case C4_5:
            k_bch = 51648;
            n_bch = 51840;
            break;
        case C5_6:
            k_bch = 53840;
            n_bch = 54000;
            break;
        }
    }
    else{
        switch(code_rate){
        case C1_2:
            k_bch = 7032;
            n_bch = 7200;
            break;
        case C3_5:
            k_bch = 9552;
            n_bch = 9720;
            break;
        case C2_3:
            k_bch = 10632;
            n_bch = 10800;
            break;
        case C3_4:
            k_bch = 11712;
            n_bch = 11880;
            break;
        case C4_5:
            k_bch = 12432;
            n_bch = 12600;
            break;
        case C5_6:
            k_bch = 13152;
            n_bch = 13320;
            break;
        }
    }

    if(n_bch<=0 || len_in%n_bch) return;

    int n = 0;
    for(int j = 0; j < len_in; j += n_bch) {
        ++frames;
        int corrected=corrector.correct(in+j,n_bch,k_bch,fec_type!=FEC_FRAME_NORMAL);
        if(corrected<0) { ++failedFrames; ++n; continue; }
        correctedBits+=quint64(corrected);

        auto bbBits = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(k_bch));
        for (int i = 0; i < k_bch; ++i)
            (*bbBits)[static_cast<size_t>(i)] = in[j + i] ^ descrambler[i];

        const int index = plp_id[n];
        auto ownedPost = std::make_shared<owned_l1_post>(l1_post);
        auto permit = acquire_async_queue_slot(bbQueueSlots());
        bb_de_header *receiver = deheader;
        QMetaObject::invokeMethod(receiver,
            [receiver, index, ownedPost, bbBits, permit] {
                (void)permit;
                receiver->execute(index, ownedPost->value,
                                  static_cast<int>(bbBits->size()), bbBits->data());
            },
            Qt::QueuedConnection);
        ++n;
    }

    const quint64 total=frames,fixed=correctedBits,failed=failedFrames;
    QMetaObject::invokeMethod(deheader,[this,total,fixed,failed]{ deheader->set_bch_metrics(total,fixed,failed); },Qt::QueuedConnection);
}
//------------------------------------------------------------------------------------------
void bch_decoder::stop()
{
    emit finished();
}
//------------------------------------------------------------------------------------------
