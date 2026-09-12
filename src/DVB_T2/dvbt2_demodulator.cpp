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
#include "dvbt2_demodulator.h"
#include "DSP/complex_rotator.h"
#include "DSP/guard_acquisition.h"

#include <immintrin.h>
#include <chrono>

#include "DSP/fast_math.h"

namespace {
using profile_clock = std::chrono::steady_clock;
inline quint64 profile_ns(profile_clock::time_point t0)
{
    return static_cast<quint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        profile_clock::now() - t0).count());
}
}

//-------------------------------------------------------------------------------------------
dvbt2_demodulator::dvbt2_demodulator(float _level_min, float _sample_rate, QObject *parent) :
    QObject(parent),
    sample_rate(_sample_rate),
    level_min(_level_min)
{
    table_sin_cos_instance.table_();

    mutex = new QMutex;
    // max parameters
    unsigned int max_len_symbol = FFT_32K + FFT_32K / 4 + P1_LEN;
    resample =  sample_rate / (SAMPLE_RATE * upsample);
    max_sample_rate_deviation = resample * 1.0e-4;// for 100ppm
    // Maximum INPUT chunk and maximum OUTPUT chunk are different when the
    // HackRF runs at 8 MS/s.  1.3.0 incorrectly sized the post-resampler
    // buffer from the input count; at 8 MS/s that buffer was about 12.5% too
    // small and could be overwritten.
    const uint len_max = static_cast<uint>(std::ceil((max_len_symbol + P1_LEN)
                                * (resample + max_sample_rate_deviation) * upsample)) + 64u;
    out_resampled_capacity = static_cast<int>(max_len_symbol + P1_LEN + 512u);

    direct_resampler = new bandlimited_resampler(sample_rate, SAMPLE_RATE);
    out_resampled = new complex[out_resampled_capacity];
    out_derotate_sample = new complex[len_max];
    buffer_sym = new complex[max_len_symbol];
    for(uint i = 0; i < max_len_symbol; ++i) {
        buffer_sym[i] = {0.0f, 0.0f};
    }
    //p1 symbol
    p1_demodulator = new p1_symbol();
    //__Fast Fourier Transform__
    fft = new fast_fourier_transform;
    //pilot generator
    pilot = new pilot_generator();
    //address of frequency deinterleaved
    fq_deinterleaver = new address_freq_deinterleaver();
    //p2 symbol
    p2_demodulator = new p2_symbol();
    //data symbol
    data_demodulator = new data_symbol();
    //frame closing (fc) symbol
    fc_demod = new fc_symbol();
    //time deinterleaver and removal of cyclic Q-delay
    mutex_out = new QMutex;
    deinterleaver = new time_deinterleaver(mutex_out);
    thread = new QThread;
    thread->setObjectName("time_deinterleaver");
    deinterleaver->moveToThread(thread);
    connect(this, &dvbt2_demodulator::data, deinterleaver, &time_deinterleaver::execute, Qt::BlockingQueuedConnection);
    connect(this, &dvbt2_demodulator::l1_dyn_execute, deinterleaver, &time_deinterleaver::l1_dyn_execute, Qt::BlockingQueuedConnection);
//    thread->start(QThread::TimeCriticalPriority);
    connect(thread, &QThread::finished, deinterleaver, &QObject::deleteLater);
    thread->start();
    // Ensure its event loop is running before immediate stop/restart can occur.
    QMetaObject::invokeMethod(deinterleaver,[]{},Qt::BlockingQueuedConnection);
}
//-------------------------------------------------------------------------------------------
dvbt2_demodulator::~dvbt2_demodulator()
{   
    thread->quit(); thread->wait(); delete thread;
    delete p1_demodulator;
    delete p2_demodulator;
    delete data_demodulator;
    delete fc_demod;
    delete fft; delete pilot; delete fq_deinterleaver; delete direct_resampler; delete mutex; delete mutex_out;
    delete [] out_derotate_sample;
    delete [] out_resampled;
    delete [] buffer_sym;
}
//-------------------------------------------------------------------------------------------
dvbt2_stage_profile dvbt2_demodulator::take_stage_profile()
{
    const dvbt2_stage_profile result = stage_profile;
    stage_profile = {};
    return result;
}
//-------------------------------------------------------------------------------------------
void dvbt2_demodulator::reset()
{
    exp_avg_dc_real.reset();
    exp_avg_dc_imag.reset();
    loop_filter_frequency_offset.reset();
    frequency_est_filtered = 0;
    frequency_nco = 0;
    loop_filter_phase_offset.reset();
    phase_nco = 0.0f;
    loop_filter_sample_rate_offset.reset();
    old_sample_rate_est = 0.0f;
    sample_rate_est_filtered = 0;
    resample =  sample_rate / (SAMPLE_RATE * upsample);
    p2_init = false;cpConfidence=0;measuredGuard=0;
    guardCoherence=0;guardRepeatabilityDb=0;residualFrequencyHz=0;guardQualitySymbols=0;
    demodulator_init = false;
    next_symbol_type = SYMBOL_TYPE_P1;
}
//-------------------------------------------------------------------------------------------
void dvbt2_demodulator::init_dvbt2()
{
    dvbt2.bandwidth = BANDWIDTH_8_0_MHZ;
    dvbt2.miso_group = MISO_TX1;//?
    dvbt2_p2_parameters_init(dvbt2);
    in_fft = fft->init(dvbt2.fft_size);
    fq_deinterleaver->init(dvbt2);
    p2_demodulator->init(dvbt2, pilot, fq_deinterleaver);
    // for start;
    dvbt2.guard_interval_size = dvbt2.fft_size / 4;
    symbol_size = dvbt2.fft_size + dvbt2.guard_interval_size;
    est_chunk = symbol_size;
    //..
    p2_init = true;
 }
