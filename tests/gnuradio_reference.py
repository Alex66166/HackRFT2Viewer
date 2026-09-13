#!/usr/bin/env python3
"""Independent RF integration fixture using GNU Radio 3.10's DVB-T2 transmitter.

No receiver tables/encoders are imported. Requires GNU Radio, NumPy and SciPy.
Generate: python3 tests/gnuradio_reference.py generate /tmp/gr-reference
Replay:  build-native/iq_replay_test /tmp/gr-reference/input.cs8 /tmp/output.ts
Verify:  python3 tests/gnuradio_reference.py verify /tmp/gr-reference /tmp/output.ts

Transmitter provenance: https://github.com/drmpeg/gr-dvbt2 (now GNU Radio gr-dtv).
This is an interoperability regression, not a DVB certification or RF hardware test.
"""
import argparse
import json
from pathlib import Path

import numpy as np

FEC_BLOCKS = 108
DATA_BYTES_PER_BBFRAME = (51648 - 80) // 8
PACKET_COUNT = 32768


def generate(args):
    from gnuradio import blocks, digital, dtv, gr
    from scipy.signal import resample_poly

    directory = args.directory
    directory.mkdir(parents=True, exist_ok=True)
    # Keep every packet distinct; HEM transmits the 187 bytes after sync.
    rng = np.random.default_rng(20260912)
    packets = rng.integers(0, 256, (PACKET_COUNT, 188), dtype=np.uint8)
    packets[:, 0:3] = [0x47, 1, 0]  # PID 256, payload only
    packets[:, 3] = 0x10 + np.arange(PACKET_COUNT) % 16
    packets[:, 4:8] = np.arange(PACKET_COUNT, dtype='>u4').view(np.uint8).reshape(-1, 4)
    packets.tofile(directory / 'input.ts')

    standard, fec, rate = dtv.STANDARD_DVBT2, dtv.FECFRAME_NORMAL, dtv.C4_5
    qam, rotation = dtv.MOD_64QAM, dtv.ROTATION_ON
    carriers, fft, guard = dtv.CARRIERS_EXTENDED, dtv.FFTSIZE_32K, dtv.GI_1_16
    pilots, version, preamble = dtv.PILOT_PP4, dtv.VERSION_131, dtv.PREAMBLE_T2_SISO
    papr, mode = dtv.PAPR_OFF, dtv.INPUTMODE_HIEFF
    data_symbols, fft_size, ti_blocks = 63, 32768, 3
    samples_per_frame = (data_symbols + 1) * (fft_size + fft_size // 16) + 2048
    flowgraph = gr.top_block('Independent DVB-T2 integration fixture')
    chain = [
        blocks.file_source(1, str(directory / 'input.ts'), True),
        dtv.dvb_bbheader_bb(standard, fec, rate, dtv.RO_0_35, mode,
                           dtv.INBAND_OFF, FEC_BLOCKS, 4000000),
        dtv.dvb_bbscrambler_bb(standard, fec, rate),
        dtv.dvb_bch_bb(standard, fec, rate),
        dtv.dvb_ldpc_bb(standard, fec, rate, qam),
        dtv.dvbt2_interleaver_bb(fec, rate, qam),
        dtv.dvbt2_modulator_bc(fec, qam, rotation),
        dtv.dvbt2_cellinterleaver_cc(fec, qam, FEC_BLOCKS, ti_blocks),
        dtv.dvbt2_framemapper_cc(fec, rate, qam, rotation, FEC_BLOCKS, ti_blocks,
            carriers, fft, guard, dtv.L1_MOD_QPSK, pilots, 2, data_symbols,
            papr, version, preamble, mode, dtv.RESERVED_OFF,
            dtv.L1_SCRAMBLED_OFF, dtv.INBAND_OFF),
        dtv.dvbt2_freqinterleaver_cc(carriers, fft, pilots, guard,
            data_symbols, papr, version, preamble),
        dtv.dvbt2_pilotgenerator_cc(carriers, fft, pilots, guard, data_symbols,
            papr, version, preamble, dtv.MISO_TX1, dtv.EQUALIZATION_OFF,
            dtv.BANDWIDTH_8_0_MHZ, fft_size),
        digital.ofdm_cyclic_prefixer(fft_size, fft_size + fft_size // 16, 0, ''),
        dtv.dvbt2_p1insertion_cc(carriers, fft, guard, data_symbols,
            preamble, dtv.SHOWLEVELS_OFF, 3.31),
        blocks.head(gr.sizeof_gr_complex, args.frames * samples_per_frame),
        blocks.file_sink(gr.sizeof_gr_complex, str(directory / 'elementary.cf32')),
    ]
    flowgraph.connect(*chain)
    flowgraph.run()
    samples = np.fromfile(directory / 'elementary.cf32', dtype=np.complex64)
    if len(samples) != args.frames * samples_per_frame:
        raise RuntimeError('GNU Radio did not produce the complete fixture')
    # Independent conversion: 64/7 MS/s -> 10 MS/s, then signed 8-bit I/Q.
    samples = resample_poly(samples, 35, 32, window=('kaiser', 8.0))
    power = np.mean(abs(samples) ** 2)
    rng = np.random.default_rng(1226)
    sigma = np.sqrt(power / 2) * 10 ** (-args.snr / 20)
    samples += sigma * (rng.normal(size=len(samples)) + 1j * rng.normal(size=len(samples)))
    samples *= 18.4 / np.sqrt(power)
    quantized = np.rint(samples.view(np.float32))
    if np.max(abs(quantized)) > 127:
        raise RuntimeError('Fixture clips; refusing an ambiguous test signal')
    quantized.astype(np.int8).tofile(directory / 'input.cs8')
    # Acquisition intentionally discards the first T2 frame. The writer also
    # discards its initial partial TS packet; require every subsequent packet.
    first_packet = (FEC_BLOCKS * DATA_BYTES_PER_BBFRAME + 186) // 187
    end_packet = args.frames * FEC_BLOCKS * DATA_BYTES_PER_BBFRAME // 187
    metadata = dict(gnuradio=gr.version(), frames=args.frames, snr_db=args.snr,
                    sample_rate_hz=10000000, first_packet=first_packet,
                    end_packet=end_packet, expected_fec=(args.frames - 1) * FEC_BLOCKS)
    (directory / 'fixture.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps(metadata), flush=True)


def verify(args):
    metadata = json.loads((args.directory / 'fixture.json').read_text())
    source = np.fromfile(args.directory / 'input.ts', dtype=np.uint8).reshape(-1, 188)
    output = np.fromfile(args.output, dtype=np.uint8)
    first, end = metadata['first_packet'], metadata['end_packet']
    expected_bytes = (end - first) * 188
    if output.size != expected_bytes:
        raise RuntimeError(f'Incomplete TS: {output.size} bytes; expected {expected_bytes}')
    expected = source[np.arange(first, end) % len(source)].reshape(-1)
    mismatch = np.count_nonzero(output != expected)
    if mismatch:
        raise RuntimeError(f'TS differs from the independent source in {mismatch} bytes')
    print(f'PASS: {end - first} packets, {expected_bytes} exact TS bytes; no gaps or duplicates')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    generator = commands.add_parser('generate')
    generator.add_argument('directory', type=Path)
    generator.add_argument('--frames', type=int, default=5, choices=range(3, 13))
    generator.add_argument('--snr', type=float, default=20)
    verifier = commands.add_parser('verify')
    verifier.add_argument('directory', type=Path)
    verifier.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.command == 'generate':
        if not np.isfinite(args.snr) or not 0 <= args.snr <= 100:
            parser.error('--snr must be finite and between 0 and 100 dB')
        generate(args)
    else:
        verify(args)


if __name__ == '__main__':
    main()
