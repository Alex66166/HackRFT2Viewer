QT += core gui widgets network concurrent printsupport
CONFIG += c++17 release
CONFIG -= app_bundle
TEMPLATE = app
TARGET = HackRFT2Viewer

DEFINES += UNICODE _UNICODE HACKRF_STATIC
QMAKE_CXXFLAGS_RELEASE += -O2 -mavx2 -msse4.1
QMAKE_CFLAGS_RELEASE += -O2

INCLUDEPATH += \
    $$PWD/src \
    $$PWD/src/DSP \
    $$PWD/src/DVB_T2 \
    $$PWD/src/fftw3 \
    $$PWD/src/third_party/libhackrf \
    $$PWD/src/third_party/libusb

SOURCES += \
    $$PWD/src/main.cpp \
    $$PWD/src/main_window.cpp \
    $$PWD/src/rx_hackrf_pro.cpp \
    $$PWD/src/plot.cpp \
    $$PWD/src/qcustomplot.cpp \
    $$PWD/src/third_party/libhackrf/hackrf.c \
    $$files($$PWD/src/DVB_T2/*.cpp) \
    $$PWD/src/DVB_T2/LDPC/tables_handler.cc

HEADERS += \
    $$PWD/src/diagnostics.h \
    $$PWD/src/main_window.h \
    $$PWD/src/rx_hackrf_pro.h \
    $$PWD/src/plot.h \
    $$PWD/src/qcustomplot.h \
    $$files($$PWD/src/DSP/*.h) \
    $$files($$PWD/src/DSP/*.hh) \
    $$files($$PWD/src/DVB_T2/*.h) \
    $$files($$PWD/src/DVB_T2/LDPC/*.hh) \
    $$PWD/src/third_party/libhackrf/hackrf.h \
    $$PWD/src/third_party/libusb/libusb.h

win32 {
    LIBS += -L$$PWD/src/third_party/libusb -llibusb-1.0
    LIBS += -L$$PWD/src/fftw3 -llibfftw3f-3
    LIBS += -lwinpthread -ldwmapi -luxtheme -lversion -lnetapi32 -luserenv -liphlpapi
}
