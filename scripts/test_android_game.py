"""Bounded full-program device smoke test. Never uninstalls or clears app/game data."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--serial',required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--apk',type=Path)
args=parser.parse_args()
adb=Path(os.environ['LOCALAPPDATA'])/'Android/Sdk/platform-tools/adb.exe'
def call(*command,check=True):
    return subprocess.run([str(adb),'-s',args.serial,*command],stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=check,timeout=60)
args.output.mkdir(parents=True,exist_ok=True)
model=call('shell','getprop','ro.product.model').stdout.decode().strip()
if model!='SM-S938B': raise SystemExit(f'Expected authorized S25 Ultra SM-S938B, got {model}')
identity=call('shell','sha256sum','/sdcard/mp2androidevolved/SLES_522.56').stdout.decode()
if not identity.startswith('9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba '):
    raise SystemExit('PAL ELF identity mismatch on device')
if args.apk:
    install=call('install','-r',str(args.apk.resolve()))
    (args.output/'install.txt').write_bytes(install.stdout+install.stderr)
call('shell','input','keyevent','KEYCODE_WAKEUP')
start=call('shell','am','start','-W','-n','com.mp2r.android/.GameActivity')
pid=call('shell','pidof','com.mp2r.android',check=False).stdout.decode().strip().split()
(args.output/'start.txt').write_bytes(start.stdout+start.stderr)
for n in range(4):
    time.sleep(15)
    (args.output/f'frame-{n}.png').write_bytes(call('exec-out','screencap','-p').stdout)
    (args.output/f'activity-{n}.txt').write_bytes(call('shell','dumpsys','activity','activities').stdout)
# Escape invokes the app's explicit exit -> cooperative stop and retained log.
call('shell','input','keyevent','KEYCODE_ESCAPE')
for n in range(10):
    time.sleep(1)
    log=call('exec-out','run-as','com.mp2r.android','cat','files/game.log',check=False).stdout
    if b'RESULT reason=' in log: break
(args.output/'game.log').write_bytes(log)
if pid: (args.output/'logcat.txt').write_bytes(call('logcat','--pid='+pid[0],'-d','-t','1500','-v','threadtime').stdout)
(args.output/'device.json').write_text(json.dumps({'serial':args.serial,'model':model,'elf':identity.strip()},indent=2),encoding='utf-8')
if b'RESULT reason=' not in log: raise SystemExit('Native session did not stop cleanly; inspect device captures and logcat')
print(log.decode('utf-8',errors='replace').splitlines()[-1])
