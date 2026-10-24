#!/usr/bin/env python3
"""Inspect an independently recorded 2015 Russian DVB-T2 IQ archive.
Do not assume that a .cpx file is HackRF CS8: compare all known raw encodings.
This probe never executes archived files and does not classify by extension.
"""
import argparse, json, pathlib
import numpy as np

parser=argparse.ArgumentParser()
parser.add_argument("directory",type=pathlib.Path)
args=parser.parse_args()
for path in sorted(args.directory.rglob("*")):
    if not path.is_file():
        continue
    size=path.stat().st_size
    print("FILE",path.relative_to(args.directory),"bytes",size,"hex",path.open("rb").read(32).hex(),flush=True)
    if size < 1024 or size > 512_000_000:
        continue
    raw=path.open("rb").read(min(size,2_000_000))
    for name,dt in (("cs8","i1"),("cu8","u1"),("ci16-le","<i2"),("ci16-be",">i2"),("cf32-le","<f4"),("cf32-be",">f4"),("cf64-le","<f8")):
        a=np.frombuffer(raw[:len(raw)//np.dtype(dt).itemsize*np.dtype(dt).itemsize],dtype=dt)
        if a.size<2000 or a.size%2: continue
        a=a.astype(np.float64)
        finite=np.isfinite(a)
        if finite.mean()<0.999: continue
        z=a[0::2]+1j*a[1::2]
        mag=np.abs(z)
        if name=="cu8": z-=128+128j;mag=np.abs(z)
        print("FORMAT",name,"mean",round(float(abs(np.mean(z))),5),"rms",round(float(np.sqrt(np.mean(mag*mag))),5),
              "p99",round(float(np.quantile(mag,.99)),5),"max",round(float(mag.max()),5),
              "zero%",round(float(np.mean(mag==0)*100),3),flush=True)
