#include <QJsonDocument>
#include <QJsonObject>
#include "diagnostics.h"
/* HackRF T2 Viewer - main window, GPL-3.0-or-later */
#include "main_window.h"

#include "DVB_T2/llr_demapper.h"
#include "plot.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTextStream>
#include <QTime>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <QDebug>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTableWidget>

namespace {
QLabel *sectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

QString formatCount(quint64 value)
{
    return QLocale().toString(value);
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    qRegisterMetaType<HackRfSettings>();
    qRegisterMetaType<RadioMetrics>();
    qRegisterMetaType<TransportMetrics>();
    qRegisterMetaType<QVector<float>>();qRegisterMetaType<QList<int>>();
    buildUi();
    populateChannels();
    connectUi();
    m_vlc = new QProcess(this);
    m_vlcPath->setText(findVlc());
    loadPreferences();m_scanTimer.setSingleShot(true);
    connect(&m_scanTimer,&QTimer::timeout,this,[this]{advanceScan();});
    refreshDevice();
}

MainWindow::~MainWindow()
{
    savePreferences();stopReceiver();
    if(m_vlc->state()!=QProcess::NotRunning){m_vlc->kill();m_vlc->waitForFinished(1000);}
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("HackRF T2 Viewer %1").arg(QString::fromLatin1(Diagnostics::version())));
    setMinimumSize(1180, 740);
    resize(1460, 900);

