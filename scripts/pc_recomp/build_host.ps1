# Compila el host de validación de MP2 con MSVC x86. Sin ventanas ni pruebas gráficas.
param([int]$Jobs=4)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(!$vs){throw 'No se encontraron las herramientas MSVC x86'}
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$build="$root/work/pc/build-host"
New-Item -ItemType Directory -Force $build | Out-Null
$batch=@(
  '@echo off',
  "call `"$vs/VC/Auxiliary/Build/vcvarsall.bat`" x86 >nul",
  'if errorlevel 1 exit /b 1',
  "`"$cmake`" -S `"$root/desktop/pc_host`" -B `"$build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_C_COMPILER=cl -DCMAKE_BUILD_TYPE=Release",
  'if errorlevel 1 exit /b 1',
  "`"$cmake`" --build `"$build`" --parallel $Jobs -- -k 0",
  'exit /b %errorlevel%'
)
[IO.File]::WriteAllLines("$build/build.cmd",$batch,[Text.Encoding]::ASCII)
$ErrorActionPreference='Continue'
& "$build/build.cmd"
exit $LASTEXITCODE
