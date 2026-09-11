#include "diagnostics.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
/*
 * HackRF T2 Viewer - HackRF Pro receive backend
 * Copyright (C) 2026 OpenAI
 * GPL-3.0-or-later
 */
#include "rx_hackrf_pro.h"
#include "DSP/complex_rotator.h"

#include <QMutexLocker>
#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>

namespace {
using profile_clock = std::chrono::steady_clock;
inline quint64 profileNs(profile_clock::time_point t0)
{
    return static_cast<quint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        profile_clock::now() - t0).count());
}

QString compactSerial(const read_partid_serialno_t &part)
{
    return QStringLiteral("%1%2%3%4")
        .arg(part.serial_no[0], 8, 16, QLatin1Char('0'))
        .arg(part.serial_no[1], 8, 16, QLatin1Char('0'))
        .arg(part.serial_no[2], 8, 16, QLatin1Char('0'))
        .arg(part.serial_no[3], 8, 16, QLatin1Char('0'))
        .toUpper();
}

int clippedLna(int value)
{
    value = std::max(0, std::min(40, value));
    return value - value % 8;
}

int clippedVga(int value)
{
    value = std::max(0, std::min(62, value));
    return value - value % 2;
}
}

RxHackRfPro::RxHackRfPro(const HackRfSettings &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_channelFrequencyHz = m_settings.frequencyHz;
    m_actualFrequencyHz = m_settings.frequencyHz;
    m_signal = new signal_estimate;

    m_demodulator = new dvbt2_demodulator(0.02f,
                                          static_cast<float>(m_settings.sampleRateHz));
    m_demodulatorThread.setObjectName(QStringLiteral("DVB-T2 demodulator"));
    m_demodulator->moveToThread(&m_demodulatorThread);
    connect(&m_demodulatorThread, &QThread::finished,
            m_demodulator, &QObject::deleteLater);
    connect(m_demodulator,&dvbt2_demodulator::receiver_stage,this,&RxHackRfPro::receiverStage);
    m_spectrumFft=new fast_fourier_transform;
    m_spectrumInput=m_spectrumFft->init(2048);
    m_demodulatorThread.start(QThread::HighPriority);
    QMetaObject::invokeMethod(m_demodulator,[]{},Qt::BlockingQueuedConnection);
}

RxHackRfPro::~RxHackRfPro()
{
    stop(); m_demodulatorThread.quit(); m_demodulatorThread.wait();
    delete m_signal; delete m_spectrumFft;
}
QString RxHackRfPro::errorName(int errorCode)
{
    return QString::fromLatin1(hackrf_error_name(static_cast<hackrf_error>(errorCode)));
}

bool RxHackRfPro::probe(HackRfDeviceInfo &info, QString &errorText)
{
    int result = hackrf_init();
    if (result != HACKRF_SUCCESS) {
        errorText = QStringLiteral("hackrf_init: %1").arg(errorName(result));
        return false;
    }

    hackrf_device *device = nullptr;
    result = hackrf_open(&device);
    if (result != HACKRF_SUCCESS) {
        errorText = QStringLiteral("HackRF не найден или занят: %1").arg(errorName(result));
        hackrf_exit();
        return false;
    }

    uint8_t boardId = BOARD_ID_UNDETECTED;
    uint8_t boardRev = BOARD_REV_UNDETECTED;
    uint16_t usbApi = 0;
    uint32_t platforms = 0;
    uint8_t clockStatus = 0;
    char firmware[256] = {};
    read_partid_serialno_t serial = {};

    hackrf_board_id_read(device, &boardId);
    hackrf_board_rev_read(device, &boardRev);
    hackrf_version_string_read(device, firmware, 255);
    hackrf_usb_api_version_read(device, &usbApi);
    hackrf_supported_platform_read(device, &platforms);
    hackrf_get_clkin_status(device, &clockStatus);
    hackrf_board_partid_serialno_read(device, &serial);

    info.boardName = QString::fromLatin1(
        hackrf_board_id_name(static_cast<hackrf_board_id>(boardId)));
    info.boardRevision = QString::fromLatin1(
        hackrf_board_rev_name(static_cast<hackrf_board_rev>(boardRev)));
    info.serial = compactSerial(serial);
    info.firmware = QString::fromLatin1(firmware);
    info.usbApi = QStringLiteral("%1.%2")
                      .arg((usbApi >> 8) & 0xff, 2, 16, QLatin1Char('0'))
                      .arg(usbApi & 0xff, 2, 16, QLatin1Char('0'))
                      .toUpper();
    info.isPro = boardId == BOARD_ID_PRALINE ||
                 (platforms & HACKRF_PLATFORM_PRALINE) != 0;
    info.clockInputDetected = clockStatus != 0;

    hackrf_close(device);
    hackrf_exit();
    return true;
}

