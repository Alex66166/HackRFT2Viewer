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