    const QString style = QStringLiteral(R"(
        QMainWindow, QWidget { background: #10151d; color: #e8edf5; font-family: "Segoe UI"; font-size: 10pt; }
        QGroupBox { border: 1px solid #2b3545; border-radius: 8px; margin-top: 12px; padding: 10px; background: #151c26; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #9fb9df; }
        QPushButton { background: #26364d; border: 1px solid #39506f; border-radius: 6px; padding: 7px 12px; }
        QPushButton:hover { background: #314766; }
        QPushButton:disabled { color: #657082; background: #1a212b; }
        QPushButton#primaryButton { background: #147d68; border-color: #1aa589; font-weight: 600; }
        QPushButton#dangerButton { background: #803747; border-color: #a64a5c; }
        QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox { background: #0d1219; border: 1px solid #344157; border-radius: 5px; padding: 5px; min-height: 22px; }
        QTabWidget::pane { border: 1px solid #293446; border-radius: 7px; top: -1px; }
        QTabBar::tab { background: #171f2b; padding: 9px 15px; margin-right: 2px; }
        QTabBar::tab:selected { background: #26364d; color: #71e0c5; }
        QPlainTextEdit { background: #0b1017; border: 1px solid #2a3545; border-radius: 6px; font-family: Consolas; }
        QSlider::groove:horizontal { height: 5px; background: #2a3545; border-radius: 2px; }
        QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #4fd1b2; }
        QProgressBar { border: 1px solid #344157; border-radius: 5px; text-align: center; background: #0d1219; }
        QProgressBar::chunk { background: #147d68; }
        QLabel#title { font-size: 20pt; font-weight: 700; color: #f4f7fb; }
        QLabel#subtitle { color: #8291a8; }
        QLabel#sectionTitle { font-size: 12pt; font-weight: 600; color: #b9cae2; }
        QLabel#metricValue { font-size: 16pt; font-weight: 700; color: #59ddbd; }
        QLabel#adviceGood { background: #153b34; color: #85efd6; border-radius: 7px; padding: 10px; }
        QLabel#adviceWarn { background: #4b3720; color: #ffd58a; border-radius: 7px; padding: 10px; }
        QLabel#adviceBad { background: #49252d; color: #ff9bae; border-radius: 7px; padding: 10px; }
    )");
    qApp->setStyleSheet(style);

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(14, 12, 14, 14);

    auto *header = new QHBoxLayout;
    auto *titleBox = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("HackRF T2 Viewer"));
    title->setObjectName(QStringLiteral("title"));
    auto *subtitle = new QLabel(QStringLiteral("Программный приём DVB‑T2 • HackRF Pro • VLC 3.0"));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    header->addLayout(titleBox);
    header->addStretch();
    m_deviceStatus = new QLabel(QStringLiteral("● устройство не проверено"));
    m_deviceDetails = new QLabel;
    m_deviceDetails->setObjectName(QStringLiteral("subtitle"));
    auto *deviceBox = new QVBoxLayout;
    deviceBox->addWidget(m_deviceStatus, 0, Qt::AlignRight);
    deviceBox->addWidget(m_deviceDetails, 0, Qt::AlignRight);
    header->addLayout(deviceBox);
    root->addLayout(header);

    auto *body = new QHBoxLayout;
    body->setSpacing(12);

    auto *sidebar = new QWidget;
    sidebar->setFixedWidth(330);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(0, 0, 0, 0);

    auto *deviceGroup = new QGroupBox(QStringLiteral("Приёмник"));
    auto *deviceLayout = new QVBoxLayout(deviceGroup);
    m_refreshButton = new QPushButton(QStringLiteral("Обновить сведения о HackRF"));
    deviceLayout->addWidget(m_refreshButton);
    side->addWidget(deviceGroup);

    auto *channelGroup = new QGroupBox(QStringLiteral("Канал DVB‑T2"));
    auto *channelLayout = new QVBoxLayout(channelGroup);
    m_channelCombo = new QComboBox;
    channelLayout->addWidget(m_channelCombo);
    auto *navigation = new QHBoxLayout;
    auto *previous = new QPushButton(QStringLiteral("◀"));
    previous->setObjectName(QStringLiteral("previousChannel"));
    auto *tune = new QPushButton(QStringLiteral("Настроить"));
    tune->setObjectName(QStringLiteral("tuneChannel"));
    auto *next = new QPushButton(QStringLiteral("▶"));
    next->setObjectName(QStringLiteral("nextChannel"));
    navigation->addWidget(previous);
    navigation->addWidget(tune, 1);
    navigation->addWidget(next);
    channelLayout->addLayout(navigation);
    auto *frequencyLine = new QHBoxLayout;
    frequencyLine->addWidget(new QLabel(QStringLiteral("Частота:")));
    m_frequencyMhz = new QDoubleSpinBox;
    m_frequencyMhz->setRange(0.1, 6000.0);
    m_frequencyMhz->setDecimals(3);
    m_frequencyMhz->setSingleStep(8.0);
    m_frequencyMhz->setSuffix(QStringLiteral(" МГц"));
    frequencyLine->addWidget(m_frequencyMhz, 1);
    channelLayout->addLayout(frequencyLine);
    m_scanButton = new QPushButton(QStringLiteral("Сканировать UHF 21–69"));
    m_scanProgress = new QProgressBar;
    m_scanProgress->setRange(0, 49);
    m_scanProgress->setValue(0);
    m_scanProgress->setVisible(false);
    m_scanDwell=new QSpinBox;m_scanDwell->setRange(5,60);m_scanDwell->setValue(12);
    m_scanDwell->setSuffix(QStringLiteral(" с на частоту"));channelLayout->addWidget(m_scanDwell);
    channelLayout->addWidget(m_scanButton);
    channelLayout->addWidget(m_scanProgress);
    side->addWidget(channelGroup);

    auto *radioGroup = new QGroupBox(QStringLiteral("Радиотракт"));
    auto *radioForm = new QFormLayout(radioGroup);
    m_sampleRate = new QComboBox;
    m_sampleRate->addItem(QStringLiteral("10 MS/s (рекомендуется)"), 10000000.0);
    m_sampleRate->addItem(QStringLiteral("12.5 MS/s"), 12500000.0);
    m_sampleRate->addItem(QStringLiteral("16 MS/s"), 16000000.0);
    m_sampleRate->addItem(QStringLiteral("20 MS/s"), 20000000.0);
    m_sampleRate->addItem(QStringLiteral("8 MS/s (экономия CPU)"), 8000000.0);
    m_bandwidth = new QComboBox;
    m_bandwidth->addItem(QStringLiteral("8 МГц"), 8000000);
    m_bandwidth->addItem(QStringLiteral("9 МГц"), 9000000);
    m_bandwidth->addItem(QStringLiteral("10 МГц"), 10000000);
    radioForm->addRow(QStringLiteral("I/Q поток:"), m_sampleRate);
    radioForm->addRow(QStringLiteral("Фильтр:"), m_bandwidth);
    side->addWidget(radioGroup);

    auto *gainGroup = new QGroupBox(QStringLiteral("Ручное усиление — меняется на ходу"));
    auto *gainLayout = new QGridLayout(gainGroup);
    m_rfAmp = new QCheckBox(QStringLiteral("RF AMP  +≈11 дБ"));
    gainLayout->addWidget(m_rfAmp, 0, 0, 1, 3);
    gainLayout->addWidget(new QLabel(QStringLiteral("IF/LNA")), 1, 0);
    m_lnaGain = new QSlider(Qt::Horizontal);
    m_lnaGain->setRange(0, 5);
    m_lnaGain->setValue(2);
    m_lnaValue = new QLabel(QStringLiteral("16 дБ"));
    gainLayout->addWidget(m_lnaGain, 1, 1);
    gainLayout->addWidget(m_lnaValue, 1, 2);
    gainLayout->addWidget(new QLabel(QStringLiteral("VGA")), 2, 0);
    m_vgaGain = new QSlider(Qt::Horizontal);
    m_vgaGain->setRange(0, 31);
    m_vgaGain->setValue(8);
    m_vgaValue = new QLabel(QStringLiteral("16 дБ"));
    gainLayout->addWidget(m_vgaGain, 2, 1);
    gainLayout->addWidget(m_vgaValue, 2, 2);
    side->addWidget(gainGroup);

    auto *controls = new QHBoxLayout;
    m_startButton = new QPushButton(QStringLiteral("▶ Начать приём"));
    m_startButton->setObjectName(QStringLiteral("primaryButton"));
    m_stopButton = new QPushButton(QStringLiteral("■ Стоп"));
    m_stopButton->setObjectName(QStringLiteral("dangerButton"));
    m_stopButton->setEnabled(false);
    controls->addWidget(m_startButton, 1);
    controls->addWidget(m_stopButton);
    side->addLayout(controls);
    side->addStretch();
    body->addWidget(sidebar);

    auto *tabs = new QTabWidget;
    tabs->addTab(buildReceiverPage(), QStringLiteral("Приём"));
    tabs->addTab(buildDiagnosticsPage(), QStringLiteral("Диагностика"));
    tabs->addTab(buildTransportPage(), QStringLiteral("Каналы и VLC"));
    tabs->addTab(buildAdvancedPage(), QStringLiteral("HackRF Pro"));
    body->addWidget(tabs, 1);
    root->addLayout(body, 1);
    setCentralWidget(central);
}

QWidget *MainWindow::makeMetricCard(const QString &title, QLabel **valueLabel)
{
    auto *card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(QStringLiteral("QFrame {background:#151c26;border:1px solid #2b3545;border-radius:8px;}"));
    auto *layout = new QVBoxLayout(card);
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("subtitle"));
    auto *value = new QLabel(QStringLiteral("—"));
    value->setObjectName(QStringLiteral("metricValue"));
    layout->addWidget(titleLabel);
    layout->addWidget(value);
    *valueLabel = value;
    return card;
}

QWidget *MainWindow::buildReceiverPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *metrics = new QHBoxLayout;
    metrics->addWidget(makeMetricCard(QStringLiteral("Синхронизация"), &m_lockValue));
    metrics->addWidget(makeMetricCard(QStringLiteral("SNR"), &m_snrValue));
    m_snrValue->setToolTip(QStringLiteral("Оценка по ближайшим точкам созвездия может быть завышена при большом числе ошибок. Приём подтверждают корректные BCH-блоки и транспортный поток."));
    metrics->addWidget(makeMetricCard(QStringLiteral("Уровень"), &m_levelValue));
    metrics->addWidget(makeMetricCard(QStringLiteral("Перегрузка"), &m_clipValue));
    metrics->addWidget(makeMetricCard(QStringLiteral("USB поток"), &m_usbValue));
    metrics->addWidget(makeMetricCard(QStringLiteral("Потери буфера"), &m_dropValue));
    layout->addLayout(metrics);

    m_pipeline=new QLabel;m_pipeline->setWordWrap(true);layout->addWidget(m_pipeline);
    auto *plots = new QHBoxLayout;
    m_spectrumPlotWidget = new QCustomPlot;
    m_constellationPlotWidget = new QCustomPlot;
    m_spectrumPlotWidget->setMinimumHeight(360);
    m_constellationPlotWidget->setMinimumSize(360, 360);
    plots->addWidget(m_spectrumPlotWidget, 3);
    plots->addWidget(m_constellationPlotWidget, 2);
    layout->addLayout(plots, 1);
    m_spectrumPlot = new plot(m_spectrumPlotWidget, type_spectrograph,
                              QStringLiteral("Спектр входного I/Q"), this);
    m_constellationPlot = new plot(m_constellationPlotWidget, type_constelation,
                                   QStringLiteral("Созвездие L1-pre / выбранного PLP"), this);

    m_gainAdvice = new QLabel(QStringLiteral("Начните приём. Стартовая точка: AMP выкл., LNA 16 дБ, VGA 16 дБ."));
    m_gainAdvice->setObjectName(QStringLiteral("adviceGood"));
    m_gainAdvice->setWordWrap(true);
    layout->addWidget(m_gainAdvice);
    return page;
}

QWidget *MainWindow::buildDiagnosticsPage()
{
    auto *page = new QWidget;
    auto *layout = new QGridLayout(page);
    m_p1PlotWidget = new QCustomPlot;
    m_equalizerPlotWidget = new QCustomPlot;
    m_frequencyPlotWidget = new QCustomPlot;
    m_l1Info = new QPlainTextEdit;
    m_l1Info->setReadOnly(true);
    m_p1PlotWidget->setMinimumHeight(260);
    m_equalizerPlotWidget->setMinimumHeight(260);
    m_frequencyPlotWidget->setMinimumHeight(220);
    layout->addWidget(m_p1PlotWidget, 0, 0);
    layout->addWidget(m_equalizerPlotWidget, 0, 1);
    layout->addWidget(m_frequencyPlotWidget, 1, 0);
    layout->addWidget(m_l1Info, 1, 1);
    layout->setRowStretch(0, 1);
    layout->setRowStretch(1, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    m_p1Plot = new plot(m_p1PlotWidget, type_oscilloscope,
                        QStringLiteral("Корреляция P1"), this);
    m_equalizerPlot = new plot(m_equalizerPlotWidget, type_oscilloscope_2,
                               QStringLiteral("Оценка эквалайзера"), this);
    m_frequencyPlot = new plot(m_frequencyPlotWidget, type_null_indicator,
                               QStringLiteral("Коррекция частоты и sample rate"), this);
    return page;
}

QWidget *MainWindow::buildTransportPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(sectionTitle(QStringLiteral("MPEG Transport Stream")));

    auto *metrics = new QHBoxLayout;
    metrics->addWidget(makeMetricCard(QStringLiteral("Битрейт"), &m_tsRateValue));
    metrics->addWidget(makeMetricCard(QStringLiteral("TS-пакеты"), &m_tsPacketsValue));
    metrics->addWidget(makeMetricCard(QStringLiteral("TEI ошибки"), &m_tsErrorValue));
    metrics->addWidget(makeMetricCard(QStringLiteral("Continuity"), &m_tsContinuityValue));
    metrics->addWidget(makeMetricCard(QStringLiteral("Потеря sync"), &m_tsSyncValue));
    layout->addLayout(metrics);

    auto *outputGroup = new QGroupBox(QStringLiteral("VLC 3.0"));
    auto *form = new QGridLayout(outputGroup);
    m_udpEnabled = new QCheckBox(QStringLiteral("Выводить поток в UDP"));
    m_udpEnabled->setChecked(true);
    m_udpPort = new QSpinBox;
    m_udpPort->setRange(1024, 65535);
    m_udpPort->setValue(7654);
    m_plpCombo=new QComboBox;m_plpCombo->addItem(QStringLiteral("PLP: автоматически"),-1);layout->addWidget(m_plpCombo);
    m_serviceCombo = new QComboBox;
    m_serviceCombo->addItem(QStringLiteral("Весь мультиплекс"), -1);
    m_vlcPath = new QLineEdit;
    auto *browse = new QPushButton(QStringLiteral("Обзор…"));
    browse->setObjectName(QStringLiteral("browseVlc"));
    m_vlcButton = new QPushButton(QStringLiteral("Открыть в VLC 3.0"));
    m_vlcButton->setObjectName(QStringLiteral("primaryButton"));
    form->addWidget(m_udpEnabled, 0, 0);
    form->addWidget(new QLabel(QStringLiteral("Порт:")), 0, 1);
    form->addWidget(m_udpPort, 0, 2);
    form->addWidget(new QLabel(QStringLiteral("Телеканал:")), 1, 0);
    form->addWidget(m_serviceCombo, 1, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("vlc.exe:")), 2, 0);
    form->addWidget(m_vlcPath, 2, 1);
    form->addWidget(browse, 2, 2);
    form->addWidget(m_vlcButton, 3, 0, 1, 3);
    layout->addWidget(outputGroup);

    auto *recordGroup = new QGroupBox(QStringLiteral("Запись без перекодирования"));
    auto *recordLayout = new QHBoxLayout(recordGroup);
    m_recordButton = new QPushButton(QStringLiteral("● Начать запись .ts"));
    recordLayout->addWidget(m_recordButton);
    recordLayout->addStretch();
    layout->addWidget(recordGroup);

    m_found=new QTableWidget(0,3);
    m_found->setHorizontalHeaderLabels({QStringLiteral("МГц"),QStringLiteral("SID"),QStringLiteral("Телеканал — двойной щелчок для просмотра")});
    m_found->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    m_found->setEditTriggers(QAbstractItemView::NoEditTriggers);m_found->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_found->setMaximumHeight(180);layout->addWidget(m_found);
    layout->addWidget(sectionTitle(QStringLiteral("Журнал приёма")));
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(3000);
    layout->addWidget(m_log, 1);
    return page;
}

QWidget *MainWindow::buildAdvancedPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *warning = new QLabel(QStringLiteral(
        "Эти параметры относятся к HackRF Pro. Для обычного DVB‑T2 оставьте значения по умолчанию. "
        "Питание антенного входа включайте только для совместимой активной антенны или предусилителя."));
    warning->setObjectName(QStringLiteral("adviceWarn"));
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto *rfGroup = new QGroupBox(QStringLiteral("Дополнительные функции приёмника"));
    auto *rfLayout = new QVBoxLayout(rfGroup);
    m_biasTee = new QCheckBox(QStringLiteral("Питание RF-порта 3,3 В / до 50 мА (bias tee)"));
    m_narrowband = new QCheckBox(QStringLiteral("Узкополосный фильтр HackRF Pro — для DVB‑T2 обычно выключен"));
    m_clockOut = new QCheckBox(QStringLiteral("Выдавать опорную частоту на CLKOUT"));
    rfLayout->addWidget(m_biasTee);
    rfLayout->addWidget(m_narrowband);
    rfLayout->addWidget(m_clockOut);
    layout->addWidget(rfGroup);

    auto *routing = new QGroupBox(QStringLiteral("Маршрутизация SMA/тактовых сигналов"));
    auto *routingForm = new QFormLayout(routing);
    m_p1Routing = new QComboBox;
    m_p1Routing->addItem(QStringLiteral("NC (по умолчанию)"), P1_SIGNAL_NC);
    m_p1Routing->addItem(QStringLiteral("Trigger input"), P1_SIGNAL_TRIGGER_IN);
    m_p1Routing->addItem(QStringLiteral("Trigger output"), P1_SIGNAL_TRIGGER_OUT);
    m_p1Routing->addItem(QStringLiteral("CLKIN"), P1_SIGNAL_CLKIN);
    m_p1Routing->addItem(QStringLiteral("AUX CLK1"), P1_SIGNAL_AUX_CLK1);
    m_p1Routing->addItem(QStringLiteral("AUX CLK2"), P1_SIGNAL_AUX_CLK2);
    m_p1Routing->addItem(QStringLiteral("P2.2 CLKIN"), P1_SIGNAL_P22_CLKIN);
    m_p1Routing->addItem(QStringLiteral("P2.5"), P1_SIGNAL_P2_5);
    m_p2Routing = new QComboBox;
    m_p2Routing->addItem(QStringLiteral("CLK3 (по умолчанию)"), P2_SIGNAL_CLK3);
    m_p2Routing->addItem(QStringLiteral("Trigger input"), P2_SIGNAL_TRIGGER_IN);
    m_p2Routing->addItem(QStringLiteral("Trigger output"), P2_SIGNAL_TRIGGER_OUT);
    m_clockInput = new QComboBox;
    m_clockInput->addItem(QStringLiteral("P1"), CLKIN_SIGNAL_P1);
    m_clockInput->addItem(QStringLiteral("P2.2"), CLKIN_SIGNAL_P22);
    routingForm->addRow(QStringLiteral("Разъём P1:"), m_p1Routing);
    routingForm->addRow(QStringLiteral("Разъём P2:"), m_p2Routing);
    routingForm->addRow(QStringLiteral("Источник CLKIN:"), m_clockInput);
    m_applyAdvanced = new QPushButton(QStringLiteral("Применить расширенные настройки"));
    routingForm->addRow(m_applyAdvanced);
    layout->addWidget(routing);

    auto *exportButton = new QPushButton(QStringLiteral("Сохранить диагностический отчёт…"));
    exportButton->setObjectName(QStringLiteral("exportDiagnostics"));
    layout->addWidget(exportButton);
    auto *capture=new QPushButton(QStringLiteral("Записать 3 секунды исходного I/Q…"));
    connect(capture,&QPushButton::clicked,this,[this]{
        if(!m_receiver)return;
        const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("I/Q: signed int8 I,Q"),QStringLiteral("HackRF_%1MHz_%2MSps.cs8").arg(m_frequencyMhz->value(),0,'f',3).arg(m_sampleRate->currentData().toDouble()/1e6),QStringLiteral("I/Q (*.cs8)"));
        if(!path.isEmpty())QMetaObject::invokeMethod(m_receiver,"captureIq",Qt::QueuedConnection,Q_ARG(QString,path));
    });layout->addWidget(capture);
    auto *load=new QPushButton(QStringLiteral("Открыть I/Q-файл для диагностики…"));
    connect(load,&QPushButton::clicked,this,[this]{loadIq();});layout->addWidget(load);
    auto *hardware=new QPushButton(QStringLiteral("Вернуться к приёму с HackRF"));
    connect(hardware,&QPushButton::clicked,this,[this]{stopReceiver();m_iqPath.clear();refreshDevice();});layout->addWidget(hardware);
    layout->addStretch();
    return page;
}

void MainWindow::populateChannels()
{
    m_channelCombo->clear();
    for(int channel = 21; channel <= 69; ++channel) {
        const int frequencyMhz = 474 + (channel - 21) * 8;
        m_channelCombo->addItem(QStringLiteral("ТВК %1 — %2 МГц")
                                    .arg(channel).arg(frequencyMhz),
                                static_cast<qulonglong>(frequencyMhz) * 1000000ULL);
    }
    const int defaultIndex = m_channelCombo->findData(586000000ULL);
    m_channelCombo->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    m_frequencyMhz->setValue(m_channelCombo->currentData().toULongLong() / 1.0e6);
}

void MainWindow::connectUi()
{
    connect(m_serviceCombo,qOverload<int>(&QComboBox::activated),this,[this](int){if(m_vlc->state()!=QProcess::NotRunning)launchVlc();});
    connect(m_plpCombo,qOverload<int>(&QComboBox::activated),this,[this](int){
        if(m_transport)QMetaObject::invokeMethod(m_transport,"select_plp",Qt::QueuedConnection,Q_ARG(int,m_plpCombo->currentData().toInt()));
        m_serviceCombo->clear();
        if(m_plpCount>0 && m_serviceCount==0)m_serviceCombo->addItem(QStringLiteral("PLP найден — ждём TS/PAT/SDT"),-2);
        else m_serviceCombo->addItem(QStringLiteral("Весь мультиплекс"),-1);
    });
    connect(m_found,&QTableWidget::cellDoubleClicked,this,[this](int row,int){
        quint64 frequency=m_found->item(row,0)->data(Qt::UserRole).toULongLong();int sid=m_found->item(row,1)->text().toInt();
        tuneFrequency(frequency);m_pendingService=sid;if(!m_receiver)startReceiver();launchVlc();
    });
    connect(m_refreshButton, &QPushButton::clicked, this, [this]{ refreshDevice(); });
    connect(m_startButton, &QPushButton::clicked, this, [this]{ startReceiver(); });
    connect(m_stopButton, &QPushButton::clicked, this, [this]{ stopReceiver(); });
    connect(m_scanButton, &QPushButton::clicked, this, [this]{ startScan(); });
    connect(m_channelCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int){
                const quint64 hz = m_channelCombo->currentData().toULongLong();
                if(hz > 0) m_frequencyMhz->setValue(hz / 1.0e6);
            });

    auto *previous = findChild<QPushButton*>(QStringLiteral("previousChannel"));
    auto *next = findChild<QPushButton*>(QStringLiteral("nextChannel"));
    auto *tune = findChild<QPushButton*>(QStringLiteral("tuneChannel"));
    connect(previous, &QPushButton::clicked, this, [this]{ changeChannel(-1); });
    connect(next, &QPushButton::clicked, this, [this]{ changeChannel(1); });
    connect(tune, &QPushButton::clicked, this, [this]{ tuneSelectedChannel(); });

    connect(m_lnaGain, &QSlider::valueChanged, this, [this](int step){
        m_lnaValue->setText(QStringLiteral("%1 дБ").arg(step * 8));
        applyLiveGains();
    });
    connect(m_vgaGain, &QSlider::valueChanged, this, [this](int step){
        m_vgaValue->setText(QStringLiteral("%1 дБ").arg(step * 2));
        applyLiveGains();
    });
    connect(m_rfAmp, &QCheckBox::toggled, this, [this]{ applyLiveGains(); });

    connect(m_udpEnabled, &QCheckBox::toggled, this, [this](bool enabled){
        if(m_transport)
            QMetaObject::invokeMethod(m_transport, "set_network_output", Qt::QueuedConnection,
                                      Q_ARG(bool, enabled), Q_ARG(int, m_udpPort->value()));
    });
    connect(m_udpPort, qOverload<int>(&QSpinBox::valueChanged), this, [this](int port){
        if(m_transport)
            QMetaObject::invokeMethod(m_transport, "set_network_output", Qt::QueuedConnection,
                                      Q_ARG(bool, m_udpEnabled->isChecked()), Q_ARG(int, port));
    });

    connect(findChild<QPushButton*>(QStringLiteral("browseVlc")), &QPushButton::clicked,
            this, [this]{ browseVlc(); });
    connect(m_vlcButton, &QPushButton::clicked, this, [this]{ launchVlc(); });
    connect(m_recordButton, &QPushButton::clicked, this, [this]{ toggleRecording(); });
    connect(findChild<QPushButton*>(QStringLiteral("exportDiagnostics")), &QPushButton::clicked,
            this, [this]{ exportDiagnostics(); });

    connect(m_applyAdvanced, &QPushButton::clicked, this, [this]{
        if(!m_receiver) {
            QMessageBox::information(this, QStringLiteral("HackRF Pro"),
                                     QStringLiteral("Сначала запустите приём."));
            return;
        }
        QMetaObject::invokeMethod(m_receiver, "setBiasTee", Qt::QueuedConnection,
                                  Q_ARG(bool, m_biasTee->isChecked()));
        QMetaObject::invokeMethod(m_receiver, "setNarrowbandFilter", Qt::QueuedConnection,
                                  Q_ARG(bool, m_narrowband->isChecked()));
        QMetaObject::invokeMethod(m_receiver, "setClockOut", Qt::QueuedConnection,
                                  Q_ARG(bool, m_clockOut->isChecked()));
        QMetaObject::invokeMethod(m_receiver, "setConnectorRouting", Qt::QueuedConnection,
                                  Q_ARG(int, m_p1Routing->currentData().toInt()),
                                  Q_ARG(int, m_p2Routing->currentData().toInt()),
                                  Q_ARG(int, m_clockInput->currentData().toInt()));
        appendLog(QStringLiteral("Расширенные настройки HackRF Pro отправлены устройству."));
    });
}

HackRfSettings MainWindow::settingsFromUi() const
{
    HackRfSettings settings;
    settings.iqFile=m_iqPath;
    settings.frequencyHz = static_cast<quint64>(std::llround(m_frequencyMhz->value() * 1.0e6));
    settings.sampleRateHz = m_sampleRate->currentData().toDouble();
    settings.bandwidthHz = m_bandwidth->currentData().toUInt();
    settings.lnaGainDb = m_lnaGain->value() * 8;
    settings.vgaGainDb = m_vgaGain->value() * 2;
    settings.rfAmp = m_rfAmp->isChecked();
    settings.biasTee = m_biasTee->isChecked();
    settings.narrowbandFilter = m_narrowband->isChecked();
    settings.clockOut = m_clockOut->isChecked();
    settings.p1Signal = m_p1Routing->currentData().toInt();
    settings.p2Signal = m_p2Routing->currentData().toInt();
    settings.clockInputSignal = m_clockInput->currentData().toInt();
    return settings;
}

void MainWindow::refreshDevice()
{
    if(m_running) return;
    QString error;
    HackRfDeviceInfo info;
    if(RxHackRfPro::probe(info, error)) {
        m_deviceInfo = info;
        m_deviceStatus->setText(info.isPro
            ? QStringLiteral("● HackRF Pro готов")
            : QStringLiteral("● Найден HackRF (не Pro)"));
        m_deviceStatus->setStyleSheet(QStringLiteral("color:#59ddbd;font-weight:600;"));
        m_deviceDetails->setText(QStringLiteral("%1 • FW %2 • USB API %3")
                                     .arg(info.boardRevision, info.firmware, info.usbApi));
        appendLog(QStringLiteral("Найден %1, ревизия %2, серийный номер %3, FW %4.")
                      .arg(info.boardName, info.boardRevision, info.serial, info.firmware));
        if(!info.isPro)
            appendLog(QStringLiteral("Предупреждение: Pro-функции будут недоступны."));
    } else {
        m_deviceStatus->setText(QStringLiteral("● HackRF не найден"));
        m_deviceStatus->setStyleSheet(QStringLiteral("color:#ff869d;font-weight:600;"));
        m_deviceDetails->setText(error);
        appendLog(error);
    }
}

void MainWindow::startReceiver()
{
    if(m_receiver || m_radioThread.isRunning())return;
    const auto session=++m_session;m_radioMetrics={};m_transportMetrics={};m_snr=-99;m_serviceCount=0;m_plpCount=0;m_statsLog.start();updatePipeline();
    m_lockValue->setText(QStringLiteral("ПОИСК P1"));m_snrValue->setText(QStringLiteral("—"));m_l1Info->clear();
    m_startButton->setEnabled(false);m_stopButton->setEnabled(true);m_sampleRate->setEnabled(false);m_bandwidth->setEnabled(false);
    m_receiver=new RxHackRfPro(settingsFromUi());m_receiver->moveToThread(&m_radioThread);
    connect(&m_radioThread,&QThread::started,m_receiver,&RxHackRfPro::start);
    connect(m_receiver,&RxHackRfPro::radioError,this,[this,session](QString text){if(session==m_session){appendLog(QStringLiteral("ОШИБКА: ")+text);m_stopButton->setEnabled(true);}});
    connect(m_receiver,&RxHackRfPro::receiverStage,this,[this,session](QString text){if(session==m_session)appendLog(text);});
    connect(m_receiver,&RxHackRfPro::runningChanged,this,[this,session](bool running){
        if(session!=m_session)return;m_running=running;setRunningUi(running);
        appendLog(running?QStringLiteral("Приём запущен."):QStringLiteral("Приём остановлен."));
        if(running && m_scanning)m_scanTimer.start(m_scanDwell->value()*1000);
        if(!running && m_receiver)QTimer::singleShot(0,this,[this,session]{if(session==m_session)stopReceiver();});
    });
    connect(m_receiver,&RxHackRfPro::tunedFrequencyChanged,this,[this,session](quint64 hz){if(session==m_session)m_deviceDetails->setText(QStringLiteral("Настройка %1 МГц • FW %2").arg(hz/1e6,0,'f',3).arg(m_deviceInfo.firmware));});
    connect(m_receiver,&RxHackRfPro::metricsChanged,this,[this,session](RadioMetrics m){if(session==m_session)updateRadioMetrics(m);});
    double rate=m_sampleRate->currentData().toDouble()/1e6,center=m_frequencyMhz->value();
    connect(m_receiver,&RxHackRfPro::inputSpectrum,this,[this,session,rate,center](QVector<float> power){
        if(session!=m_session || power.isEmpty())return;QVector<double> x(power.size()),y(power.size());
        for(int i=0;i<power.size();++i){x[i]=center+rate*(double(i)/power.size()-.5);y[i]=power[i];}
        if(!m_spectrumPlotWidget->graphCount())m_spectrumPlotWidget->addGraph();
        m_spectrumPlotWidget->graph(0)->setData(x,y);m_spectrumPlotWidget->xAxis->setRange(center-rate/2,center+rate/2);m_spectrumPlotWidget->yAxis->setRange(-110,5);
        m_spectrumPlotWidget->xAxis->setLabel(QStringLiteral("МГц"));m_spectrumPlotWidget->yAxis->setLabel(QStringLiteral("dBFS / FFT bin"));m_spectrumPlotWidget->replot(QCustomPlot::rpQueuedReplot);
    });
    connectDemodulator();m_radioThread.start(QThread::HighPriority);
}

void MainWindow::connectDemodulator()
{
    const auto session=m_session;auto *demod=m_receiver->demodulator();
    // Copy borrowed DSP pointers on their producer thread before queuing a plot update.
    auto snapshot=[this,session](plot *target,auto method){
        auto timer=std::make_shared<QElapsedTimer>();
        return [this,session,target,method,timer](int count,complex *data){
            if(count<=0 || !data || (timer->isValid() && timer->elapsed()<250))return;
            timer->start();QVector<complex> copy(count);std::copy(data,data+count,copy.begin());
            QMetaObject::invokeMethod(this,[this,session,target,method,copy]()mutable{if(session==m_session)(target->*method)(copy.size(),copy.data());},Qt::QueuedConnection);
        };
    };
    connect(demod->p1_demodulator,&p1_symbol::replace_oscilloscope,this,snapshot(m_p1Plot,&plot::replace_oscilloscope),Qt::DirectConnection);
    connect(demod->p2_demodulator,&p2_symbol::replace_oscilloscope,this,snapshot(m_equalizerPlot,&plot::replace_oscilloscope),Qt::DirectConnection);
    connect(demod->data_demodulator,&data_symbol::replace_oscilloscope,this,snapshot(m_equalizerPlot,&plot::replace_oscilloscope),Qt::DirectConnection);
    auto havePlp=std::make_shared<std::atomic_bool>(false);
    auto prePlot=snapshot(m_constellationPlot,&plot::replace_constelation);
    connect(demod->p2_demodulator,&p2_symbol::replace_constelation,this,[havePlp,prePlot](int n,complex* p){if(!havePlp->load())prePlot(n,p);},Qt::DirectConnection);
    auto plpPlot=snapshot(m_constellationPlot,&plot::replace_constelation);
    connect(demod->deinterleaver,&time_deinterleaver::replace_constelation,this,[havePlp,plpPlot](int n,complex* p){havePlp->store(true);plpPlot(n,p);},Qt::DirectConnection);
    connect(demod,&dvbt2_demodulator::replace_null_indicator,this,[this,session](float a,float b){if(session==m_session)m_frequencyPlot->replace_null_indicator(a,b);});
    connect(demod->deinterleaver->qam,&llr_demapper::signal_noise_ratio,this,[this,session](float snr){
        if(session!=m_session)return;m_snr=snr;m_snrValue->setText(QStringLiteral("%1 дБ").arg(snr,0,'f',1));m_lockValue->setText(QStringLiteral("DEMOD"));
        if(m_scanning)m_scanBestSnr=qMax(m_scanBestSnr,double(snr));updateGainAdvice();
    });
    connect(demod->p2_demodulator,&p2_symbol::view_l1_presignalling,this,[this,session](QString info){if(session==m_session)m_l1Info->setPlainText(info);});
    connect(demod->p2_demodulator,&p2_symbol::view_l1_postsignalling,this,[this,session](QString info){if(session==m_session)m_l1Info->appendPlainText(info);});
    connect(demod->p2_demodulator,&p2_symbol::plps_changed,this,[this,session](QStringList n,QList<int> i){
        if(session==m_session)updatePlps(n,i);
    });
    m_transport=demod->deinterleaver->qam->decoder->decoder->deheader;
    connect(m_transport,&bb_de_header::transport_metrics,this,[this,session](TransportMetrics m){if(session==m_session)updateTransportMetrics(m);});
    connect(m_transport,&bb_de_header::services_changed,this,[this,session](QStringList n,QList<int> i){if(session==m_session)updateServices(n,i);});
    connect(m_transport,&bb_de_header::ts_stage,this,[this,session](QString text){if(session==m_session)appendLog(text);});
    connect(m_transport,&bb_de_header::plps_changed,this,[this,session](QStringList n,QList<int> i){
        if(session==m_session)updatePlps(n,i);
    });
    QMetaObject::invokeMethod(m_transport,"set_network_output",Qt::QueuedConnection,Q_ARG(bool,m_udpEnabled->isChecked()),Q_ARG(int,m_udpPort->value()));
}

void MainWindow::stopReceiver()
{
    ++m_session;m_scanTimer.stop();m_scanning=false;m_scanProgress->setVisible(false);m_scanButton->setText(QStringLiteral("Сканировать UHF 21–69"));
    if(m_receiver){auto *receiver=m_receiver;QMetaObject::invokeMethod(receiver,[receiver]{receiver->stop();delete receiver;},Qt::BlockingQueuedConnection);m_radioThread.quit();m_radioThread.wait();}
    m_receiver=nullptr;m_transport=nullptr;m_running=false;m_recording=false;m_plpCount=0;m_recordButton->setText(QStringLiteral("● Начать запись .ts"));m_lockValue->setText(QStringLiteral("СТОП"));setRunningUi(false);
}

void MainWindow::setRunningUi(bool running)
{
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    m_refreshButton->setEnabled(!running);
    m_sampleRate->setEnabled(!running);
    m_bandwidth->setEnabled(!running);
    m_deviceStatus->setText(running ? QStringLiteral("● приём работает")
                                    : QStringLiteral("● приём остановлен"));
    m_deviceStatus->setStyleSheet(running
        ? QStringLiteral("color:#59ddbd;font-weight:600;")
        : QStringLiteral("color:#a8b3c3;font-weight:600;"));
}

void MainWindow::tuneSelectedChannel()
{
    tuneFrequency(static_cast<quint64>(std::llround(m_frequencyMhz->value() * 1.0e6)));
}

void MainWindow::tuneFrequency(quint64 frequencyHz)
{
    bool restart=m_receiver!=nullptr,scanning=m_scanning;if(restart)stopReceiver();
    {QSignalBlocker block(m_channelCombo);m_channelCombo->setCurrentIndex(m_channelCombo->findData(frequencyHz));}
    m_scanning=scanning;m_frequencyMhz->setValue(frequencyHz/1e6);m_snr=-99;m_serviceCount=0;m_pendingService=-1;
    m_plpCount=0;
    m_lockValue->setText(QStringLiteral("ПОИСК"));m_snrValue->setText(QStringLiteral("—"));m_l1Info->clear();
    m_serviceCombo->clear();m_serviceCombo->addItem(QStringLiteral("Весь мультиплекс"),-1);m_plpCombo->clear();m_plpCombo->addItem(QStringLiteral("PLP: автоматически"),-1);
    appendLog(QStringLiteral("Настройка на %1 МГц.").arg(frequencyHz/1e6,0,'f',3));if(restart)startReceiver();
    if(scanning){m_scanProgress->setVisible(true);m_scanButton->setText(QStringLiteral("Остановить сканирование"));}
}

void MainWindow::changeChannel(int delta)
{
    const int count = m_channelCombo->count();
    if(count == 0) return;
    int index = (m_channelCombo->currentIndex() + delta + count) % count;
    m_channelCombo->setCurrentIndex(index);
    tuneSelectedChannel();
}

void MainWindow::applyLiveGains()
{
    if(!m_receiver) return;
    QMetaObject::invokeMethod(m_receiver, "setGains", Qt::QueuedConnection,
                              Q_ARG(int, m_lnaGain->value() * 8),
                              Q_ARG(int, m_vgaGain->value() * 2),
                              Q_ARG(bool, m_rfAmp->isChecked()));
}

void MainWindow::updateRadioMetrics(const RadioMetrics &metrics)
{
    m_radioMetrics = metrics;
    m_levelValue->setText(QStringLiteral("%1 dBFS").arg(metrics.rmsDbfs, 0, 'f', 1));
    m_clipValue->setText(QStringLiteral("%1 %").arg(metrics.clipPercent, 0, 'f', 3));
    m_usbValue->setText(QStringLiteral("%1 MS/s")
                            .arg(metrics.usbMegaSamplesPerSecond, 0, 'f', 2));
    m_dropValue->setText(formatCount(metrics.droppedBuffers));
    if(m_running){
        if(metrics.droppedLastInterval)m_lockValue->setText(QStringLiteral("ПОТЕРИ I/Q"));
        else if(metrics.cpConfidence<=0){m_lockValue->setText(QStringLiteral("ПОИСК P1"));m_snr=-99;m_snrValue->setText(QStringLiteral("—"));}
        else if(m_serviceCount)m_lockValue->setText(QStringLiteral("TS LOCK"));
        else if(metrics.l1PostMatches)m_lockValue->setText(QStringLiteral("L1 / PLP"));
        else if(metrics.l1PreMatches)m_lockValue->setText(QStringLiteral("L1 PRE"));
        else if(metrics.p1Matches)m_lockValue->setText(QStringLiteral("P1 → L1"));
        else m_lockValue->setText(QStringLiteral("ПОИСК P1"));
    }
    updatePipeline();updateGainAdvice();
}

void MainWindow::updateTransportMetrics(const TransportMetrics &metrics)
{
    m_transportMetrics = metrics;updatePipeline();
    m_tsRateValue->setText(QStringLiteral("%1 Мбит/с").arg(metrics.bitrateMbps, 0, 'f', 2));
    m_tsPacketsValue->setText(formatCount(metrics.packets));
    m_tsErrorValue->setText(formatCount(metrics.transportErrors));
    m_tsContinuityValue->setText(formatCount(metrics.continuityErrors));
    m_tsSyncValue->setText(formatCount(metrics.syncLosses));
}

void MainWindow::updatePlps(const QStringList &names,const QList<int> &indices)
{
    const int count=qMin(names.size(),indices.size());
    const int selected=m_plpCombo->currentData().toInt();
    const int previousCount=m_plpCount;
    {
        QSignalBlocker block(m_plpCombo);
        m_plpCombo->clear();
        m_plpCombo->addItem(QStringLiteral("PLP: автоматически"),-1);
        for(int i=0;i<count;++i)m_plpCombo->addItem(names[i],indices[i]);
        const int index=m_plpCombo->findData(selected);
        if(index>=0)m_plpCombo->setCurrentIndex(index);
    }
    m_plpCount=count;
    if(count>0 && m_serviceCount==0){
        QSignalBlocker block(m_serviceCombo);
        m_serviceCombo->clear();
        m_serviceCombo->addItem(QStringLiteral("PLP найден — ждём TS/PAT/SDT"),-2);
    }
    if(count>0 && count!=previousCount)
        appendLog(QStringLiteral("L1 обнаружил PLP: %1. Ожидание корректного TS/PAT/SDT.").arg(count));
    updatePipeline();updateGainAdvice();
}

void MainWindow::updateServices(const QStringList &names,const QList<int> &ids)
{
    int selected=m_pendingService>=0?m_pendingService:m_serviceCombo->currentData().toInt();
    if(selected<-1)selected=-1;
    const int count=qMin(names.size(),ids.size());
    QSignalBlocker block(m_serviceCombo);
    m_serviceCombo->clear();m_serviceCombo->addItem(QStringLiteral("Весь мультиплекс"),-1);
    quint64 frequency=quint64(std::llround(m_frequencyMhz->value()*1e6));
    for(int i=0;i<count;++i){
        m_serviceCombo->addItem(QStringLiteral("%1 [SID %2]").arg(names[i]).arg(ids[i]),ids[i]);
        int row=0;for(;row<m_found->rowCount();++row)if(m_found->item(row,0)->data(Qt::UserRole).toULongLong()==frequency && m_found->item(row,1)->text().toInt()==ids[i])break;
        if(row==m_found->rowCount()){m_found->insertRow(row);for(int col=0;col<3;++col)m_found->setItem(row,col,new QTableWidgetItem);}
        m_found->item(row,0)->setText(QString::number(frequency/1e6,'f',3));m_found->item(row,0)->setData(Qt::UserRole,frequency);m_found->item(row,1)->setText(QString::number(ids[i]));m_found->item(row,2)->setText(names[i]);
    }
    int index=m_serviceCombo->findData(selected);if(index>=0)m_serviceCombo->setCurrentIndex(index);
    if(m_pendingService>=0 && index>=0){m_pendingService=-1;if(m_vlc->state()!=QProcess::NotRunning)launchVlc();}
    m_serviceCount=count;
    if(!m_serviceCount && m_plpCount>0)m_serviceCombo->addItem(QStringLiteral("PLP найден — ждём TS/PAT/SDT"),-2);
    if(m_serviceCount){m_lockValue->setText(QStringLiteral("TS LOCK"));appendLog(QStringLiteral("Сервисов в мультиплексе: %1").arg(m_serviceCount));savePreferences();}
    updatePipeline();updateGainAdvice();
}

void MainWindow::updateGainAdvice()
{
    QString text;
    QString styleName;
    if(m_radioMetrics.droppedLastInterval || m_radioMetrics.backlog>18){
        text=QStringLiteral("Потери I/Q: DSP не успевает. На 8 MS/s это уже не проблема USB/усиления; закройте тяжёлые программы. Очередь автоматически сбрасывает устаревшие блоки.");
        styleName=QStringLiteral("adviceBad");
    } else if(m_radioMetrics.clipPercent > 0.02 || m_radioMetrics.peakDbfs > -0.3) {
        text = QStringLiteral("Перегрузка АЦП: сначала выключите RF AMP, затем уменьшайте LNA и VGA примерно поровну.");
        styleName = QStringLiteral("adviceBad");
    } else if(m_radioMetrics.l1PostMatches && !m_serviceCount) {
        text=QStringLiteral("Параметры L1/PLP прочитаны, но транспортный поток пока не восстановлен. Значение SNR само по себе не подтверждает приём. Счётчики BCH, потерь I/Q и качество защитного интервала сохраняются в диагностике.");
        styleName=QStringLiteral("adviceWarn");
    } else if(m_radioMetrics.rmsDbfs < -38.0 && m_snr < 8.0) {
        text = QStringLiteral("Сигнал слабый: добавляйте LNA и VGA по одному шагу. RF AMP включайте последним.");
        styleName = QStringLiteral("adviceWarn");
    } else if(m_snr >= 16.0 && m_radioMetrics.clipPercent < 0.001) {
        text = QStringLiteral("Настройка усиления хорошая: высокий SNR, перегрузки практически нет.");
        styleName = QStringLiteral("adviceGood");
    } else if(!m_radioMetrics.l1PreMatches){
        text=QStringLiteral("L1 ещё не декодирован. Проверяйте P1, P2, CP и потери I/Q. Одного уровня сигнала недостаточно для оценки настройки.");
        styleName=QStringLiteral("adviceWarn");
    } else {
        text = QStringLiteral("Усиление допустимое. Настраивайте по максимуму SNR, а не по самому высокому уровню сигнала.");
        styleName = QStringLiteral("adviceGood");
    }
    m_gainAdvice->setObjectName(styleName);
    m_gainAdvice->style()->unpolish(m_gainAdvice);
    m_gainAdvice->style()->polish(m_gainAdvice);
    m_gainAdvice->setText(text);
}

QString MainWindow::findVlc() const
{
#ifdef Q_OS_WIN
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/vlc/vlc.exe"),
        qEnvironmentVariable("ProgramFiles") + QStringLiteral("/VideoLAN/VLC/vlc.exe"),
        qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/VideoLAN/VLC/vlc.exe"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/vlc/vlc.exe")
    };
    for(const QString &candidate : candidates)
        if(QFileInfo::exists(candidate)) return QDir::toNativeSeparators(candidate);
#endif
    return QStringLiteral("vlc.exe");
}

void MainWindow::browseVlc()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Укажите VLC 3.0"), QString(), QStringLiteral("VLC (vlc.exe)"));
    if(!path.isEmpty()) m_vlcPath->setText(QDir::toNativeSeparators(path));
}

void MainWindow::launchVlc()
{
    if(!m_udpEnabled->isChecked()) {
        QMessageBox::warning(this, QStringLiteral("VLC"),
                             QStringLiteral("Включите вывод транспортного потока в UDP."));
        return;
    }
    if(m_serviceCount<=0) {
        QMessageBox::information(this, QStringLiteral("VLC"),
                                 QStringLiteral("Каналы ещё не обнаружены. Дождитесь состояния TS LOCK и списка сервисов; одного L1/PLP недостаточно для воспроизведения."));
        return;
    }
    if(m_vlc->state()!=QProcess::NotRunning){m_vlc->kill();m_vlc->waitForFinished(1500);}

    QStringList arguments;
    arguments << QStringLiteral("--no-one-instance") << QStringLiteral("--no-media-library") << QStringLiteral("--network-caching=700")
              << QStringLiteral("--clock-jitter=0")
              << QStringLiteral("--clock-synchro=0");
    const int serviceId = m_serviceCombo->currentData().toInt();
    if(serviceId >= 0)
        arguments << QStringLiteral("--program=%1").arg(serviceId);
    arguments << QStringLiteral("udp://@127.0.0.1:%1").arg(m_udpPort->value());
    m_vlc->start(m_vlcPath->text(), arguments);
    if(!m_vlc->waitForStarted(2500)) {
        QMessageBox::critical(this, QStringLiteral("VLC"),
                              QStringLiteral("Не удалось запустить VLC. Укажите полный путь к vlc.exe."));
        appendLog(QStringLiteral("VLC не запущен: %1").arg(m_vlc->errorString()));
    } else {
        appendLog(QStringLiteral("VLC запущен для udp://@127.0.0.1:%1.").arg(m_udpPort->value()));
    }
}

void MainWindow::toggleRecording()
{
    if(!m_transport) {
        QMessageBox::information(this, QStringLiteral("Запись"),
                                 QStringLiteral("Сначала запустите приём."));
        return;
    }
    if(m_recording) {
        QMetaObject::invokeMethod(m_transport, "set_recording", Qt::QueuedConnection,
                                  Q_ARG(bool, false), Q_ARG(QString, QString()));
        m_recording = false;
        m_recordButton->setText(QStringLiteral("● Начать запись .ts"));
        appendLog(QStringLiteral("Запись транспортного потока остановлена."));
        return;
    }
    const QString suggested = QStringLiteral("DVB-T2_%1MHz_%2.ts")
        .arg(m_frequencyMhz->value(), 0, 'f', 3)
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Запись MPEG-TS"), suggested,
        QStringLiteral("MPEG Transport Stream (*.ts)"));
    if(path.isEmpty()) return;
    QMetaObject::invokeMethod(m_transport, "set_recording", Qt::QueuedConnection,
                              Q_ARG(bool, true), Q_ARG(QString, path));
    m_recording = true;
    m_recordButton->setText(QStringLiteral("■ Остановить запись"));
    appendLog(QStringLiteral("Запись транспортного потока: %1").arg(path));
}

void MainWindow::exportDiagnostics()
{
    const QString suggested = QStringLiteral("HackRFT2_Diagnostics_%1.txt")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Диагностический отчёт"), suggested,
        QStringLiteral("Текстовый файл (*.txt)"));
    if(path.isEmpty()) return;

    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Диагностика"), file.errorString());
        return;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "HackRF T2 Viewer diagnostics\n" << Diagnostics::environment();
    out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";
    out << "Device: " << m_deviceInfo.boardName << "\n";
    out << "Revision: " << m_deviceInfo.boardRevision << "\n";
    out << "Serial: " << m_deviceInfo.serial << "\n";
    out << "Firmware: " << m_deviceInfo.firmware << "\n";
    out << "USB API: " << m_deviceInfo.usbApi << "\n";
    out << "HackRF Pro: " << (m_deviceInfo.isPro ? "yes" : "no") << "\n";
    out << "Clock input: " << (m_deviceInfo.clockInputDetected ? "detected" : "internal") << "\n\n";
    const HackRfSettings settings = settingsFromUi();
    out << "Frequency Hz: " << settings.frequencyHz << "\n";
    out << "Sample rate: " << settings.sampleRateHz << "\n";
    out << "Bandwidth: " << settings.bandwidthHz << "\n";
    out << "RF AMP: " << settings.rfAmp << "\n";
    out << "LNA dB: " << settings.lnaGainDb << "\n";
    out << "VGA dB: " << settings.vgaGainDb << "\n";
    out << "Bias tee: " << settings.biasTee << "\n";
    out << "Narrowband: " << settings.narrowbandFilter << "\n";
    out << "CLKOUT: " << settings.clockOut << "\n\n";
    out << "SNR dB: " << m_snr << "\n";
    out << "CP quality symbols: " << m_radioMetrics.guardQualitySymbols << "\n";
    out << "CP coherence: " << m_radioMetrics.guardCoherence << "\n";
    out << "CP repeatability dB (not calibrated C/N): " << m_radioMetrics.guardRepeatabilityDb << "\n";
    out << "Residual carrier offset Hz: " << m_radioMetrics.residualFrequencyHz << "\n";
    out << "RMS dBFS: " << m_radioMetrics.rmsDbfs << "\n";
    out << "Peak dBFS: " << m_radioMetrics.peakDbfs << "\n";
    out << "Clip percent: " << m_radioMetrics.clipPercent << "\n";
    out << "USB MS/s: " << m_radioMetrics.usbMegaSamplesPerSecond << "\n";
    out << "Dropped buffers: " << m_radioMetrics.droppedBuffers << "\n\n";
    out << "TS bitrate Mbps: " << m_transportMetrics.bitrateMbps << "\n";
    out << "TS packets: " << m_transportMetrics.packets << "\n";
    out << "TEI errors: " << m_transportMetrics.transportErrors << "\n";
    out << "Continuity errors: " << m_transportMetrics.continuityErrors << "\n";
    out << "Sync losses: " << m_transportMetrics.syncLosses << "\n\n";
    out << "BCH frames: " << m_transportMetrics.bchFrames << "\n";
    out << "BCH failed: " << m_transportMetrics.bchFailedFrames << "\n";
    out << "BB frames: " << m_transportMetrics.bbFrames << "\n";
    out << "BB header errors: " << m_transportMetrics.bbHeaderErrors << "\n";
    out << "--- Log ---\n" << m_log->toPlainText() << "\n";
    // Include the most recent worker events in the report itself. Copy all
    // bounded session logs and CSVs alongside it for detailed analysis.
    const QString session = Diagnostics::sessionDir();
    QStringList copyErrors;
    const QString bundle = path + QStringLiteral(".files");
    if(!session.isEmpty() && QDir().mkpath(bundle)) {
        for(const auto &entry : QDir(session).entryInfoList(QDir::Files)) {
            if(!QFile::copy(entry.absoluteFilePath(), QDir(bundle).filePath(entry.fileName())))
                copyErrors << entry.fileName();
            if(entry.suffix() == QStringLiteral("log")) {
                QFile log(entry.absoluteFilePath());
                if(log.open(QIODevice::ReadOnly)) {
                    log.seek(qMax(qint64(0), log.size() - 256 * 1024));
                    out << "\n--- " << entry.fileName() << " (tail) ---\n" << QString::fromUtf8(log.readAll());
                }
            }
        }
        out << "\nAttached files: " << bundle << "\n";
    } else copyErrors << QStringLiteral("Cannot create diagnostics folder");
    out.flush();
    const bool reportOk = file.error() == QFileDevice::NoError;
    file.close();
    if(!reportOk || !copyErrors.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Диагностика"),
            QStringLiteral("Отчёт сохранён не полностью. Ошибки: %1").arg(copyErrors.join(QStringLiteral(", "))));
        return;
    }
    appendLog(QStringLiteral("Диагностический отчёт сохранён: %1").arg(path));
}

