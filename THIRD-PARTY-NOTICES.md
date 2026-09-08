# Third-party notices and source locations

HackRF T2 Viewer modifications are provided under GPL-3.0-or-later.
The full application source accompanies this distribution in Source-1.4.0.zip.

- sdr_receiver_dvb_t2, Oleg Malyutin and contributors, GPL-3.0-or-later.
  https://github.com/Oleg-Malyutin/sdr_receiver_dvb_t2/tree/332e9704c29d16940a7322b9f49a11a6f5c04437
  The DVB-T2 core has been modified; see CHANGELOG.md.
- LDPC code: Ahmet Inan, Ron Economos and Oleg Malyutin, GPL-3.0-or-later.
  Copyright notices remain in src/DVB_T2/LDPC.
- BCH / Galois-field code: Ahmet Inan and Ron Economos, GPL-3.0-or-later.
  https://github.com/drmpeg/gr-dvbs2rx/tree/74df1dadbd20b02ce0cdfd3244f9c97022981235/lib
  Four headers in src/third_party/bch; bitman.hh functions marked inline for use in multiple translation units.
- HackRF host library: Great Scott Gadgets, Jared Boone, Benjamin Vernoux and contributors, BSD-3-Clause.
  https://github.com/greatscottgadgets/hackrf/tree/393dc91be7e0cd17af61600d7ce31d8a54a2f404/host/libhackrf
  The applicable host-library notice is in HACKRF-LICENSE.txt.
- QCustomPlot: Emanuel Eichhammer, GPL-3.0; copyright and license header retained in qcustomplot.cpp/.h.
  https://www.qcustomplot.com/
- FFTW 3: Matteo Frigo, Massachusetts Institute of Technology and contributors, GPL-2.0-or-later.
  https://www.fftw.org/fftw-3.3.5.tar.gz
- Qt 5.15.2 runtime: The Qt Company and contributors, LGPL-3.0/GPL.
  https://download.qt.io/archive/qt/5.15/5.15.2/single/
  Shared libraries remain replaceable. License texts are in licenses/.
- libusb 1.0.29, official MinGW64 DLL: libusb contributors, LGPL-2.1-or-later.
  https://github.com/libusb/libusb/releases/tag/v1.0.29
- GCC / MinGW-w64 runtime: GCC contributors, GPL with GCC Runtime Library Exception; MinGW-w64 copyright notices.
  https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html
  https://www.mingw-w64.org/
- VLC 3.0.23 x64: VideoLAN and contributors, GPL-2.0-or-later and component licenses.
  VLC is an unmodified, separate program under vlc/ with its COPYING.txt, AUTHORS.txt, THANKS.txt.
  Official binary: https://download.videolan.org/pub/videolan/vlc/3.0.23/win64/vlc-3.0.23-win64.zip
  Corresponding source: https://download.videolan.org/pub/videolan/vlc/3.0.23/vlc-3.0.23.tar.xz

No affiliation with or endorsement by the upstream projects is implied.

Upstream source archives for FFTW 3.3.5, libusb 1.0.29 and VLC 3.0.23 are included in third-party-source/ inside the application source ZIP.

- L1 shortening/puncturing and reference transmitter: GNU Radio gr-dtv v3.10.12.0,
  Copyright 2015-2017, 2019 Free Software Foundation, Inc., GPL-3.0-or-later.
  https://github.com/gnuradio/gnuradio/tree/v3.10.12.0/gr-dtv/lib/dvbt2
  Reference files retained under tests/reference/; L1 FEC adaptations under src/DVB_T2/.