bool RxHackRfPro::reportResult(int result, const QString &operation)
{
    if (result == HACKRF_SUCCESS)
        return true;
    emit radioError(QStringLiteral("%1: %2 (%3)")
                        .arg(operation, errorName(result))
                        .arg(result));
    return false;
}

void RxHackRfPro::start()
{
    if (m_running.load())
        return;

    if(!m_settings.iqFile.isEmpty()) {
        m_replayFile=new QFile(m_settings.iqFile,this);
        if(!m_replayFile->open(QIODevice::ReadOnly)) { emit radioError(m_replayFile->errorString()); emit runningChanged(false); return; }
        resetPipeline(); m_metricsTimer.start(); m_running.store(true);
        m_replayTimer=new QTimer(this);
        connect(m_replayTimer,&QTimer::timeout,this,[this]{
            if(m_pendingBlocks.load()>=16)return;
            const QByteArray bytes=m_replayFile->read(DeviceBlockBytes);
            if(bytes.isEmpty()){m_replayTimer->stop();emit receiverStage(QStringLiteral("I/Q-файл прочитан."));return;}
            processSamples(reinterpret_cast<const uint8_t*>(bytes.constData()),bytes.size());
        });
        m_replayTimer->start(qMax(1,int(1000.0*DeviceBlockBytes/(2*m_settings.sampleRateHz))));
        emit runningChanged(true); return;
    }
    int result = hackrf_init();
    if (!reportResult(result, QStringLiteral("Инициализация libhackrf"))) { emit runningChanged(false);return; }
    m_hackrfInitialized = true;

    result = hackrf_open(&m_device);
    if (!reportResult(result, QStringLiteral("Открытие HackRF"))) {
        hackrf_exit();
        m_hackrfInitialized = false;emit runningChanged(false);
        return;
    }

    if (!reportResult(hackrf_set_sample_rate(m_device, m_settings.sampleRateHz),
                      QStringLiteral("Частота дискретизации")) ||
        !reportResult(hackrf_set_baseband_filter_bandwidth(m_device, m_settings.bandwidthHz),
                      QStringLiteral("Полоса фильтра")) ||
        !reportResult(hackrf_set_freq(m_device, m_settings.frequencyHz),
                      QStringLiteral("Частота настройки"))) {
        stop();
        return;
    }

    setGains(m_settings.lnaGainDb, m_settings.vgaGainDb, m_settings.rfAmp);
    setBiasTee(m_settings.biasTee);
    setNarrowbandFilter(m_settings.narrowbandFilter);
    setClockOut(m_settings.clockOut);
    setConnectorRouting(m_settings.p1Signal, m_settings.p2Signal,
                        m_settings.clockInputSignal);

    resetPipeline();
    m_metricsTimer.start();
    m_running.store(true);
    result = hackrf_start_rx(m_device, &RxHackRfPro::callback, this);
    if (!reportResult(result, QStringLiteral("Запуск потока приёма"))) {
        stop();
        return;
    }

    m_running.store(true);
    emit runningChanged(true);
    emit tunedFrequencyChanged(m_settings.frequencyHz);
}