//-------------------------------------------------------------------------------------------
void dvbt2_demodulator::execute(const int _len_in, complex* _in, signal_estimate* signal_)
{
    mutex->lock();
    processedSamples+=quint64(_len_in);

    const int len_in = _len_in;
    int idx_in = 0;

    while(idx_in < len_in) {

        if(est_chunk == 0) {
            est_chunk = symbol_size;
            if(next_symbol_type == SYMBOL_TYPE_P1) est_chunk += P1_LEN;
        }

        double arbitrary_resample = resample - sample_rate_est_filtered;
        const double direct_input_step = arbitrary_resample * upsample;

        chunk = static_cast<int>(std::nearbyint(est_chunk * direct_input_step));
        int remain = len_in - idx_in;
        if(chunk > remain) chunk = remain;

        // Apply each phase estimate once, at the completed symbol below.
        while(phase_nco > M_PI_X_2) {
            phase_nco -= M_PI_X_2;
        }
        while(phase_nco < -M_PI_X_2) {
            phase_nco += M_PI_X_2;
        }
        const complex *in = _in + idx_in;

        const auto t_rotate = profile_clock::now();
        rotate_samples(in,out_derotate_sample,chunk,frequency_nco,-double(frequency_est_filtered),phase_nco);
        stage_profile.inputRotateNs += profile_ns(t_rotate);
        idx_in += chunk;
        //___timing synchronization___
        // Convert directly from the HackRF rate to the DVB-T2 elementary
        // rate.  This replaces the old Farrow -> ~18.285 MS/s -> 64-tap /2
        // path, whose expensive FIR work stayed almost constant at 8 MS/s.
        const auto t_resampler = profile_clock::now();
        const int len_out_resampled = direct_resampler->execute(
                    chunk, out_derotate_sample, direct_input_step,
                    out_resampled, out_resampled_capacity);
        stage_profile.resamplerNs += profile_ns(t_resampler);
        //___demodulations and get offset synchronization__
        if(len_out_resampled > 0) {
            const auto t_symbols = profile_clock::now();
            symbol_acquisition(len_out_resampled, out_resampled, signal_);
            stage_profile.symbolAcquireNs += profile_ns(t_symbols);
        }
        if(signal_->reset) break;

    }

    mutex->unlock();

}
//-------------------------------------------------------------------------------------------
void dvbt2_demodulator::symbol_acquisition(int _len_in, complex* _in, signal_estimate* signal_)
{
    int len_in = _len_in;
    complex* in = _in;

    float phase_est = 0.0f;
    float frequency_est = 0.0f;
    float sample_rate_est = 0.0f;

    int consume = 0;
    while(consume < len_in) {

        if(next_symbol_type == SYMBOL_TYPE_P1) {

            bool p1_decoded = false;
            const auto t_p1 = profile_clock::now();
            const bool p1_detected = p1_demodulator->execute(level_min, len_in, in, consume, symbol_synchronize,
                                       buffer_sym, idx_buffer_sym, dvbt2, signal_->coarse_freq_offset,
                                       p1_decoded, signal_->p1_reset);
            stage_profile.p1Ns += profile_ns(t_p1);
            ++stage_profile.p1Calls;
            if(p1_detected) {
                if(p1_decoded) {
                    ++p1Matches;
                    if(dvbt2.preamble!=T2_SISO || (dvbt2.fft_mode!=FFTSIZE_16K && dvbt2.fft_mode!=FFTSIZE_32K && dvbt2.fft_mode!=FFTSIZE_16K_T2GI && dvbt2.fft_mode!=FFTSIZE_32K_T2GI)) {
                        if(p1Matches==1)emit receiver_stage(QStringLiteral("P1 найден: неподдерживаемый режим. Поддерживаются DVB-T2 SISO, FFT 16K/32K, полоса 8 МГц."));
                        reset();signal_->reset=true;return;
                    }
                }
                if(p1_decoded){
                    if(std::abs(signal_->coarse_freq_offset)>100.0){
                        signal_->change_frequency=true;return;
                    }
                    if(!p2_init)init_dvbt2();
                    next_symbol_type=SYMBOL_TYPE_P2;
                }

            }

            continue;

        }
        //__Fast Fourier Transform_________________________________
        uint len_in_sym = len_in - consume;
        uint len_out_sym = symbol_size- idx_buffer_sym;
        uint len_cpy_sym = len_out_sym > len_in_sym ? len_in_sym : len_out_sym;
        memcpy(buffer_sym + idx_buffer_sym, in + consume, sizeof(complex) * len_cpy_sym);
        consume += len_cpy_sym;
        idx_buffer_sym += len_cpy_sym;

        if(idx_buffer_sym == symbol_size) {
            idx_buffer_sym = 0;
            int s_max = 0;
            if(crc32_l1_pre) {
                complex* cp = buffer_sym + dvbt2.fft_size;
                complex sum = {0.0f, 0.0f};

                for (int i = 0; i < dvbt2.guard_interval_size; ++i){
                    sum += (cp[i] * conj(buffer_sym[i]));
                }
                frequency_est = std::arg(sum) / dvbt2.fft_size * SAMPLE_RATE / sample_rate;
                // Compare repeated samples away from CP boundaries. This is
                // a modulation-independent quality diagnostic, not calibrated C/N:
                // multipath, timing error and interference also reduce coherence.
                const int margin=std::max(16,std::min(256,dvbt2.guard_interval_size/4));
                std::complex<double> repeat{};double energy=0;
                for(int i=margin;i<dvbt2.guard_interval_size-margin;++i){
                    repeat+=std::complex<double>(cp[i]*conj(buffer_sym[i]));
                    energy+=std::norm(cp[i])+std::norm(buffer_sym[i]);
                }
                if(energy>1e-15){
                    const double coherence=std::clamp(2*std::abs(repeat)/energy,0.,1.-1e-9);
                    guardCoherence=guardQualitySymbols?0.95*guardCoherence+0.05*coherence:coherence;
                    guardRepeatabilityDb=10*std::log10(std::max(1e-9,guardCoherence)/(1-guardCoherence));
                    residualFrequencyHz=std::arg(repeat)/dvbt2.fft_size*SAMPLE_RATE/(2*M_PI);
                    ++guardQualitySymbols;
                }
                float max_integral = 0.5f / dvbt2.fft_size;
                frequency_est_filtered += loop_filter_frequency_offset(frequency_est, max_integral);
            }

            if(!demodulator_init){
                const auto t_guard = profile_clock::now();
                const auto estimate=acquire_guard(buffer_sym,dvbt2.fft_size);
                stage_profile.guardNs += profile_ns(t_guard);
                measuredGuard=estimate.samples;cpConfidence=estimate.confidence;
                if(estimate.samples<=0 || estimate.confidence<.12f){
                    next_symbol_type=SYMBOL_TYPE_P1;est_chunk=0;continue;
                }
                dvbt2.guard_interval_size=estimate.samples;
                double phase=0;
                rotate_samples(buffer_sym+estimate.samples,in_fft,dvbt2.fft_size,phase,-estimate.radiansPerSample);
                frequency_est_filtered+=float(estimate.radiansPerSample*SAMPLE_RATE/sample_rate);
            }else{
                memcpy(in_fft,buffer_sym+dvbt2.guard_interval_size,sizeof(complex)*dvbt2.fft_size);
            }
            const auto t_fft = profile_clock::now();
            ofdm_cell = fft->execute();
            stage_profile.fftNs += profile_ns(t_fft);
            ++stage_profile.fftCalls;

            est_chunk = 0;

        }
        else {
            est_chunk = symbol_size - idx_buffer_sym;

            continue;

        }
        //________________________________________________________
        if(next_symbol_type == SYMBOL_TYPE_DATA) {
            const auto t_data = profile_clock::now();
            complex* deinterleaved_cell = data_demodulator->execute(idx_symbol, ofdm_cell,
                                                              sample_rate_est, phase_est);
            stage_profile.dataDemodNs += profile_ns(t_data);
            ++stage_profile.dataCalls;
            if(deint_start) {

                const auto t_downstream = profile_clock::now();
                emit data(dvbt2.c_data, deinterleaved_cell);
                stage_profile.downstreamDataNs += profile_ns(t_downstream);

            }
            ++idx_symbol;
            if(idx_symbol == end_data_symbol) {
                if(frame_closing_symbol) next_symbol_type = SYMBOL_TYPE_FC;
                else next_symbol_type = SYMBOL_TYPE_P1;
            }
        }
        else if(next_symbol_type == SYMBOL_TYPE_FC) {
            const auto t_fc = profile_clock::now();
            complex* deinterleaved_cell = fc_demod->execute(ofdm_cell, sample_rate_est, phase_est);
            stage_profile.fcDemodNs += profile_ns(t_fc);
            ++stage_profile.fcCalls;
            if(deint_start) {

                const auto t_downstream = profile_clock::now();
                emit data(dvbt2.n_fc, deinterleaved_cell);
                stage_profile.downstreamDataNs += profile_ns(t_downstream);

            }
            next_symbol_type = SYMBOL_TYPE_P1;
        }
        else if(next_symbol_type == SYMBOL_TYPE_P2) {
            ++p2Attempts;
            idx_symbol = 0;
            bool crc32_l1_post = false;
            symbol_synchronize = true;
            const auto t_p2 = profile_clock::now();
            complex* deinterleaved_cell = p2_demodulator->execute(dvbt2, demodulator_init, idx_symbol, ofdm_cell,
                                                                  l1_pre, l1_post, crc32_l1_pre, crc32_l1_post,
                                                                  sample_rate_est, phase_est, symbol_synchronize);
            stage_profile.p2Ns += profile_ns(t_p2);
            ++stage_profile.p2Calls;
            if(crc32_l1_pre) {
                ++l1PreMatches;if(crc32_l1_post)++l1PostMatches;
                if(demodulator_init) {
                    if(crc32_l1_post) {

                        for(int plp=0;plp<l1_post.num_plp;++plp) if(l1_post.plp[plp].time_il_type) {
                            if(l1PostMatches==1)emit receiver_stage(QStringLiteral("L1 найден: межкадровое перемежение PLP пока не поддерживается."));reset();signal_->reset=true;return;
                        }
                        if(!deint_start) {
                            const auto t_control = profile_clock::now();
                            QMetaObject::invokeMethod(deinterleaver,[this]{ deinterleaver->start(dvbt2,l1_pre,l1_post); },Qt::BlockingQueuedConnection);
                            stage_profile.downstreamControlNs += profile_ns(t_control);
                            deint_start = true;

                            emit amount_plp(l1_post.num_plp);

                        }

                        const auto t_control = profile_clock::now();
                        emit l1_dyn_execute(l1_post, dvbt2.c_p2, deinterleaved_cell);
                        stage_profile.downstreamControlNs += profile_ns(t_control);

                    }
                    ++idx_symbol;
                    next_symbol_type = SYMBOL_TYPE_DATA;
                }
                else {
                    set_guard_interval();
                    data_demodulator->init(dvbt2, pilot, fq_deinterleaver);
                    end_data_symbol = dvbt2.len_frame - dvbt2.l_fc;
                    if(dvbt2.l_fc) {
                        frame_closing_symbol = true;
                        fc_demod->init(dvbt2, pilot, fq_deinterleaver);
                    }
                    else {
                        frame_closing_symbol = false;
                    }
                    demodulator_init = true;
                    next_symbol_type = SYMBOL_TYPE_P1;

                    continue;

                }
            }
            else {
                ++l1PreErrors;
                if(!demodulator_init) {
                    symbol_size=dvbt2.fft_size+dvbt2.fft_size/4;est_chunk=symbol_size;
                    next_symbol_type = SYMBOL_TYPE_P1;

                    continue;

                }
                else {
                    signal_->reset = true;
                    signal_->p1_reset = true;
                    next_symbol_type = SYMBOL_TYPE_P1;
                    reset();

                    return;

                }
            }
        }

        phase_est_filtered = loop_filter_phase_offset(phase_est * 1.0f, M_PIf32 * 2);
        phase_nco += phase_est_filtered;
//        double step = -8.5e-10;
//        if(old_sample_rate_est - sample_rate_est > 0.0f) {
//            sample_rate_est_filtered -= step;
//            if(resample - sample_rate_est_filtered < min_resample) sample_rate_est_filtered += step;
//        }
//        else if(old_sample_rate_est - sample_rate_est < 0.0f){
//            sample_rate_est_filtered += step;
//            if(resample - sample_rate_est_filtered > max_resample) sample_rate_est_filtered -= step;
//        }
//        old_sample_rate_est = sample_rate_est;
        if(symbol_synchronize){
            sample_rate_est_filtered = loop_filter_sample_rate_offset(sample_rate_est, max_sample_rate_deviation);
        }

        float sample_rate_offset_hz = (sample_rate_est_filtered *(float) SAMPLE_RATE) / M_PI_X_2;
        float frequency_offset_hz = (frequency_est_filtered * (float)SAMPLE_RATE) / M_PI_X_2;

        emit replace_null_indicator(sample_rate_offset_hz, frequency_offset_hz);

    }

}
//----------------------------------------------------------------------------------------------
void dvbt2_demodulator::set_guard_interval()
{
    switch (dvbt2.guard_interval_mode) {
    case GI_1_4:
        dvbt2.guard_interval_size = dvbt2.fft_size / 4;
        break;
    case GI_1_8:
        dvbt2.guard_interval_size = dvbt2.fft_size / 8;
        break;
    case GI_1_16:
        dvbt2.guard_interval_size = dvbt2.fft_size / 16;
        break;
    case GI_1_32:
        dvbt2.guard_interval_size = dvbt2.fft_size / 32;
        break;
    case GI_1_128:
        dvbt2.guard_interval_size = dvbt2.fft_size / 128;
        break;
    case GI_19_128:
        dvbt2.guard_interval_size = (dvbt2.fft_size / 128) * 19;
        break;
    case GI_19_256:
        dvbt2.guard_interval_size = (dvbt2.fft_size / 256) * 19;
        break;
    default:
        break;
    }
    symbol_size = dvbt2.fft_size + dvbt2.guard_interval_size;
    est_chunk = symbol_size;
}
//-------------------------------------------------------------------------------------------
void dvbt2_demodulator::set_guard_interval_by_brute_force ()
{
    static int idx = 0;
    static int c = 0;
    int num = 5;
    if(c == 0) {
        frequency_est_filtered = 0.0f;
        frequency_nco = 0.0f;
    }
    switch (idx) {
    case 0:
        dvbt2.guard_interval_size = dvbt2.fft_size / 32;
        if(c++ > num){
            ++idx;
            c = 0;
        }
        break;
    case 1:
        dvbt2.guard_interval_size = dvbt2.fft_size / 16;
        if(c++ > num){
            ++idx;
            c = 0;
        }
        break;
    case 2:
        dvbt2.guard_interval_size = dvbt2.fft_size / 8;
        if(c++ > num){
            ++idx;
            c = 0;
        }
        break;
    case 3:
        dvbt2.guard_interval_size = dvbt2.fft_size / 4;
        if(c++ > num){
            ++idx;
            c = 0;
        }
        break;
    case 4:
        dvbt2.guard_interval_size = dvbt2.fft_size / 128;
        if(c++ > num){
            ++idx;
            c = 0;
        }
        break;
    case 5:
        dvbt2.guard_interval_size = (dvbt2.fft_size / 128) * 19;
        if(c++ > num){
            ++idx;
            c = 0;
        }
        break;
    case 6:
        dvbt2.guard_interval_size = (dvbt2.fft_size / 256) * 19;
        if(c++ > num){
            idx = 0;
            c = 0;
        }
        break;
    default:
        break;
    }
    symbol_size = dvbt2.fft_size + dvbt2.guard_interval_size;
    est_chunk = symbol_size;
}
//-------------------------------------------------------------------------------------------
void dvbt2_demodulator::stop()
{
    emit finished();
}
//-------------------------------------------------------------------------------------------

void dvbt2_demodulator::discontinuity()
{
    reset();p1_demodulator->restart();direct_resampler->reset();
    idx_buffer_sym=0;est_chunk=0;symbol_size=P1_LEN;symbol_synchronize=false;crc32_l1_pre=false;
    phase_est_filtered=0;cpConfidence=0;
}
