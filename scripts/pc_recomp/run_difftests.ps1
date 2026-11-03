# Prueba diferencial sin ventanas: lift32 contra Unicorn por instrucción y por función real.
#   run_difftests.ps1 [-Count 300] [-Tags exe,x_physicalsimulationmfc,...]
param([int]$Count=300,[string[]]$Tags=@('exe','rlmfc','x_physicalsimulationmfc','x_gameobjectsmfc','e2_d3d8_driver_mfc'))
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$py="$root\.venv\Scripts\python.exe"
$modules=(Get-Content "$root\config\pc\modules.json" -Raw -Encoding UTF8 | ConvertFrom-Json).modules
$out="$root\work\pc\difftest"
New-Item -ItemType Directory -Force $out | Out-Null
$lines=@('@echo off',"call `"$vs\VC\Auxiliary\Build\vcvarsall.bat`" x64 >nul")
$seed=11
foreach($tag in ($Tags -split ',')){
  $m=$modules | Where-Object tag -eq $tag
  $image="$root\work\pc\guest\$(Split-Path -Leaf $m.file)"
  $lines+="`"$py`" `"$root\scripts\pc_recomp\difftest_real.py`" `"$image`" `"$root\work\pc\modules\$tag\catalog.json`" --n $Count --seed $seed --out `"$out\$tag`""
  $lines+="echo DIFFTEST_DONE $tag rc=%errorlevel%"
  $seed++
}
[IO.File]::WriteAllLines("$out\run.cmd",$lines,[Text.Encoding]::ASCII)
& "$out\run.cmd"