void MainWindow::startScan()
{
    if(m_scanning) {
        m_scanning = false;m_scanTimer.stop();
        m_scanButton->setText(QStringLiteral("Сканировать UHF 21–69"));
        appendLog(QStringLiteral("Сканирование остановлено пользователем."));
        return;
    }
    if(!m_running) {
        QMessageBox::information(this, QStringLiteral("Сканирование"),
                                 QStringLiteral("Сначала запустите приём."));
        return;
    }
    m_scanning = true;
    m_scanIndex = 0;
    m_scanResults.clear();
    m_scanProgress->setVisible(true);
    m_scanProgress->setValue(0);
    m_scanButton->setText(QStringLiteral("Остановить сканирование"));
    appendLog(QStringLiteral("Сканирование ТВК 21–69: %1 секунд на частоту.").arg(m_scanDwell->value()));
    advanceScan();
}

void MainWindow::advanceScan()
{
    if(!m_scanning) return;
    if(m_scanIndex > 0) {
        const int previousChannel = 20 + m_scanIndex;
        if(m_serviceCount > 0) {
            const QString found = QStringLiteral("ТВК %1: %2 МГц, SNR %3 дБ, сервисов %4")
                .arg(previousChannel)
                .arg(474 + (previousChannel - 21) * 8)
                .arg(m_scanBestSnr, 0, 'f', 1)
                .arg(m_serviceCount);
            m_scanResults.append(found);
            appendLog(QStringLiteral("НАЙДЕН: %1").arg(found));
        } else if(m_scanBestSnr > 2.0) {
            appendLog(QStringLiteral("ТВК %1: есть кандидат по SNR %2 дБ, но сервисов TS не найдено — частота не добавлена.")
                      .arg(previousChannel).arg(m_scanBestSnr,0,'f',1));
        }
    }
    if(m_scanIndex >= 49) {
        m_scanning = false;m_scanTimer.stop();
        m_scanButton->setText(QStringLiteral("Сканировать UHF 21–69"));
        m_scanProgress->setVisible(false);
        appendLog(m_scanResults.isEmpty()
            ? QStringLiteral("Сканирование завершено: мультиплексы не найдены.")
            : QStringLiteral("Сканирование завершено. Найдено частот: %1.").arg(m_scanResults.size()));
        return;
    }
    const int channel = 21 + m_scanIndex;
    const quint64 frequency = static_cast<quint64>(474 + m_scanIndex * 8) * 1000000ULL;
    m_channelCombo->setCurrentIndex(m_scanIndex);
    m_scanBestSnr = -99.0;
    m_serviceCount = 0;
    tuneFrequency(frequency);
    ++m_scanIndex;
    m_scanProgress->setValue(m_scanIndex);
    // Dwell starts when the new receiver reports runningChanged(true).
}

