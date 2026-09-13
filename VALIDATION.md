# Validation — 1.4.2-RC1

This is a diagnostic release candidate, not a confirmed working broadcast receiver.

## Reviewed inputs

Reviewed the only remote branch (`main`, 20 commits through
`6d7b77277e8c8c4c12f66308cf797ca3367845b9`), source archives 1.1.0, 1.2.0,
1.3.0, 1.4.0 and both 1.4.1-DIAG archives, application logs, FEC timings,
DSP CSV profiles and the provided 586 MHz / 10 MS/s cs8 recording.
Private recordings and original diagnostic reports are not part of this repository.

The old Python/leandvb proposal cannot decode DVB-T2. The proposed `dtv-utils`
replacement command was not an established working receiver. This candidate
continues the actual C++ DVB-T2 implementation already in this repository.

## Findings and changes

- BCH work caused backpressure in the supplied timings. Byte-table syndrome
  calculation and binary-field symmetry replace the bit-by-bit hot loop.
  Corrected codewords are checked again before acceptance.
- LDPC status calculation did not compile for scalar L1 decoding. Scalar and
  SIMD paths now handle validity separately.
- QPSK does not use the parity interleaver used for the supported QAM modes.
  Its bypass is now covered by an encoded nonzero QPSK test.
- Do not discard an entire FEC word solely because LDPC has residual errors;
  BCH gets the opportunity to correct them and rejects uncorrectable words.
- Do not mutate common-PLP metadata to suppress decoding. The recording has
  two Type 1 data PLPs, not a common PLP that explains the overload.
- Flush partial SIMD batches on a 200 ms timer, and explicitly at test EOF.
- With time interleaving disabled, reset cell-interleaver cyclic shifts per
  FEC word; the former code applied a multi-FEC permutation to that mode.
- Keep owned asynchronous payloads and bounded queues. A full queue still
  applies backpressure; this does not guarantee zero drops on every CPU.
- Bounded logs per application session, export of worker logs and DSP CSVs,
  build/OS/Qt identification, and atomic cs8 recording with JSON metadata.

## Reproducible checks

Run `QT_NATIVE=/usr bash build-tests-native.sh` on Linux with GCC, Qt 5 development
packages, FFTW, libusb and ripgrep. Default is AddressSanitizer + UBSan; use a
separate `BUILD` directory when changing `SANITIZE`. GUI uses Qt offscreen.
The Windows packaging job depends on the native regression job.

The native regression suite and GUI start/replay/retune/stop tests passed locally with ASan and UBSan (leak detection disabled). GUI tests use offscreen CPU rendering with QT_NO_OPENGL; they do not validate an OpenGL or Windows runtime. See TEST-RESULTS.txt.

The tests include P1/P2 and L1 signalling, queue ownership and receiver restart,
all constellations and short/normal FEC sizes, partial batches, nonzero BCH
codewords for all 12 supported rate/FEC combinations with 0..capacity errors,
and byte-exact TS reconstruction, PAT discovery, file output and local UDP.

`fec_vector_test` contains a forward BCH encoder, LDPC encoder, QAM mapper and
cell/time interleaver. It does not call receiver permutation or demapper helpers.
It compares every recovered systematic bit with the original, including QPSK
4/5 and rotated 64-QAM 4/5 with TI lengths 0, 1 and 3 (33/33/108 FEC words).
It is a synthetic FEC test, not a complete RF-to-video broadcast test.

## Full RF-to-TS regression added on 2026-09-12

`full_rf_test` now constructs 108 **different** BBFRAME payloads containing known
TS packets, BCH/LDPC encodes them, applies rotated 64-QAM and TI length 3, builds
32K extended / GI 1/16 / PP4 OFDM frames, independently resamples to 10 MS/s,
and quantizes to signed 8-bit I/Q. It passes those samples through the real
receiver input, DSP workers and transport writer. Every recovered systematic
bit and every output TS byte is compared with the original. The default 20 dB
AWGN case recovers 324/324 FEC words and 2,071,008 TS bytes without errors.
This test is part of the default sanitizer gate before Windows packaging.
Pilot generation and frequency-address tables are shared with the receiver;
this is not an independent certification of those tables. The TS payload is
a deterministic test payload, not encoded television video.

A separate release-build pacing check feeds 12 frames at 10 MS/s without waiting
for the DSP after each USB-sized block. It recovered 1,188/1,188 FEC words and
7,593,696 exact TS bytes with zero queue drops on the current Linux environment.
This approximately three-second check does not certify sustained Windows/USB
performance. Run it without sanitizers:

```bash
REALTIME_RF=1 QT_NATIVE=/usr SANITIZE=0 NO_GUI=1 TESTS=full_rf_test \
  bash build-tests-native.sh 20
```

The direct elementary-rate diagnostic variant (`DIRECT_RF=1`) previously recovered
324/324 repeated-payload words at 18 dB. At 15 dB it recovered none. These are
measurements of this test setup, not a universal reception threshold.

Continuous CP coherence, CP repeatability in dB, its symbol count, and residual
carrier offset are now included in the bounded DSP CSV and exported report.
CP repeatability is **not calibrated C/N**: timing error, multipath and
interference also affect it. The GUI explains that its nearest-constellation
SNR estimate alone does not establish reception.

## Actual recording result

The provided file contains 57,817,088 bytes (2.8908544 seconds at 10 MS/s).
P1 and both L1 CRC checks succeed. The recording signals 32K, extended carriers,
GI 1/16, PP4, and two rotated 64-QAM / normal FEC / 4/5 / TI-length-3 PLPs.
No ADC samples reach the signed-int8 clipping rails.

The offline replay processes all 624 FEC words, all fail BCH, and produces **zero TS bytes**. Therefore working TV reception,
VLC video playback and absence of overload on the user's Windows/HackRF setup
are **not established**. Zero drops in the offline harness are not a hardware
load result: its producer waits for the demodulator after every block.
Both reduced frontend loop activity and an experimental increase to 100 LDPC
iterations failed to recover TS; those experimental changes are not enabled.
Further complex/pilot-smoothed equalizers and weighted 64-point max-log demapping
also recovered no valid BCH words. Investigative data replacement and unbounded
FFT/TI dumps were removed from production code.

The new continuous measurement on the supplied recording is CP coherence 0.9693,
repeatability about 15.0 dB and residual carrier offset about -0.53 Hz at EOF.
A check on the raw CS8 samples, before the receiver DSP, also finds reduced
CP repeatability. Together with the controlled noise tests, this makes input
quality a plausible limiting factor, **not proof that every software issue is
excluded or that antenna/gain settings are the sole cause**.

To reproduce without hardware:

```bash
QT_NATIVE=/usr SANITIZE=0 NO_GUI=1 TESTS=iq_replay_test \
  bash build-tests-native.sh /path/recording.cs8 /path/output.ts 10000000
```

Exit 0 means some TS was written, 3 means no TS, and 2 means invalid input.
TS presence alone does not prove clean video or error-free transport.

No HackRF is attached to the build environment. Windows runtime/WinUSB and real
broadcast playback still need verification. The CI build log is authoritative
for whether the Windows executable and portable ZIP were built successfully.

## Independent GNU Radio interoperability check (2026-09-13)

