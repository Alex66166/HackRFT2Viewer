/*
 * HackRF T2 Viewer - HackRF Pro receive backend
 * Copyright (C) 2026 OpenAI
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#ifndef RX_HACKRF_PRO_H
#define RX_HACKRF_PRO_H

#include <QObject>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#include <vector>
#include <memory>
#include <QMutex>
#include <QThread>


#include <atomic>
#include <deque>
#include <string>

#include "DSP/iq_correct.hh"
#include "DVB_T2/dvbt2_demodulator.h"
#include "third_party/libhackrf/hackrf.h"

struct HackRfDeviceInfo
{
    QString boardName;
    QString boardRevision;
    QString serial;
    QString firmware;
    QString usbApi;
    bool isPro = false;
    bool clockInputDetected = false;
};

struct HackRfSettings
{
    quint64 frequencyHz = 586000000ULL;
    double sampleRateHz = 10000000.0;
    quint32 bandwidthHz = 8000000U;
    int lnaGainDb = 16;
    int vgaGainDb = 16;
    bool rfAmp = false;
    bool biasTee = false;
    bool narrowbandFilter = false;
    bool clockOut = false;
    int p1Signal = P1_SIGNAL_NC;
    int p2Signal = P2_SIGNAL_CLK3;
    int clockInputSignal = CLKIN_SIGNAL_P1;
    QString iqFile;
};

struct RadioMetrics
{
    double rmsDbfs = -120.0;
    double peakDbfs = -120.0;
    double clipPercent = 0.0;
    double usbMegaSamplesPerSecond = 0.0;
    quint64 callbackBlocks = 0;
    quint64 droppedBuffers = 0;
    quint64 processedSamples=0,p1Matches=0,l1PreMatches=0,l1PostMatches=0;
    double dspLoadPercent=0,dspMegaSamplesPerSecond=0,coarseCorrectionHz=0;
    quint64 p2Attempts=0,l1PreErrors=0,droppedLastInterval=0;
    int backlog=0,guardSamples=0;float cpConfidence=0;
};

Q_DECLARE_METATYPE(HackRfDeviceInfo)
Q_DECLARE_METATYPE(HackRfSettings)
Q_DECLARE_METATYPE(RadioMetrics)

class RxHackRfPro final : public QObject
{
    Q_OBJECT
    friend class ReceiverRegressionTest;
    friend class P2AcquisitionTest;

public:
    explicit RxHackRfPro(const HackRfSettings &settings, QObject *parent = nullptr);
    ~RxHackRfPro() override;

    static bool probe(HackRfDeviceInfo &info, QString &errorText);
    static QString errorName(int errorCode);

    dvbt2_demodulator *demodulator() const { return m_demodulator; }

signals:
    void inputSpectrum(QVector<float> powerDb);
    void receiverStage(QString message);
    void radioError(QString message);
    void runningChanged(bool running);
    void tunedFrequencyChanged(quint64 frequencyHz);
    void gainChanged(int lnaDb, int vgaDb, bool rfAmp);
    void metricsChanged(RadioMetrics metrics);
    void advancedStateChanged(bool biasTee, bool narrowband, bool clockOut);


public slots:
    void start();
    void stop();
    void captureIq(QString path);
    void setGains(int lnaDb, int vgaDb, bool rfAmp);
    void setBiasTee(bool enabled);
    void setNarrowbandFilter(bool enabled);
    void setClockOut(bool enabled);
    void setConnectorRouting(int p1Signal, int p2Signal, int clockInputSignal);

private:
    static int callback(hackrf_transfer *transfer);
    void processSamples(const uint8_t *bytes, int byteCount);
    void processBlock(const QByteArray &bytes);
    void drainBlocks();
    void clearBlockQueue();
    void resetPipeline();
    void updateFromDemodulator();
    bool reportResult(int result, const QString &operation);

    HackRfSettings m_settings;
    hackrf_device *m_device = nullptr;
    bool m_hackrfInitialized = false;
    std::atomic_bool m_running{false};

    dvbt2_demodulator *m_demodulator = nullptr;
    QThread m_demodulatorThread;
    signal_estimate *m_signal = nullptr;

    static constexpr int DeviceBlockBytes=262144;
    static constexpr int MaxQueuedBlocks=24;
    struct QueuedBlock {
        quint64 sequence = 0;
        QByteArray bytes;
    };
    std::atomic_int m_pendingBlocks{0};
    std::deque<QueuedBlock> m_blockQueue;
    QMutex m_queueMutex;
    bool m_dispatchPending = false;
    std::atomic_ullong m_usbSamples{0},m_usbBlocks{0},m_queueDrops{0};
    std::vector<complex> m_block;
    qint64 m_dspNanoseconds=0;
    int m_settleSamples=0;
    QFile *m_replayFile=nullptr;
    QFile m_stageProfileFile;
    QString m_stageProfilePath;
    quint64 m_profileStatsNs=0,m_profileIqNs=0,m_profileCoarseNs=0,m_profileDemodNs=0,m_profileSpectrumNs=0;
    quint64 m_profileBlocks=0;
    struct Capture {QString path;QByteArray bytes;int used=0;};
    std::shared_ptr<Capture> m_capture;QMutex m_captureMutex;
    double m_coarsePhase=0,m_coarseHz=0;
    quint64 m_lastSequence=0,m_lastDropCount=0;bool m_haveSequence=false;
    QTimer *m_replayTimer=nullptr;
    qint64 m_captureRemaining=0;
    fast_fourier_transform *m_spectrumFft=nullptr;
    complex *m_spectrumInput=nullptr;
    QMutex m_controlMutex;
    quint64 m_channelFrequencyHz = 0;
    quint64 m_actualFrequencyHz = 0;

    iq_correct<int8_t> m_iqCorrect{7, 0.04f, 0.02f};

    QElapsedTimer m_metricsTimer;
    quint64 m_metricSamples = 0;
    quint64 m_sumSquares = 0;
    int m_peak = 0;
    quint64 m_clippedComponents = 0;
    quint64 m_callbackBlocks = 0;
    quint64 m_droppedBuffers = 0;
};

#endif // RX_HACKRF_PRO_H
