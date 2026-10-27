"""Copy the private extraction to the requested Android root folder, then hash it.

Refuses to overwrite an existing folder. --verify-only never copies game files.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
from development import ROOT

p=argparse.ArgumentParser()
p.add_argument('--serial',required=True)
p.add_argument('--verify-only',action='store_true')
a=p.parse_args()
adb=Path(os.environ['LOCALAPPDATA'])/'Android/Sdk/platform-tools/adb.exe'
base=[str(adb),'-s',a.serial]
target='/sdcard/mp2androidevolved'
manifest=json.loads((ROOT/'private/disc/iso_manifest.json').read_text())
def call(args,**kw): return subprocess.run(base+args,check=True,timeout=240,**kw)
devices=subprocess.check_output([str(adb),'devices'],text=True)
if f'{a.serial}\tdevice' not in devices: raise SystemExit('Selected device is not connected/authorized')
checksum=ROOT/'private/disc/mp2r-sha256.txt'
checksum.write_text(''.join(f"{row['sha256']}  {row['path']}\n" for row in manifest['files']),encoding='ascii',newline='\n')
if not a.verify_only:
    exists=subprocess.run(base+['shell','test','-e',target],timeout=10).returncode==0
    if exists: raise SystemExit('Target already exists. Refusing overwrite; use --verify-only to check it.')
    available=subprocess.check_output(base+['shell','df','-k','/sdcard'],text=True).splitlines()[-1].split()[3]
    needed=sum(row['size'] for row in manifest['files'])+100*1024*1024
    if int(available)*1024<needed: raise SystemExit('Insufficient device storage')
    call(['shell','mkdir',target])
    children=sorted((ROOT/'private/disc/extracted').iterdir())
    call(['push',*[str(path) for path in children],target+'/'])
call(['push',str(checksum),target+'/mp2r-sha256.txt'])
with (ROOT/'private/logs/verify-phone-data.log').open('w',encoding='utf-8') as log:
    call(['shell',f'cd {target} && sha256sum -c mp2r-sha256.txt'],stdout=log,stderr=subprocess.STDOUT)
print(f'Verified {len(manifest["files"])} original files in /storage/emulated/0/mp2androidevolved')
