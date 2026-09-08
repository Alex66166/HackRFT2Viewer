#!/usr/bin/env python3
"""Package an already built Windows app and the official, hash-checked VLC ZIP."""
from pathlib import Path
import argparse, hashlib, json, shutil, zipfile
p=argparse.ArgumentParser();p.add_argument('--vlc-zip',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parent;version='1.4.0';a.output.mkdir(parents=True,exist_ok=True)
assert hashlib.sha256(a.vlc_zip.read_bytes()).hexdigest()=='992d19dbd0b8a7cde9167d2f7780b1ef6f92acc8a71acfa736101a21f35181e1','Unexpected VLC binary archive'
source=a.output/f'HackRFT2Viewer-Source-{version}.zip'
with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED,6) as z:
    for path in sorted(root.rglob('*')):
        rel=path.relative_to(root)
        if not path.is_file():continue
        if any(x in {'build-native','build-release','build-win','.git','__pycache__'} for x in rel.parts):continue
        if path.suffix in {'.o','.d','.tmp','.log'} or path.name in {'settings.ini','frontend-benchmark'}:continue
        z.write(path,Path('HackRFT2Viewer')/rel)
dist=root/'build-win/dist';assert (dist/'HackRFT2Viewer.exe').is_file(),'Build Windows first'
portable=a.output/f'HackRFT2Viewer-Portable-{version}.zip';prefix=f'HackRFT2Viewer-Portable-{version}'
with zipfile.ZipFile(portable,'w',zipfile.ZIP_DEFLATED,6) as z:
    for path in sorted(dist.rglob('*')):
        if path.is_file():z.write(path,f'{prefix}/{path.relative_to(dist).as_posix()}')
    for name in ['README-RU.md','VALIDATION.md','CHANGELOG.md','THIRD-PARTY-NOTICES.md','HACKRF-LICENSE.txt','COPYING']:
        if (root/name).is_file():z.write(root/name,f'{prefix}/{name}')
    for path in sorted((root/'licenses').rglob('*')):
        if path.is_file():z.write(path,f'{prefix}/licenses/{path.relative_to(root/"licenses").as_posix()}')
    z.write(source,f'{prefix}/Source-{version}.zip')
    with zipfile.ZipFile(a.vlc_zip) as vlc:
        for info in vlc.infolist():
            if not info.is_dir():z.writestr(f'{prefix}/vlc/{info.filename.split("/",1)[1]}',vlc.read(info))
checks=a.output/f'SHA256-{version}.txt'
checks.write_text(''.join(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n' for path in [portable,source]),encoding='utf-8')
for path in [portable,source]:
    with zipfile.ZipFile(path) as z:assert z.testzip() is None
print(json.dumps([{'path':str(x.resolve()),'bytes':x.stat().st_size} for x in [portable,source,checks]],indent=2))
