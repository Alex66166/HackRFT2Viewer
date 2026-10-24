#!/usr/bin/env python3
"""Generate reversible IQ hypotheses for a very short real broadcast recording.

Swapping and conjugating I/Q tests capture conventions (frequency inversion)
without inventing samples or changing the sample rate. The reference file is
left untouched, and results are compared in the real C++ DVB-T2 demodulator.
"""
import argparse
from pathlib import Path

import numpy as np

p=argparse.ArgumentParser()
p.add_argument("input",type=Path)
p.add_argument("directory",type=Path)
a=p.parse_args()
raw=np.fromfile(a.input,dtype=np.int8)
if raw.size<100000 or raw.size%2: p.error("Expected interleaved CS8 IQ")
iq=raw.astype(np.int16).reshape(-1,2)
a.directory.mkdir(parents=True,exist_ok=True)
hypotheses={
    "conjugate": np.column_stack((iq[:,0],-iq[:,1])),
    "swap": iq[:, ::-1],
    "swap_conjugate": np.column_stack((iq[:,1],-iq[:,0])),
}
for name, data in hypotheses.items():
    file=a.directory/(name+".cs8")
    np.clip(data,-127,127).astype(np.int8).tofile(file)
    print(name,file,file.stat().st_size,flush=True)