void RxHackRfPro::stop()
{
    bool running=m_running.exchange(false);
    if(m_replayTimer)m_replayTimer->stop();
    if(m_device && running)hackrf_stop_rx(m_device);
    if(m_demodulatorThread.isRunning())
        QMetaObject::invokeMethod(m_demodulator,[this]{
            clearBlockQueue();
            QMutexLocker lock(&m_captureMutex);m_capture.reset();
        },Qt::BlockingQueuedConnection);
    else {
        clearBlockQueue();
        QMutexLocker lock(&m_captureMutex);m_capture.reset();
    }
    if(m_device){QMutexLocker lock(&m_controlMutex);hackrf_close(m_device);m_device=nullptr;}
    if(m_hackrfInitialized){hackrf_exit();m_hackrfInitialized=false;}
    if(running)emit runningChanged(false);
}
void RxHackRfPro::resetPipeline()
{
    clearBlockQueue();
    *m_signal=signal_estimate{};
    m_actualFrequencyHz=m_channelFrequencyHz;
    m_settleSamples=int(m_settings.sampleRateHz*0.12);
    m_coarsePhase=0.0;m_coarseHz=0.0;
    m_lastSequence=0;m_lastDropCount=m_queueDrops.load();m_haveSequence=false;
    m_profileStatsNs=m_profileIqNs=m_profileCoarseNs=m_profileDemodNs=m_profileSpectrumNs=0;
    m_profileBlocks=0;
    if(!m_stageProfileFile.isOpen() && !Diagnostics::sessionDir().isEmpty()) {
        m_stageProfilePath = Diagnostics::filePath(QStringLiteral("stage-profile.csv"));
        m_stageProfileFile.setFileName(m_stageProfilePath);
        if(m_stageProfileFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            QTextStream out(&m_stageProfileFile);out.setCodec("UTF-8");
            if(m_stageProfileFile.size() == 0) out << "elapsed_ms,input_samples,blocks,usb_msps,processed_msps,backlog,drops,drops_interval,"
                   "frontend_stats_ms,iq_correct_ms,coarse_rotate_ms,demod_outer_ms,spectrum_ms,"
                   "demod_input_rotate_ms,resampler_ms,symbol_acquire_ms,p1_ms,guard_ms,fft_ms,p2_ms,"
                   "data_demod_ms,fc_demod_ms,downstream_data_wait_ms,downstream_control_wait_ms,"
                   "p1_calls,fft_calls,p2_calls,data_calls,fc_calls,p1_matches,p2_attempts,l1_pre,l1_post,l1_pre_errors\n";
            out.flush();
            emit receiverStage(QStringLiteral("Внутренний профиль DSP: %1").arg(m_stageProfilePath));
        }
    }
}
void RxHackRfPro::captureIq(QString path)
{
    if(!m_running.load())return;
    auto capture=std::make_shared<Capture>();capture->path=path;capture->bytes.resize(int(m_settings.sampleRateHz)*6);
    capture->metadata = QJsonObject{
        {QStringLiteral("format"),QStringLiteral("cs8: signed int8 interleaved I,Q")},
        {QStringLiteral("sample_rate_hz"),m_settings.sampleRateHz},
        {QStringLiteral("frequency_hz"),double(m_settings.frequencyHz)},
        {QStringLiteral("bandwidth_hz"),double(m_settings.bandwidthHz)},
        {QStringLiteral("lna_db"),m_settings.lnaGainDb},
        {QStringLiteral("vga_db"),m_settings.vgaGainDb},
        {QStringLiteral("rf_amp"),m_settings.rfAmp},
        {QStringLiteral("version"),QString::fromLatin1(Diagnostics::version())},
        {QStringLiteral("requested_utc"),QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
    };
    {QMutexLocker lock(&m_captureMutex);m_capture=capture;}
    emit receiverStage(QStringLiteral("Запись 3 секунд непрерывного USB I/Q до DSP: %1").arg(path));
}
void RxHackRfPro::setGains(int lnaDb, int vgaDb, bool rfAmp)
{
    lnaDb = clippedLna(lnaDb);
    vgaDb = clippedVga(vgaDb);
    m_settings.lnaGainDb = lnaDb;
    m_settings.vgaGainDb = vgaDb;
    m_settings.rfAmp = rfAmp;
    if (m_device == nullptr)
        return;

    const int r1 = hackrf_set_amp_enable(m_device, rfAmp ? 1 : 0);
    const int r2 = hackrf_set_lna_gain(m_device, static_cast<uint32_t>(lnaDb));
    const int r3 = hackrf_set_vga_gain(m_device, static_cast<uint32_t>(vgaDb));
    if (reportResult(r1, QStringLiteral("RF AMP")) &&
        reportResult(r2, QStringLiteral("IF/LNA")) &&
        reportResult(r3, QStringLiteral("Baseband VGA"))) {
        emit gainChanged(lnaDb, vgaDb, rfAmp);
    }
}

void RxHackRfPro::setBiasTee(bool enabled)
{
    m_settings.biasTee = enabled;
    if (m_device != nullptr &&
        reportResult(hackrf_set_antenna_enable(m_device, enabled ? 1 : 0),
                     QStringLiteral("Питание антенного входа")))
        emit advancedStateChanged(enabled, m_settings.narrowbandFilter,
                                  m_settings.clockOut);
}

void RxHackRfPro::setNarrowbandFilter(bool enabled)
{
    m_settings.narrowbandFilter = enabled;
    if (m_device != nullptr &&
        reportResult(hackrf_set_narrowband_filter(m_device, enabled ? 1 : 0),
                     QStringLiteral("Узкополосный фильтр")))
        emit advancedStateChanged(m_settings.biasTee, enabled, m_settings.clockOut);
}

void RxHackRfPro::setClockOut(bool enabled)
{
    m_settings.clockOut = enabled;
    if (m_device != nullptr &&
        reportResult(hackrf_set_clkout_enable(m_device, enabled ? 1 : 0),
                     QStringLiteral("CLKOUT")))
        emit advancedStateChanged(m_settings.biasTee, m_settings.narrowbandFilter,
                                  enabled);
}

void RxHackRfPro::setConnectorRouting(int p1Signal, int p2Signal,
                                      int clockInputSignal)
{
    m_settings.p1Signal = p1Signal;
    m_settings.p2Signal = p2Signal;
    m_settings.clockInputSignal = clockInputSignal;
    if (m_device == nullptr)
        return;
    reportResult(hackrf_set_p1_ctrl(m_device,
                                    static_cast<p1_ctrl_signal>(p1Signal)),
                 QStringLiteral("Маршрутизация P1"));
    reportResult(hackrf_set_p2_ctrl(m_device,
                                    static_cast<p2_ctrl_signal>(p2Signal)),
                 QStringLiteral("Маршрутизация P2"));
    reportResult(hackrf_set_clkin_ctrl(
                     m_device, static_cast<clkin_ctrl_signal>(clockInputSignal)),
                 QStringLiteral("Источник CLKIN"));
}

int RxHackRfPro::callback(hackrf_transfer *transfer)
{
    if (transfer == nullptr || transfer->rx_ctx == nullptr)
        return 0;
    auto *self = static_cast<RxHackRfPro *>(transfer->rx_ctx);
    if (self->m_running.load() || self->m_device != nullptr)
        self->processSamples(transfer->buffer, transfer->valid_length);
    return 0;
}

void RxHackRfPro::processSamples(const uint8_t *bytes,int count)
{
    if(!m_running.load() || !bytes || count<=0 || (count&1))return;
    m_usbSamples.fetch_add(count/2);const quint64 sequence=m_usbBlocks.fetch_add(1);
    std::shared_ptr<Capture> complete;
    {QMutexLocker lock(&m_captureMutex);if(m_capture){
        int amount=qMin(count,m_capture->bytes.size()-m_capture->used);
        memcpy(m_capture->bytes.data()+m_capture->used,bytes,amount);m_capture->used+=amount;
        if(m_capture->used==m_capture->bytes.size()){complete=m_capture;m_capture.reset();}
    }}
    if(complete)QMetaObject::invokeMethod(this,[this,complete]{
        QSaveFile file(complete->path);
        if(!file.open(QIODevice::WriteOnly) || file.write(complete->bytes)!=complete->bytes.size())emit radioError(QStringLiteral("Ошибка записи I/Q: ")+file.errorString());
        else if(!file.commit()) emit radioError(QStringLiteral("Не удалось сохранить I/Q: ")+file.errorString());
        else {
            complete->metadata.insert(QStringLiteral("bytes"),complete->used);
            complete->metadata.insert(QStringLiteral("complete"),true);
            QSaveFile metadata(complete->path+QStringLiteral(".json"));
            const auto json=QJsonDocument(complete->metadata).toJson();
            if(!metadata.open(QIODevice::WriteOnly) || metadata.write(json)!=json.size() || !metadata.commit())
                emit radioError(QStringLiteral("I/Q сохранён, но запись метаданных не удалась: ")+metadata.errorString());
            emit receiverStage(QStringLiteral("Непрерывный I/Q сохранён: %1").arg(complete->path));
        }
    },Qt::QueuedConnection);
    const QByteArray copy(reinterpret_cast<const char*>(bytes),count);
    bool schedule=false;
    {
        QMutexLocker lock(&m_queueMutex);
        if(m_pendingBlocks.load()>=MaxQueuedBlocks){
            // Do not spend the next second decoding stale I/Q.  1.3.0
            // dropped every NEW USB block while retaining the old backlog;
            // that produced a sequence gap/reset storm and made recovery
            // practically impossible.  Keep the newest block and shed the
            // queued stale backlog in one discontinuity instead.
            const quint64 stale=static_cast<quint64>(m_blockQueue.size());
            m_queueDrops.fetch_add(stale);
            m_blockQueue.clear();
            m_pendingBlocks.store(0);
        }
        m_blockQueue.push_back(QueuedBlock{sequence,copy});
        m_pendingBlocks.fetch_add(1);
        if(!m_dispatchPending){m_dispatchPending=true;schedule=true;}
    }
    if(schedule)QMetaObject::invokeMethod(m_demodulator,[this]{drainBlocks();},Qt::QueuedConnection);
}
void RxHackRfPro::drainBlocks()
{
    // One queued callback drains a batch of USB blocks.  This keeps Qt's
    // event queue from growing to one event per 256 KiB transfer while still
    // preserving sequence numbers and detecting a real discontinuity.
    for(;;){
        QueuedBlock block;
        {
            QMutexLocker lock(&m_queueMutex);
            if(m_blockQueue.empty()){
                m_dispatchPending=false;
                return;
            }
            block=std::move(m_blockQueue.front());
            m_blockQueue.pop_front();
            // pendingBlocks counts QUEUED blocks only.  The block currently
            // being decoded is no longer part of the backlog, which lets the
            // USB producer safely shed stale queued data on overload.
            m_pendingBlocks.fetch_sub(1);
        }
        if(m_running.load()){
            if(m_haveSequence && block.sequence!=m_lastSequence+1){
                m_demodulator->discontinuity();*m_signal=signal_estimate{};
            }
            m_lastSequence=block.sequence;m_haveSequence=true;
            processBlock(block.bytes);
        }
    }
}
void RxHackRfPro::clearBlockQueue()
{
    QMutexLocker lock(&m_queueMutex);
    m_blockQueue.clear();
    m_pendingBlocks.store(0);
    m_dispatchPending=false;
}
void RxHackRfPro::processBlock(const QByteArray &bytes)
{
    QElapsedTimer processing;processing.start();
    const auto *samples=reinterpret_cast<const int8_t*>(bytes.constData());
    const int count=bytes.size()/2;
    const auto t_stats=profile_clock::now();
    for(int i=0;i<bytes.size();++i){
        int v=samples[i];m_peak=std::max(m_peak,std::abs(v));m_sumSquares+=quint64(v*v);
        if(std::abs(v)>=126)++m_clippedComponents;
    }
    m_profileStatsNs += profileNs(t_stats);
    m_metricSamples+=count;
    m_block.resize(count);int gain=0;
    const auto t_iq=profile_clock::now();
    m_iqCorrect.execute(size_t(count),samples,m_block.data(),gain);
    m_profileIqNs += profileNs(t_iq);
    const auto t_coarse=profile_clock::now();
    rotate_samples(m_block.data(),m_block.data(),count,m_coarsePhase,-2*M_PI*m_coarseHz/m_settings.sampleRateHz);
    m_profileCoarseNs += profileNs(t_coarse);
    if(m_settleSamples>0)m_settleSamples-=count;
    else{
        const auto t_demod=profile_clock::now();
        m_demodulator->execute(count,m_block.data(),m_signal);
        m_profileDemodNs += profileNs(t_demod);
        if(m_signal->reset){m_signal->reset=false;m_signal->coarse_freq_offset=0;m_signal->frequency_changed=true;}
        updateFromDemodulator();
    }
    ++m_profileBlocks;
    m_dspNanoseconds+=processing.nsecsElapsed();
    const qint64 elapsed=m_metricsTimer.elapsed();
    if(elapsed<500)return;
    double components=double(m_metricSamples)*2;
    RadioMetrics m;
    m.rmsDbfs=10*std::log10(std::max(1.0e-16,double(m_sumSquares)/components/(128*128)));
    m.peakDbfs=20*std::log10(std::max(1.0e-8,double(m_peak)/128));
    m.clipPercent=100*double(m_clippedComponents)/components;
    m.usbMegaSamplesPerSecond=double(m_usbSamples.exchange(0))/(elapsed*1000.0);
    m.callbackBlocks=m_usbBlocks.load();m.droppedBuffers=m_queueDrops.load();
    m.processedSamples=m_demodulator->processedSamples;m.p1Matches=m_demodulator->p1Matches;
    m.l1PreMatches=m_demodulator->l1PreMatches;m.l1PostMatches=m_demodulator->l1PostMatches;
    m.dspLoadPercent=m_dspNanoseconds/(elapsed*10000.0);
    m.dspMegaSamplesPerSecond=m_metricSamples/(elapsed*1000.0);m.backlog=m_pendingBlocks.load();
    m.droppedLastInterval=m.droppedBuffers-m_lastDropCount;m_lastDropCount=m.droppedBuffers;
    m.p2Attempts=m_demodulator->p2Attempts;m.l1PreErrors=m_demodulator->l1PreErrors;
    m.guardSamples=m_demodulator->measuredGuard;m.cpConfidence=m_demodulator->cpConfidence;m.coarseCorrectionHz=m_coarseHz;
    if(m_stageProfileFile.isOpen() && m_stageProfileFile.size() > 8 * 1024 * 1024) {
        QFile previous(m_stageProfilePath);
        QByteArray header;
        if(previous.open(QIODevice::ReadOnly)) { header = previous.readLine(); previous.close(); }
        m_stageProfileFile.close();
        QFile::remove(m_stageProfilePath + QStringLiteral(".1"));
        QFile::rename(m_stageProfilePath, m_stageProfilePath + QStringLiteral(".1"));
        if(m_stageProfileFile.open(QIODevice::WriteOnly | QIODevice::Text)) m_stageProfileFile.write(header);
    }
    const auto demodProfile=m_demodulator->take_stage_profile();
    emit metricsChanged(m);
    if(count>=2048){
        const auto t_spectrum=profile_clock::now();
        for(int i=0;i<2048;++i)m_spectrumInput[i]=m_block[i]*float(0.5-0.5*std::cos(2*M_PI*i/2047));
        complex *fft=m_spectrumFft->execute();QVector<float> power(2048);
        for(int i=0;i<2048;++i)power[i]=10*std::log10(std::max(1.0e-16f,std::norm(fft[i])/(1024*1024.0f)));
        emit inputSpectrum(power);
        m_profileSpectrumNs += profileNs(t_spectrum);
    }
    if(m_stageProfileFile.isOpen()) {
        const auto ms=[](quint64 ns){return double(ns)/1.0e6;};
        QTextStream out(&m_stageProfileFile);out.setCodec("UTF-8");
        out.setRealNumberNotation(QTextStream::FixedNotation);out.setRealNumberPrecision(3);
        out << elapsed << ',' << m_metricSamples << ',' << m_profileBlocks << ','
            << m.usbMegaSamplesPerSecond << ',' << m.dspMegaSamplesPerSecond << ','
            << m.backlog << ',' << m.droppedBuffers << ',' << m.droppedLastInterval << ','
            << ms(m_profileStatsNs) << ',' << ms(m_profileIqNs) << ',' << ms(m_profileCoarseNs) << ','
            << ms(m_profileDemodNs) << ',' << ms(m_profileSpectrumNs) << ','
            << ms(demodProfile.inputRotateNs) << ',' << ms(demodProfile.resamplerNs) << ','
            << ms(demodProfile.symbolAcquireNs) << ',' << ms(demodProfile.p1Ns) << ','
            << ms(demodProfile.guardNs) << ',' << ms(demodProfile.fftNs) << ','
            << ms(demodProfile.p2Ns) << ',' << ms(demodProfile.dataDemodNs) << ','
            << ms(demodProfile.fcDemodNs) << ',' << ms(demodProfile.downstreamDataNs) << ','
            << ms(demodProfile.downstreamControlNs) << ',' << demodProfile.p1Calls << ','
            << demodProfile.fftCalls << ',' << demodProfile.p2Calls << ',' << demodProfile.dataCalls << ','
            << demodProfile.fcCalls << ',' << m.p1Matches << ',' << m.p2Attempts << ','
            << m.l1PreMatches << ',' << m.l1PostMatches << ',' << m.l1PreErrors << '\n';
        out.flush();
    }
    m_metricSamples=0;m_sumSquares=0;m_peak=0;m_clippedComponents=0;m_dspNanoseconds=0;m_metricsTimer.restart();
    m_profileStatsNs=m_profileIqNs=m_profileCoarseNs=m_profileDemodNs=m_profileSpectrumNs=0;m_profileBlocks=0;
}
void RxHackRfPro::updateFromDemodulator()
{
    if(!m_signal->change_frequency)return;
    m_signal->change_frequency=false;
    if(std::isfinite(m_signal->coarse_freq_offset))m_coarseHz+=m_signal->coarse_freq_offset;
    m_signal->coarse_freq_offset=0;m_signal->frequency_changed=true;m_signal->correct_resample=0;
    emit receiverStage(QStringLiteral("Цифровая коррекция частоты: %1 Гц").arg(m_coarseHz,0,'f',1));
}
