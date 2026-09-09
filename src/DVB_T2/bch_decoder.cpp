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

#include <QDebug>
#include <chrono>

namespace {
QSemaphore &bbQueueSlots()
{
    static QSemaphore slots(16);
    return slots;
}
using perf_clock = std::chrono::steady_clock;
inline quint64 perfNs(perf_clock::time_point start)
{
    return static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            perf_clock::now() - start).count());
}
struct BchDiag
{
    perf_clock::time_point window = perf_clock::now();
    quint64 calls = 0;
    quint64 inputFrames = 0;
    quint64 acceptedFrames = 0;
    quint64 failedFrames = 0;
    quint64 correctedBits = 0;
    quint64 correctNs = 0;
    quint64 descrambleNs = 0;
    quint64 bbWaitNs = 0;
    quint64 totalNs = 0;
};
BchDiag &bchDiag()
{
    static BchDiag d;
    return d;
}
}

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
    connect(this, &bch_decoder::bit_descramble,
            deheader, &bb_de_header::execute, Qt::BlockingQueuedConnection);
    connect(thread, &QThread::finished, deheader, &QObject::deleteLater);
    thread->start(QThread::HighPriority);
    QMetaObject::invokeMethod(deheader, []{}, Qt::BlockingQueuedConnection);
}

bch_decoder::~bch_decoder()
{
    thread->quit();
    thread->wait();
    delete thread;
    delete [] buffer_a;
    delete [] buffer_b;
}

void bch_decoder::init_descrambler()
{
    int sr = 0x4A80;
    for (int i = 0; i < 54000; i++) {
        uint8_t b = ((sr) ^ (sr >> 1)) & 1;
        descrambler[i] = b;
        sr >>= 1;
        if(b)
            sr |= 0x4000;
    }
}

void bch_decoder::execute(int *_idx_plp_simd, l1_postsignalling _l1_post,
                          int _len_in, uint8_t* _in)
{
    int* plp_id = _idx_plp_simd;
    l1_postsignalling l1_post = _l1_post;
    int len_in = _len_in;
    uint8_t* in = _in;
    if(!plp_id || !in || !l1_post.plp ||
       plp_id[0] < 0 || plp_id[0] >= l1_post.num_plp)
        return;

    const auto totalStart = perf_clock::now();
    BchDiag &diag = bchDiag();
    ++diag.calls;

    int k_bch = 0;
    int n_bch = 0;
    const dvbt2_fectype_t fec_type =
        static_cast<dvbt2_fectype_t>(l1_post.plp[plp_id[0]].plp_fec_type);
    const dvbt2_code_rate_t code_rate =
        static_cast<dvbt2_code_rate_t>(l1_post.plp[plp_id[0]].plp_cod);

    if(fec_type == FEC_FRAME_NORMAL) {
        switch(code_rate) {
        case C1_2: k_bch = 32208; n_bch = 32400; break;
        case C3_5: k_bch = 38688; n_bch = 38880; break;
        case C2_3: k_bch = 43040; n_bch = 43200; break;
        case C3_4: k_bch = 48408; n_bch = 48600; break;
        case C4_5: k_bch = 51648; n_bch = 51840; break;
        case C5_6: k_bch = 53840; n_bch = 54000; break;
        }
    }
    else {
        switch(code_rate) {
        case C1_2: k_bch = 7032; n_bch = 7200; break;
        case C3_5: k_bch = 9552; n_bch = 9720; break;
        case C2_3: k_bch = 10632; n_bch = 10800; break;
        case C3_4: k_bch = 11712; n_bch = 11880; break;
        case C4_5: k_bch = 12432; n_bch = 12600; break;
        case C5_6: k_bch = 13152; n_bch = 13320; break;
        }
    }

    if(n_bch <= 0 || len_in % n_bch)
        return;

    int n = 0;
    for(int j = 0; j < len_in; j += n_bch) {
        ++frames;
        ++diag.inputFrames;

        const auto correctStart = perf_clock::now();
        const int corrected = corrector.correct(
            in + j, n_bch, k_bch, fec_type != FEC_FRAME_NORMAL);
        diag.correctNs += perfNs(correctStart);

        if(corrected < 0) {
            ++failedFrames;
            ++diag.failedFrames;
            ++n;
            continue;
        }

        correctedBits += quint64(corrected);
        diag.correctedBits += quint64(corrected);
        ++diag.acceptedFrames;

        const auto descrambleStart = perf_clock::now();
        auto bbBits = std::make_shared<std::vector<uint8_t>>(
            static_cast<size_t>(k_bch));
        for (int i = 0; i < k_bch; ++i)
            (*bbBits)[static_cast<size_t>(i)] = in[j + i] ^ descrambler[i];
        diag.descrambleNs += perfNs(descrambleStart);

        const int index = plp_id[n];
        auto ownedPost = std::make_shared<owned_l1_post>(l1_post);
        const auto waitStart = perf_clock::now();
        auto permit = acquire_async_queue_slot(bbQueueSlots());
        diag.bbWaitNs += perfNs(waitStart);
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

    const quint64 total = frames;
    const quint64 fixed = correctedBits;
    const quint64 failed = failedFrames;
    QMetaObject::invokeMethod(deheader,
        [this,total,fixed,failed] {
            deheader->set_bch_metrics(total,fixed,failed);
        },
        Qt::QueuedConnection);

    diag.totalNs += perfNs(totalStart);
    const auto now = perf_clock::now();
    const auto windowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - diag.window).count();
    if(windowMs >= 5000) {
        const double ms = 1.0e-6;
        const double correctPerFrame = diag.inputFrames
            ? diag.correctNs * ms / double(diag.inputFrames) : 0.0;
        qInfo().noquote()
            << QStringLiteral("PERF-BCH calls=%1 frames=%2 accepted=%3 failed=%4 "
                              "corrected_bits=%5 correct_ms=%6 correct_ms/frame=%7 "
                              "descramble_ms=%8 bb_queue_wait_ms=%9 total_ms=%10")
                   .arg(diag.calls)
                   .arg(diag.inputFrames)
                   .arg(diag.acceptedFrames)
                   .arg(diag.failedFrames)
                   .arg(diag.correctedBits)
                   .arg(diag.correctNs * ms, 0, 'f', 2)
                   .arg(correctPerFrame, 0, 'f', 3)
                   .arg(diag.descrambleNs * ms, 0, 'f', 2)
                   .arg(diag.bbWaitNs * ms, 0, 'f', 2)
                   .arg(diag.totalNs * ms, 0, 'f', 2);
        diag = BchDiag{};
    }
}

void bch_decoder::stop()
{
    emit finished();
}