void MainWindow::appendLog(const QString &message)
{
    qInfo().noquote()<<message;
    if(!m_log) return;
    m_log->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void MainWindow::updatePipeline()
{
    const auto &r=m_radioMetrics;const auto &t=m_transportMetrics;
    QString line=QStringLiteral("DSP %1% · %2 MS/s · очередь %3/24 | P1 %4 · P2 %5 | L1 PRE %6 / POST %7 · ошибок PRE %8\nCP %9 · GI %10 · коррекция %11 Гц | BB %12 / CRC %13 | BCH %14 / исправлено %15 / ошибок %16")
        .arg(r.dspLoadPercent,0,'f',0).arg(r.dspMegaSamplesPerSecond,0,'f',2).arg(r.backlog).arg(r.p1Matches).arg(r.p2Attempts).arg(r.l1PreMatches).arg(r.l1PostMatches).arg(r.l1PreErrors)
        .arg(r.cpConfidence,0,'f',2).arg(r.guardSamples).arg(r.coarseCorrectionHz,0,'f',0).arg(t.bbFrames).arg(t.bbHeaderErrors).arg(t.bchFrames).arg(t.bchCorrectedBits).arg(t.bchFailedFrames);
    m_pipeline->setText(line);
    if(m_statsLog.isValid() && m_statsLog.elapsed()>5000){appendLog(line+QStringLiteral(" | IQ %1 dBFS | clip %2% | samples %3 | drops %4 | TS %5").arg(r.rmsDbfs,0,'f',1).arg(r.clipPercent,0,'f',3).arg(r.processedSamples).arg(r.droppedBuffers).arg(t.packets));m_statsLog.restart();}
}
void MainWindow::loadIq()
{
    QString path=QFileDialog::getOpenFileName(this,QStringLiteral("I/Q signed int8 I,Q. Задайте исходную частоту дискретизации."),QString(),QStringLiteral("I/Q (*.cs8 *.iq);;Все файлы (*)"));
    if(path.isEmpty())return;
    stopReceiver();
    QFile metadata(path+QStringLiteral(".json"));
    if(metadata.open(QIODevice::ReadOnly)) {
        const auto object=QJsonDocument::fromJson(metadata.read(65536)).object();
        const double rate=object.value(QStringLiteral("sample_rate_hz")).toDouble();
        const int index=m_sampleRate->findData(rate);
        if(index>=0) m_sampleRate->setCurrentIndex(index);
        const double frequency=object.value(QStringLiteral("frequency_hz")).toDouble();
        if(frequency>=1e6 && frequency<=6e9) m_frequencyMhz->setValue(frequency/1e6);
        appendLog(QStringLiteral("Метаданные I/Q прочитаны: %1 MS/s").arg(m_sampleRate->currentData().toDouble()/1e6));
    }
    m_iqPath=path;appendLog(QStringLiteral("Источник I/Q: ")+path);startReceiver();
}
void MainWindow::savePreferences()
{
    QSettings cfg(QCoreApplication::applicationDirPath()+"/settings.ini",QSettings::IniFormat);
    cfg.setValue("frequencyMHz",m_frequencyMhz->value());cfg.setValue("lna",m_lnaGain->value());cfg.setValue("vga",m_vgaGain->value());cfg.setValue("amp",m_rfAmp->isChecked());
    cfg.setValue("rate",m_sampleRate->currentIndex());cfg.setValue("dwell",m_scanDwell->value());cfg.setValue("vlc",m_vlcPath->text());cfg.setValue("port",m_udpPort->value());
    cfg.beginWriteArray("channels",m_found->rowCount());
    for(int row=0;row<m_found->rowCount();++row){cfg.setArrayIndex(row);cfg.setValue("frequency",m_found->item(row,0)->data(Qt::UserRole));cfg.setValue("sid",m_found->item(row,1)->text());cfg.setValue("name",m_found->item(row,2)->text());}cfg.endArray();
}
void MainWindow::loadPreferences()
{
    QSettings cfg(QCoreApplication::applicationDirPath()+"/settings.ini",QSettings::IniFormat);
    m_frequencyMhz->setValue(cfg.value("frequencyMHz",586).toDouble());m_lnaGain->setValue(cfg.value("lna",2).toInt());m_vgaGain->setValue(cfg.value("vga",8).toInt());m_rfAmp->setChecked(cfg.value("amp",false).toBool());
    m_sampleRate->setCurrentIndex(qBound(0,cfg.value("rate",0).toInt(),4));m_scanDwell->setValue(cfg.value("dwell",12).toInt());m_udpPort->setValue(cfg.value("port",7654).toInt());
    QString vlc=cfg.value("vlc").toString();if(QFileInfo::exists(vlc))m_vlcPath->setText(vlc);
    int count=qMin(10000,cfg.beginReadArray("channels"));m_found->setRowCount(count);
    for(int row=0;row<count;++row){cfg.setArrayIndex(row);quint64 frequency=cfg.value("frequency").toULongLong();auto *item=new QTableWidgetItem(QString::number(frequency/1e6,'f',3));item->setData(Qt::UserRole,frequency);m_found->setItem(row,0,item);m_found->setItem(row,1,new QTableWidgetItem(cfg.value("sid").toString()));m_found->setItem(row,2,new QTableWidgetItem(cfg.value("name").toString()));}cfg.endArray();{QSignalBlocker block(m_channelCombo);m_channelCombo->setCurrentIndex(m_channelCombo->findData(quint64(std::llround(m_frequencyMhz->value()*1e6))));}updatePipeline();
}
