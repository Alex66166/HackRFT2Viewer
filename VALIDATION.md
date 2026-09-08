# Validation — 1.4.0

Date: 2026-09-07. Target: Windows x64, Qt 5.15.2, MinGW-w64 GCC 13.2 (AVX2).
Native instrumented tests: Linux x64, Qt 5.15.13, AddressSanitizer + UndefinedBehaviorSanitizer.

Changes covered by this release:
- The I/Q producer now batches queued blocks behind one demodulator event and preserves sequence-gap detection.
- A valid L1-post publishes PLP IDs and modulation/FEC labels before BBFRAME/PSI; the GUI exposes the intermediate state and blocks empty VLC launches.
- The GUI includes an 8 MS/s input option while retaining the old settings-index meanings.

Passed:
- Synthetic DVB-T2 P1 at amplitudes 0.5, 0.1, 0.02 and 0.005; P1 CFO at ±2.2/±18 kHz.
- P1 → P2 → L1-pre through signed-int8 I/Q input, independent resampling to 8/10/12.5/16/20 MS/s, FFT 16K/32K and all seven guard intervals.
  Caller buffers are overwritten immediately after submission to exercise ownership.
- Zero-input I/Q correction remains finite; recurrence NCO and integer ADC statistics benchmarked.
- L1-pre BCH/LDPC shortening and depuncturing with 0/1/6/12/20 damaged bits; L1-post BCH/LDPC, padding, puncturing, soft LLR, all four modulations and scrambling; PLP ID 17 parsing.
- All four constellations, short and normal FEC, 33 frames delivered as 32+1 SIMD lanes, PLP at index 1.
- One LDPC lane followed by BCH without reading unused SIMD lanes.
- BCH: all 12 supported FEC/rate pairs, injected errors from 0 to the correction limit.
- Normal and High Efficiency BBFRAME payloads split across frames: exact reconstruction of seven TS packets,
  valid PAT/service detection, identical recorded bytes and local UDP datagrams.
- GUI: start, I/Q replay, frequency change and stop repeated three times; persistent service-list insertion; status and diagnostic labels.
- Visual inspection of the GUI at 1460×900.
- Final Windows executable builds; non-system DLL imports are present in the portable directory.
- Official VLC 3.0.23 Windows ZIP SHA256 verified:
  992d19dbd0b8a7cde9167d2f7780b1ef6f92acc8a71acfa736101a21f35181e1

No AddressSanitizer or UndefinedBehaviorSanitizer errors occurred in these tests. Intentional queue-overflow capture test passed.
Release benchmark: 12.6 MS/s on the build host for the synthetic frontend workload; this is not a Windows/HackRF guarantee.
LeakSanitizer was disabled because process-task enumeration is unavailable in this environment.
Qt reports upstream deprecated painting APIs at compilation; those are warnings, not build failures.

Not tested:
- No HackRF hardware was attached.
- No Windows session was available to launch the PE executable or exercise WinUSB.
- No complete user broadcast recording was available; all synthetic P2/L1 and transport regressions pass.
- No over-the-air reception or real-time performance claim is made.

The source includes the tests and scripts. TEST-RESULTS.txt lists the successful test results.
The bundled Windows libusb DLL is the official 1.0.29 MinGW64 build; it replaces the old custom 1.0.27 RC DLL.
