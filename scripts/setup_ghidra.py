"""Install a pinned Ghidra + matching R5900 extension locally, never start a UI."""
import hashlib
import json
from pathlib import Path
import urllib.request
import subprocess
from concurrent.futures import ThreadPoolExecutor
import zipfile

ROOT=Path(__file__).resolve().parents[1]
PACKAGES=[
    ('ghidra_12.1.4_PUBLIC_20260921.zip',
     'https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip',
     'ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db'),
    ('ghidra_12.1.4_PUBLIC_20261004_ghidra-emotionengine-reloaded.zip',
     'https://github.com/chaoticgd/ghidra-emotionengine-reloaded/releases/download/v2.1.38/ghidra_12.1.4_PUBLIC_20261004_ghidra-emotionengine-reloaded.zip',
     'e389ff3e921d9f7d4db2ece9906f808545ce4ff519d9e25a019b72ea509f240b')]

def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()

downloads=ROOT/'external/downloads'
downloads.mkdir(parents=True,exist_ok=True)
for name,url,expected in PACKAGES:
    dest=downloads/name
    if not dest.exists():
        tmp=dest.with_suffix('.part')
        if name==PACKAGES[0][0]:
            length=569649598
            block=8*1024*1024
            def fetch(offset):
                end=min(offset+block,length)-1
                part=downloads/(name+f'.range-{offset}')
                if not part.exists() or part.stat().st_size!=end-offset+1:
                    subprocess.run(['curl.exe','--http1.1','--fail','--location','--silent','--show-error',
                        '--connect-timeout','10','--max-time','30','--retry','1',
                        '--range',f'{offset}-{end}','--output',str(part),url],check=True)
                if part.stat().st_size!=end-offset+1: raise ValueError('Wrong range length')
                return part
            with ThreadPoolExecutor(max_workers=2) as pool:
                parts=list(pool.map(fetch,range(0,length,block)))
            with tmp.open('wb') as f:
                for part in parts:
                    with part.open('rb') as source:
                        while chunk:=source.read(1024*1024): f.write(chunk)
        else:
            subprocess.run(['curl.exe','--fail','--location','--silent','--show-error',
                            '--connect-timeout','20','--max-time','60','--output',str(tmp),url],check=True)
        if sha(tmp)!=expected: raise ValueError('Download digest mismatch: '+name)
        tmp.rename(dest)
        if name==PACKAGES[0][0]:
            for part in downloads.glob(name+'.range-*'):
                if not part.resolve().is_relative_to(downloads.resolve()): raise ValueError('Unsafe cleanup path')
                part.unlink()
    if sha(dest)!=expected: raise ValueError('Digest mismatch: '+name)
    print('Verified',name,flush=True)
    target=ROOT/'external' if name==PACKAGES[0][0] else ROOT/'external/ghidra_12.1.4_PUBLIC/Ghidra/Extensions'
    target.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(dest) as z:
        for member in z.infolist():
            path=target/member.filename
            if not path.resolve().is_relative_to(target.resolve()): raise ValueError('Unsafe ZIP path')
        z.extractall(target)
    print('Extracted into',target,flush=True)
(ROOT/'config/ghidra-packages.lock.json').write_text(json.dumps([
    dict(name=n,url=u,sha256=h) for n,u,h in PACKAGES],indent=2)+'\n',encoding='utf-8')
