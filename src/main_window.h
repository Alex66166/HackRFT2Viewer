/* HackRF T2 Viewer - main window, GPL-3.0-or-later */
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QThread>
#include <QTimer>

#include "DVB_T2/bb_de_header.h"
#include "qcustomplot.h"
#include "rx_hackrf_pro.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QProgressBar;
class QTabWidget;
class plot;
class QTableWidget;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    friend class GuiSmokeTest;
    void savePreferences();
    void loadPreferences();
    void loadIq();
    void updatePipeline();
    void buildUi();
    QWidget *buildReceiverPage();
    QWidget *buildDiagnosticsPage();
    QWidget *buildTransportPage();
    QWidget *buildAdvancedPage();
    QWidget *makeMetricCard(const QString &title, QLabel **valueLabel);
    void populateChannels();
    void connectUi();
    void connectDemodulator();
    void setRunningUi(bool running);
    void appendLog(const QString &message);
    HackRfSettings settingsFromUi() const;

    void refreshDevice();
    void startReceiver();
    void stopReceiver();
    void tuneSelectedChannel();
    void tuneFrequency(quint64 frequencyHz);
    void changeChannel(int delta);
    void applyLiveGains();
    void updateRadioMetrics(const RadioMetrics &metrics);
    void updateTransportMetrics(const TransportMetrics &metrics);
    void updatePlps(const QStringList &names, const QList<int> &indices);
    void updateServices(const QStringList &names, const QList<int> &ids);
    void updateGainAdvice();
    void browseVlc();
    void launchVlc();
    void toggleRecording();
    void exportDiagnostics();
    void startScan();
    void advanceScan();
    QString findVlc() const;

    quint64 m_session=0;
    QString m_iqPath;
    int m_pendingService=-1;
    QTimer m_scanTimer;
    QElapsedTimer m_statsLog;
    QLabel *m_pipeline=nullptr;
    QSpinBox *m_scanDwell=nullptr;
    QComboBox *m_plpCombo=nullptr;
    QTableWidget *m_found=nullptr;
    RxHackRfPro *m_receiver = nullptr;
    QThread m_radioThread;
    bb_de_header *m_transport = nullptr;
    QProcess *m_vlc = nullptr;
    HackRfDeviceInfo m_deviceInfo;
    RadioMetrics m_radioMetrics;
    TransportMetrics m_transportMetrics;
    double m_snr = -99.0;
    int m_serviceCount = 0;
    int m_plpCount = 0;
    bool m_running = false;
    bool m_recording = false;

    bool m_scanning = false;
    int m_scanIndex = 0;
    double m_scanBestSnr = -99.0;
    QStringList m_scanResults;

    QLabel *m_deviceStatus = nullptr;
    QLabel *m_deviceDetails = nullptr;
    QComboBox *m_channelCombo = nullptr;
    QDoubleSpinBox *m_frequencyMhz = nullptr;
    QComboBox *m_sampleRate = nullptr;
    QComboBox *m_bandwidth = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_scanButton = nullptr;
    QProgressBar *m_scanProgress = nullptr;

    QCheckBox *m_rfAmp = nullptr;
    QSlider *m_lnaGain = nullptr;
    QSlider *m_vgaGain = nullptr;
    QLabel *m_lnaValue = nullptr;
    QLabel *m_vgaValue = nullptr;

    QLabel *m_lockValue = nullptr;
    QLabel *m_snrValue = nullptr;
    QLabel *m_levelValue = nullptr;
    QLabel *m_clipValue = nullptr;
    QLabel *m_usbValue = nullptr;
    QLabel *m_dropValue = nullptr;
    QLabel *m_gainAdvice = nullptr;

    QCustomPlot *m_spectrumPlotWidget = nullptr;
    QCustomPlot *m_constellationPlotWidget = nullptr;
    QCustomPlot *m_p1PlotWidget = nullptr;
    QCustomPlot *m_equalizerPlotWidget = nullptr;
    QCustomPlot *m_frequencyPlotWidget = nullptr;
    plot *m_spectrumPlot = nullptr;
    plot *m_constellationPlot = nullptr;
    plot *m_p1Plot = nullptr;
    plot *m_equalizerPlot = nullptr;
    plot *m_frequencyPlot = nullptr;
    QPlainTextEdit *m_l1Info = nullptr;

    QCheckBox *m_udpEnabled = nullptr;
    QSpinBox *m_udpPort = nullptr;
    QComboBox *m_serviceCombo = nullptr;
    QLineEdit *m_vlcPath = nullptr;
    QPushButton *m_vlcButton = nullptr;
    QPushButton *m_recordButton = nullptr;
    QLabel *m_tsRateValue = nullptr;
    QLabel *m_tsPacketsValue = nullptr;
    QLabel *m_tsErrorValue = nullptr;
    QLabel *m_tsContinuityValue = nullptr;
    QLabel *m_tsSyncValue = nullptr;
    QPlainTextEdit *m_log = nullptr;

    QCheckBox *m_biasTee = nullptr;
    QCheckBox *m_narrowband = nullptr;
    QCheckBox *m_clockOut = nullptr;
    QComboBox *m_p1Routing = nullptr;
    QComboBox *m_p2Routing = nullptr;
    QComboBox *m_clockInput = nullptr;
    QPushButton *m_applyAdvanced = nullptr;
};

#endif // MAIN_WINDOW_H
