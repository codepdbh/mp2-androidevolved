"""Update/test only the explicitly selected MP2R device and package."""
import argparse
import os
from pathlib import Path
import subprocess
from development import ROOT

p=argparse.ArgumentParser()
p.add_argument('--serial',required=True)
a=p.parse_args()
adb=Path(os.environ['LOCALAPPDATA'])/'Android/Sdk/platform-tools/adb.exe'
base=[str(adb),'-s',a.serial]
if f'{a.serial}\tdevice' not in subprocess.check_output([str(adb),'devices'],text=True):
    raise SystemExit('Selected device is not connected/authorized')
for relative in ['android/app/build/outputs/apk/debug/app-debug.apk',
                 'android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk']:
    subprocess.run(base+['install','-r',str(ROOT/relative)],check=True,timeout=60)
result=subprocess.run(base+['shell','am','instrument','-w','com.mp2r.android.test/com.mp2r.android.TouchInstrumentation'],
                      text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
(ROOT/'private/logs/android-instrumentation.log').write_text(result.stdout,encoding='utf-8')
print(result.stdout)
if result.returncode!=0 or 'INSTRUMENTATION_RESULT: passed=true' not in result.stdout:
    raise SystemExit('Android instrumentation failed; see private/logs/android-instrumentation.log')
native=subprocess.check_output(base+['shell','run-as','com.mp2r.android','cat','files/diagnostic.log'],text=True,timeout=10)
(ROOT/'private/logs/android-native-initialization.log').write_text(native,encoding='utf-8')
if not native.startswith('PASS:') or 'pc=0x10d698' not in native or 'switches=2' not in native or 'waits=1' not in native:
    raise SystemExit('App diagnostic did not record the current initialization milestone')
print('PASS: installed APK native trace confirms the original initialization milestone')
