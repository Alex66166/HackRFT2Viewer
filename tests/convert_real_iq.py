#!/usr/bin/env python3
"""Convert known complex-float32 DVB-T2 I/Q to HackRF signed 8-bit I/Q.

Quantize both I and Q with one shared scale (never individual AGC),
without changing sample timing or frequency. The original remains intact.
"""
import argparse
import json
from pathlib import Path

import numpy as np

p = argparse.ArgumentParser()
p.add_argument("input", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--rate", type=float, default=10_000_000)
p.add_argument("--freq", type=float, default=546_004_700)
a = p.parse_args()
if not a.input.is_file() or a.input.stat().st_size % 8:
    p.error("expected nonempty interleaved complex-float32 file")
source = np.fromfile(a.input, dtype='<f4')
if source.size < 20000 or source.size % 2 or not np.isfinite(source).all():
    p.error("not a valid finite CF32 IQ stream")
peak = float(np.max(np.abs(source)))
if not 1e-7 < peak < 10_000:
    p.error(f"invalid peak: {peak}")
gain = 110.0 / peak
q = np.rint(np.clip(source * gain, -127, 127)).astype(np.int8)
a.output.parent.mkdir(parents=True, exist_ok=True)
q.tofile(a.output)
clip = np.mean(np.abs(q) >= 126)
assert clip < 0.0001
info = {
    "format": "cs8: signed int8 interleaved I,Q",
    "sample_rate_hz": a.rate,
    "frequency_hz": a.freq,
    "source": str(a.input),
    "source_format": "cf32_le",
    "quantization_gain": gain,
    "sample_count": int(source.size // 2),
    "duration_seconds": float(source.size / 2 / a.rate),
    "peak": peak,
    "clipped_percent": clip * 100,
}
a.output.with_suffix(a.output.suffix + ".json").write_text(json.dumps(info, indent=2) + "\n")
print(json.dumps(info, indent=2), flush=True)
