"""Build the full ARM64 program and preview APK with bounded workers."""
import os
import subprocess
import sys
from pathlib import Path
from development import ROOT,run

cmake=ROOT/'.venv/Scripts/cmake.exe'
code=run([str(cmake),'--build',str(ROOT/'build/android-game'),'--target','mp2r_game_code','--parallel','6'],
         ROOT/'private/logs/build-android-full.log','build')
if code: raise SystemExit(code)

sdk=Path(os.environ['LOCALAPPDATA'])/'Android/Sdk'
os.environ['JAVA_HOME']=r'C:\Program Files\Android\Android Studio\jbr'
os.environ['ANDROID_HOME']=str(sdk)
gradles=list((Path.home()/'.gradle/wrapper/dists/gradle-8.13-bin').glob('*/gradle-8.13/bin/gradle.bat'))
if len(gradles)!=1: raise SystemExit('Expected one installed Gradle 8.13 distribution')
command=['cmd.exe','/d','/c',str(gradles[0]),'-p',str(ROOT/'android'),'--no-daemon','--max-workers=2',
         ':app:assembleDebug',':app:assembleDebugAndroidTest']
raise SystemExit(run(command,ROOT/'private/logs/build-android-apk.log','build'))
