"""Build the diagnostic APK with the installed SDK/JDK and bounded workers."""
import os
import subprocess
import sys
from pathlib import Path
from development import ROOT,run

subprocess.run([sys.executable,str(ROOT/'scripts/prepare_initialization_probe.py')],cwd=ROOT,check=True)

sdk=Path(os.environ['LOCALAPPDATA'])/'Android/Sdk'
os.environ['JAVA_HOME']=r'C:\Program Files\Android\Android Studio\jbr'
os.environ['ANDROID_HOME']=str(sdk)
gradles=list((Path.home()/'.gradle/wrapper/dists/gradle-8.13-bin').glob('*/gradle-8.13/bin/gradle.bat'))
if len(gradles)!=1: raise SystemExit('Expected one installed Gradle 8.13 distribution')
command=['cmd.exe','/d','/c',str(gradles[0]),'-p',str(ROOT/'android'),'--no-daemon','--max-workers=2',
         ':app:assembleDebug',':app:assembleDebugAndroidTest']
raise SystemExit(run(command,ROOT/'private/logs/build-android-apk.log'))