`tests/gnuradio_reference.py` uses GNU Radio's gr-dtv transmitter, with no
receiver encoders, pilot tables, frequency permutations or fixture helpers.
The transmitter originates in [gr-dvbt2](https://github.com/drmpeg/gr-dvbt2),
whose author reports verification against BBC DVB-T2 reference streams.
This is an interoperability check, not formal DVB certification.

The tested profile is 32K extended, GI 1/16, PP4, rotated normal-frame
64-QAM 4/5, TI length 3, one PLP with 108 FEC blocks, 63 data symbols,
QPSK L1-post and high-efficiency TS input. SciPy independently resamples the
transmitter output from 64/7 MS/s to 10 MS/s and adds 20 dB AWGN before CS8
quantization. None of the generated samples clip.

Five T2 frames produce 24,393,600 CS8 bytes. After first-frame acquisition,
the receiver accepts **432/432 FEC blocks**, recovering **14,891 packets /
2,799,508 TS bytes**. Every byte is compared against distinct source packets;
the check also requires the complete expected sequence, so truncation,
duplicates and missing packets fail. Both release and ASan/UBSan runs pass.
The independent check is now part of the native gate before Windows packaging.

To reproduce on Linux with GNU Radio 3.10, NumPy, SciPy and the native receiver
build dependencies:

```bash
python3 tests/gnuradio_reference.py generate /tmp/gr-reference
QT_NATIVE=/usr SANITIZE=1 NO_GUI=1 TESTS=iq_replay_test \
  bash build-tests-native.sh /tmp/gr-reference/input.cs8 /tmp/gr-output.ts
python3 tests/gnuradio_reference.py verify /tmp/gr-reference /tmp/gr-output.ts
```

A separate manual trial used an FFmpeg-generated MPEG-2 video / MP2 audio TS
in the same transmitter chain. The receiver discovered `Independent_Test`;
its 2,799,508 output bytes matched the corresponding cyclic source bytes
exactly. FFprobe read 584 video frames and 994 audio frames. It warned at the
incomplete video boundaries because reception starts and ends within GOPs.
This validates service discovery and a decodable video/audio transport stream;
it does not validate Windows VLC playback or a physical HackRF input.

The supplied two-PLP recording still yields **624/624 failed BCH blocks and
zero TS bytes**. Its reception is not fixed. This independent single-PLP test
does not certify every multi-PLP path or establish the cause of that failure.
No new hardware capture or installation is required from the user for these
software checks.

## GUI replay fixes verified on 2026-09-13

The independent encoded-video fixture exposed two GUI-path defects:

- File replay applied the physical tuner's 120 ms settling discard. That lost
  the first P1 and one additional T2 frame: GUI replay recovered only 324 FEC
  words / 11,168 packets, versus 432 / 14,891 through the CLI. Settling is now
  applied only to hardware input. GUI and CLI recover the same complete TS.
- A late SNR notification overwrote `TS LOCK` with `DEMOD` after service
  discovery. SNR now updates its own display; receiver/service state controls
  the lock indicator.

`gui_iq_replay_test` drives MainWindow and its normal file timer, including
backpressure and the application's own partial-FEC-batch timeout. It requires
all expected FEC words and TS packets, then the independent Python verifier
compares every output byte. The GUI check is included in the same CI gate.
Release is still a diagnostic candidate; these fixes do not recover the
supplied noisy/two-PLP recording.

The encoded-video GUI trial under ASan/UBSan now discovers `Independent_Test`,
keeps `TS LOCK`, accepts 432/432 BCH frames and writes 2,799,508 exact source
bytes. The separate start/replay/retune/stop smoke test also passes.

## L1 dynamic signalling and Windows runtime gate (2026-09-13)

The L1-post parser contained several independent defects outside the profile
of the supplied recording: repeated dynamic data began at the wrong offset;
next-frame RF/block counts overwrote current-frame values; next-frame PLP and
AUX arrays were not allocated; 48-bit AUX fields used 32-bit storage/shifts;
reserved/AUX fields accumulated stale values or retained only their last bit.
The configurable FEF-length-MSB/reserved fields also used the wrong offset.
These are corrected against ETSI EN 302 755 V1.4.1, clauses 7.2.3.1–3:
https://www.etsi.org/deliver/etsi_en/302700_302799/302755/01.04.01_60/en_302755v010401p.pdf

New field vectors cover one/two PLPs, zero/two AUX streams, repetition on/off,
different current/next scheduling, all 48 AUX bits, a second cleared frame,
owned asynchronous snapshots and every truncated length. They pass under
ASan/UBSan, as do the existing P2 FEC/acquisition tests. These checks do not
claim support for decoding auxiliary payloads or for TFS/FEF reception.

CI now transfers the independently verified GNU Radio IQ fixture and exact
expected TS to Windows. A test entry point uses the same GUI and receiver
sources, replays through the application's file timer, and checks every FEC
word and packet count. The packaged DLLs are exercised with the MinGW tools
removed from PATH; SHA-256 must match the exact reference TS before the
portable ZIP is published. This gate does not exercise a physical USB device.

### Upstream receiver comparison

The unmodified DVB-T2 sources of voxo22/HackRF_dvbt2_receiver at
`a70ca1af05a0495944fe1df06ff11b68bca73432` were built separately with a small
Qt console replay harness, lossless backpressure and decoding enabled:
https://github.com/voxo22/HackRF_dvbt2_receiver/tree/a70ca1af05a0495944fe1df06ff11b68bca73432

On the older five-frame GNU Radio 20 dB control recording it accepted 216 BCH
words and produced 1,399,472 TS bytes. Every emitted packet matched the
source, but one sequence gap remained. On the supplied 57,817,088-byte
HackRF recording it detected P1 but delivered no valid L1-post frames and
zero TS bytes. This comparison does not establish the cause of the supplied
recording's failure; it establishes that this fork is not a verified drop-in
solution for it. No decoder changes from that fork were copied into this app.
