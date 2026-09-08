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
#include "async_pipeline_payload.h"
#include <QDebug>
#include <immintrin.h>
#include <chrono>
#include <cmath>
#include <memory>
namespace {
QSemaphore &ldpcQueueSlots()
{
    static QSemaphore queueSlots(8);
    return queueSlots;
}
using perf_clock = std::chrono::steady_clock;
inline quint64 perfNs(perf_clock::time_point start)
{
    return static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            perf_clock::now() - start).count());
}
struct QamDiag
{
    perf_clock::time_point window = perf_clock::now();
    quint64 calls = 0;
    quint64 cells = 0;
    quint64 fecBlocks = 0;
    quint64 dispatches = 0;
    quint64 snrNs = 0;
    quint64 mapNs = 0;
    quint64 ldpcWaitNs = 0;
    quint64 totalNs = 0;
};
QamDiag &qamDiag()
{
    static QamDiag d;
    return d;
}
inline __m256 quantizeVector(__m256 value, __m256 precision,
                             __m256 minimum, __m256 maximum)
{
    value = _mm256_mul_ps(value, precision);
    value = _mm256_max_ps(minimum, _mm256_min_ps(maximum, value));
    return _mm256_round_ps(value,
                           _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
}
}
#if defined(_MSC_VER)
#define ALIGNED_(x) __declspec(align(x))
#else
#if defined(__GNUC__)
#define ALIGNED_(x) __attribute__ ((aligned(x)))
#endif
#endif
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
    int column, row;
    column = 2025;
    row = 8;
    address_qam16_fecshort = new int[FEC_SIZE_SHORT];
    address_generator(column, row, address_qam16_fecshort, tc_qam16_short, demux_16);
    column = 8100;
    address_qam16_fecnormal = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam16_fecnormal, tc_qam16_normal, demux_16);
    address_qam16_fecnormal_3_5 = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam16_fecnormal_3_5, tc_qam16_normal,
                      demux_16_fec_size_normal_code_3_5);
    column = 1350;
    row = 12;
    address_qam64_fecshort = new int[FEC_SIZE_SHORT];
    address_generator(column, row, address_qam64_fecshort, tc_qam64_short, demux_64);
    column = 5400;
    address_qam64_fecnormal = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam64_fecnormal, tc_qam64_normal, demux_64);
    address_qam64_fecnormal_3_5 = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam64_fecnormal_3_5, tc_qam64_normal,
                      demux_64_fec_size_normal_code_3_5);
    column = 2025;
    row = 8;
    address_qam256_fecshort = new int[FEC_SIZE_SHORT];
    address_generator(column, row, address_qam256_fecshort, tc_qam256_short,
                      demux_256_fec_size_short);
    column = 4050;
    row = 16;
    address_qam256_fecnormal = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam256_fecnormal, tc_qam256_normal,
                      demux_256_fec_size_normal);
    address_qam256_fecnormal_3_5 = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam256_fecnormal_3_5, tc_qam256_normal,
                      demux_256_fec_size_normal_3_5);
    address_qam256_fecnormal_2_3 = new int[FEC_SIZE_NORMAL];
    address_generator(column, row, address_qam256_fecnormal_2_3, tc_qam256_normal,
                      demux_256_fec_size_normal_2_3);
    buffer_a = new int8_t[FEC_SIZE_NORMAL * SIZEOF_SIMD];
    buffer_b = new int8_t[FEC_SIZE_NORMAL * SIZEOF_SIMD];
    decoder = new ldpc_decoder;
    thread = new QThread;
    thread->setObjectName("ldpc_decoder");
    decoder->moveToThread(thread);
    connect(this, &llr_demapper::soft_multiplexer_de_twist,
            decoder, &ldpc_decoder::execute, Qt::BlockingQueuedConnection);
    connect(thread, &QThread::finished, decoder, &QObject::deleteLater);
    thread->start(QThread::HighPriority);
    QMetaObject::invokeMethod(decoder, []{}, Qt::BlockingQueuedConnection);
    qInfo().noquote()
        << QStringLiteral("PERF-DIAG: AVX2 QAM/LLR mapper active; worker timings are written every 5 s.");
}
llr_demapper::~llr_demapper()
{
    thread->quit();
    thread->wait();
    delete thread;
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
void llr_demapper::address_generator(int _column, int _row, int* _address,
                                     const int* _tc, const int* _demux)
{
    int address[FEC_SIZE_NORMAL];
    for(int c = 0; c < _column; ++c) {
        for(int r = 0; r < _row; ++r)
            address[c * _row + r] = _column * r + (c + _column - _tc[r]) % _column;
    }
    int k = 0;
    int n = 0;
    const int frame = _column * _row;
    for(int i = 0; i < frame; ++i) {
        _address[i] = address[_demux[n] + k];
        ++n;
        if(n == _row) {
            n = 0;
            k += _row;
        }
    }
}
void llr_demapper::execute(int count, complex *cells, int index, l1_postsignalling post)
{
    if(!cells || !post.plp || index < 0 || index >= post.num_plp)
        return;
    const auto callStart = perf_clock::now();
    QamDiag &diag = qamDiag();
    ++diag.calls;
    diag.cells += static_cast<quint64>(count);
    const auto &plp = post.plp[index];
    if(plp.plp_mod < 0 || plp.plp_mod > 3 || plp.plp_cod < 0 || plp.plp_cod > 5)
        return;
    const int bitsPerCell = 2 * (plp.plp_mod + 1);
    const bool shortFrame = plp.plp_fec_type == FECFRAME_SHORT;
    const int fec = shortFrame ? FEC_SIZE_SHORT : FEC_SIZE_NORMAL;
    const int cellsPerFec = fec / bitsPerCell;
    if(count <= 0 || count % cellsPerFec)
        return;
    const float norms[] = {
        NORM_FACTOR_QPSK,
        NORM_FACTOR_QAM16,
        NORM_FACTOR_QAM64,
        NORM_FACTOR_QAM256
    };
    const complex rotations[] = {
        derotate_qpsk,
        derotate_qam16,
        derotate_qam64,
        derotate_qam256
    };
    const float norm = norms[plp.plp_mod];
    const complex rotation = plp.plp_rotation ? rotations[plp.plp_mod] : complex(1, 0);
    const int maxLevel = (1 << (bitsPerCell / 2)) - 1;
    const auto snrStart = perf_clock::now();
    if(plp.plp_rotation) {
        for(int i = 0; i < count; ++i)
            cells[i] *= rotation;
    }
    double signal = 0.0;
    double noise = 0.0;
    const int snrSamples = qMin(count, 2048);
    for(int i = 0; i < snrSamples; ++i) {
        const complex sample = cells[i];
        for(float component : {sample.real(), sample.imag()}) {
            if(!std::isfinite(component))
                return;
            const float nearest = qBound(
                -float(maxLevel),
                2.0f * std::round((component / norm - 1.0f) / 2.0f) + 1.0f,
                float(maxLevel)) * norm;
            signal += nearest * nearest;
            noise += (component - nearest) * (component - nearest);
        }
    }
    const double ratio = signal / qMax(noise, 1.0e-9);
    emit signal_noise_ratio(float(10.0 * std::log10(qMax(ratio, 1.0e-9))));
    const float precision = float(qBound(0.1, 8.0 * norm * ratio, 10000.0));
    diag.snrNs += perfNs(snrStart);
    int *address = nullptr;
    if(plp.plp_mod == MOD_16QAM)
        address = shortFrame ? address_qam16_fecshort
                             : (plp.plp_cod == C3_5 ? address_qam16_fecnormal_3_5
                                                    : address_qam16_fecnormal);
    if(plp.plp_mod == MOD_64QAM)
        address = shortFrame ? address_qam64_fecshort
                             : (plp.plp_cod == C3_5 ? address_qam64_fecnormal_3_5
                                                    : address_qam64_fecnormal);
    if(plp.plp_mod == MOD_256QAM)
        address = shortFrame ? address_qam256_fecshort
                             : (plp.plp_cod == C3_5 ? address_qam256_fecnormal_3_5
                               : plp.plp_cod == C2_3 ? address_qam256_fecnormal_2_3
                                                    : address_qam256_fecnormal);
    ldpc_batch &batch = m_ldpcBatches[index];
    if(batch.fec != fec ||
       batch.modulation != plp.plp_mod ||
       batch.codeRate != plp.plp_cod ||
       batch.fecType != plp.plp_fec_type) {
        batch.fec = fec;
        batch.modulation = plp.plp_mod;
        batch.codeRate = plp.plp_cod;
        batch.fecType = plp.plp_fec_type;
        batch.blocks = 0;
        batch.soft.clear();
        batch.ids.clear();
        batch.soft.resize(static_cast<size_t>(fec) * SIZEOF_SIMD);
        batch.ids.resize(SIZEOF_SIMD, index);
    }
    if(batch.soft.size() != static_cast<size_t>(fec) * SIZEOF_SIMD)
        batch.soft.resize(static_cast<size_t>(fec) * SIZEOF_SIMD);
    if(batch.ids.size() != SIZEOF_SIMD)
        batch.ids.resize(SIZEOF_SIMD, index);
    const __m256 vPrecision = _mm256_set1_ps(precision);
    const __m256 vMinimum = _mm256_set1_ps(-127.0f);
    const __m256 vMaximum = _mm256_set1_ps(127.0f);
    const __m256 signbits = _mm256_set1_ps(-0.0f);
    const __m256 vNorm2 = _mm256_set1_ps(norm * 2.0f);
    const __m256 vNorm4 = _mm256_set1_ps(norm * 4.0f);
    const __m256 vNorm8 = _mm256_set1_ps(norm * 8.0f);
    auto quantizeScalar = [precision](float value) -> int8_t {
        if(!std::isfinite(value))
            return 0;
        const float q = qBound(-127.0f, value * precision, 127.0f);
        return static_cast<int8_t>(std::nearbyint(q));
    };
    const auto mapStart = perf_clock::now();
    const int totalBlocks = count / cellsPerFec;
    diag.fecBlocks += static_cast<quint64>(totalBlocks);
    for(int block = 0; block < totalBlocks; ++block) {
        int8_t *output = batch.soft.data() + static_cast<size_t>(batch.blocks) * fec;
        const complex *input = cells + static_cast<size_t>(block) * cellsPerFec;
        if(plp.plp_mod == MOD_QPSK) {
            int bit = 0;
            for(int c = 0; c < cellsPerFec; ++c) {
                output[bit++] = quantizeScalar(input[c].real());
                output[bit++] = quantizeScalar(input[c].imag());
            }
        }
        else {
            int bitBase = 0;
            int c = 0;
            for(; c + 4 <= cellsPerFec; c += 4) {
                const float *raw = reinterpret_cast<const float *>(input + c);
                const __m256 vIn = _mm256_loadu_ps(raw);
                const __m256 llr01 = quantizeVector(vIn, vPrecision, vMinimum, vMaximum);
                const __m256 vAbs = _mm256_andnot_ps(signbits, vIn);
                float ALIGNED_(32) l01[8];
                float ALIGNED_(32) l23[8];
                float ALIGNED_(32) l45[8];
                float ALIGNED_(32) l67[8];
                _mm256_store_ps(l01, llr01);
                if(plp.plp_mod == MOD_16QAM) {
                    const __m256 x2 = _mm256_sub_ps(vAbs, vNorm2);
                    _mm256_store_ps(l23,
                        quantizeVector(x2, vPrecision, vMinimum, vMaximum));
                    int *a = address + bitBase;
                    int n = 0;
                    for(int pair = 0; pair < 2; ++pair) {
                        const int e1 = n;
                        const int o1 = n + 1;
                        const int e2 = n + 2;
                        const int o2 = n + 3;
                        output[a[0]] = static_cast<int8_t>(l01[e1]);
                        output[a[1]] = static_cast<int8_t>(l01[o1]);
                        output[a[2]] = static_cast<int8_t>(l23[e1]);
                        output[a[3]] = static_cast<int8_t>(l23[o1]);
                        output[a[4]] = static_cast<int8_t>(l01[e2]);
                        output[a[5]] = static_cast<int8_t>(l01[o2]);
                        output[a[6]] = static_cast<int8_t>(l23[e2]);
                        output[a[7]] = static_cast<int8_t>(l23[o2]);
                        n += 4;
                        a += 8;
                    }
                    bitBase += 16;
                }
                else if(plp.plp_mod == MOD_64QAM) {
                    const __m256 x4 = _mm256_sub_ps(vAbs, vNorm4);
                    const __m256 ax4 = _mm256_andnot_ps(signbits, x4);
                    const __m256 x2 = _mm256_sub_ps(ax4, vNorm2);
                    _mm256_store_ps(l23,
                        quantizeVector(x4, vPrecision, vMinimum, vMaximum));
                    _mm256_store_ps(l45,
                        quantizeVector(x2, vPrecision, vMinimum, vMaximum));
                    int *a = address + bitBase;
                    int n = 0;
                    for(int pair = 0; pair < 2; ++pair) {
                        const int e1 = n;
                        const int o1 = n + 1;
                        const int e2 = n + 2;
                        const int o2 = n + 3;
                        output[a[0]] = static_cast<int8_t>(l01[e1]);
                        output[a[1]] = static_cast<int8_t>(l01[o1]);
                        output[a[2]] = static_cast<int8_t>(l23[e1]);
                        output[a[3]] = static_cast<int8_t>(l23[o1]);
                        output[a[4]] = static_cast<int8_t>(l45[e1]);
                        output[a[5]] = static_cast<int8_t>(l45[o1]);
                        output[a[6]] = static_cast<int8_t>(l01[e2]);
                        output[a[7]] = static_cast<int8_t>(l01[o2]);
                        output[a[8]] = static_cast<int8_t>(l23[e2]);
                        output[a[9]] = static_cast<int8_t>(l23[o2]);
                        output[a[10]] = static_cast<int8_t>(l45[e2]);
                        output[a[11]] = static_cast<int8_t>(l45[o2]);
                        n += 4;
                        a += 12;
                    }
                    bitBase += 24;
                }
                else {
                    const __m256 x8 = _mm256_sub_ps(vAbs, vNorm8);
                    const __m256 ax8 = _mm256_andnot_ps(signbits, x8);
                    const __m256 x4 = _mm256_sub_ps(ax8, vNorm4);
                    const __m256 ax4 = _mm256_andnot_ps(signbits, x4);
                    const __m256 x2 = _mm256_sub_ps(ax4, vNorm2);
                    _mm256_store_ps(l23,
                        quantizeVector(x8, vPrecision, vMinimum, vMaximum));
                    _mm256_store_ps(l45,
                        quantizeVector(x4, vPrecision, vMinimum, vMaximum));
                    _mm256_store_ps(l67,
                        quantizeVector(x2, vPrecision, vMinimum, vMaximum));
                    int *a = address + bitBase;
                    int n = 0;
                    for(int pair = 0; pair < 2; ++pair) {
                        const int e1 = n;
                        const int o1 = n + 1;
                        const int e2 = n + 2;
                        const int o2 = n + 3;
                        output[a[0]] = static_cast<int8_t>(l01[e1]);
                        output[a[1]] = static_cast<int8_t>(l01[o1]);
                        output[a[2]] = static_cast<int8_t>(l23[e1]);
                        output[a[3]] = static_cast<int8_t>(l23[o1]);
                        output[a[4]] = static_cast<int8_t>(l45[e1]);
                        output[a[5]] = static_cast<int8_t>(l45[o1]);
                        output[a[6]] = static_cast<int8_t>(l67[e1]);
                        output[a[7]] = static_cast<int8_t>(l67[o1]);
                        output[a[8]] = static_cast<int8_t>(l01[e2]);
                        output[a[9]] = static_cast<int8_t>(l01[o2]);
                        output[a[10]] = static_cast<int8_t>(l23[e2]);
                        output[a[11]] = static_cast<int8_t>(l23[o2]);
                        output[a[12]] = static_cast<int8_t>(l45[e2]);
                        output[a[13]] = static_cast<int8_t>(l45[o2]);
                        output[a[14]] = static_cast<int8_t>(l67[e2]);
                        output[a[15]] = static_cast<int8_t>(l67[o2]);
                        n += 4;
                        a += 16;
                    }
                    bitBase += 32;
                }
            }
            int bit = bitBase;
            for(; c < cellsPerFec; ++c) {
                float i = input[c].real();
                float q = input[c].imag();
                for(int pair = 0; pair < bitsPerCell / 2; ++pair) {
                    output[address[bit++]] = quantizeScalar(i);
                    output[address[bit++]] = quantizeScalar(q);
                    const float threshold =
                        norm * float(1 << (bitsPerCell / 2 - 1 - pair));
                    i = std::abs(i) - threshold;
                    q = std::abs(q) - threshold;
                }
            }
        }
        batch.ids[batch.blocks] = index;
        ++batch.blocks;
        if(batch.blocks == SIZEOF_SIMD) {
            auto soft = std::make_shared<std::vector<int8_t>>();
            soft->swap(batch.soft);
            auto ids = std::make_shared<std::vector<int>>(batch.ids);
            batch.blocks = 0;
            batch.soft.resize(static_cast<size_t>(fec) * SIZEOF_SIMD);
            batch.ids.assign(SIZEOF_SIMD, index);
            auto ownedPost = std::make_shared<owned_l1_post>(post);
            const auto waitStart = perf_clock::now();
            auto permit = acquire_async_queue_slot(ldpcQueueSlots());
            diag.ldpcWaitNs += perfNs(waitStart);
            ++diag.dispatches;
            ldpc_decoder *receiver = decoder;
            QMetaObject::invokeMethod(receiver,
                [receiver, ids, ownedPost, soft, permit] {
                    (void)permit;
                    receiver->execute(ids->data(), ownedPost->value,
                                      static_cast<int>(soft->size()), soft->data());
                },
                Qt::QueuedConnection);
        }
    }
    diag.mapNs += perfNs(mapStart);
    diag.totalNs += perfNs(callStart);
    const auto now = perf_clock::now();
    const auto windowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - diag.window).count();
    if(windowMs >= 5000) {
        const double ms = 1.0e-6;
        const double msPerFec = diag.fecBlocks
            ? (diag.mapNs * ms / double(diag.fecBlocks)) : 0.0;
        qInfo().noquote()
            << QStringLiteral("PERF-QAM calls=%1 cells=%2 fec=%3 dispatch=%4 "
                              "snr_ms=%5 map_ms=%6 map_ms/fec=%7 ldpc_queue_wait_ms=%8 "
                              "total_ms=%9 pending_fec=%10 mod=%11")
                   .arg(diag.calls)
                   .arg(diag.cells)
                   .arg(diag.fecBlocks)
                   .arg(diag.dispatches)
                   .arg(diag.snrNs * ms, 0, 'f', 2)
                   .arg(diag.mapNs * ms, 0, 'f', 2)
                   .arg(msPerFec, 0, 'f', 3)
                   .arg(diag.ldpcWaitNs * ms, 0, 'f', 2)
                   .arg(diag.totalNs * ms, 0, 'f', 2)
                   .arg(batch.blocks)
                   .arg(plp.plp_mod);
        diag = QamDiag{};
    }
}
void llr_demapper::stop()
{
    m_ldpcBatches.clear();
    emit finished();
}
