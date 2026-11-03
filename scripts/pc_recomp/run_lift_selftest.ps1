# Prueba diferencial por instrucción de lift32 contra Unicorn (necesita LLVM y MSVC x86).
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$out="$root\work\pc\selftest"
New-Item -ItemType Directory -Force $out | Out-Null
[IO.File]::WriteAllLines("$out\clang32.bat",@('@echo off','"C:\Program Files\LLVM\bin\clang.exe" --target=i686-pc-windows-msvc %*'),[Text.Encoding]::ASCII)
$lines=@('@echo off',"call `"$vs\VC\Auxiliary\Build\vcvarsall.bat`" x86 >nul","set RECOMP_CC=$out\clang32.bat","cd /d `"$root\external\pcrecomp\tools\lift`"","`"$root\.venv\Scripts\python.exe`" difftest.py")
[IO.File]::WriteAllLines("$out\run.cmd",$lines,[Text.Encoding]::ASCII)
& "$out\run.cmd"
