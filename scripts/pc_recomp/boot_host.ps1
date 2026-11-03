# Compila el host y lo arranca sin ventanas, bajo los límites de scripts/development.py.
#   boot_host.ps1 -Log nombre [-NoBuild] [-Trace] [-AllowWindow] [-Frames N]
param([string]$Log='boot',[switch]$NoBuild,[switch]$Trace,[switch]$AllowWindow,[int]$Frames=0,[string]$InputScript='',[int]$CaptureEvery=0,[string]$Profile='analysis',[int]$Seconds=0,[switch]$Widescreen)
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
$py='.\.venv\Scripts\python'
if(!$NoBuild){
  & $py scripts\development.py --log work\pc\logs\build_host.log --profile build -- powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\pc_recomp\build_host.ps1 -Jobs 4
  "build rc=$LASTEXITCODE"
  Select-String -Path work\pc\logs\build_host.log -Pattern ' error |FAILED|LNK[0-9]' | Select-Object -First 15 | ForEach-Object { $_.Line.Substring(0,[Math]::Min(220,$_.Line.Length)) }
}
$extra=@('--registry','config\pc\default_registry.txt'); if($Trace){$extra+='--trace-native'}; if($AllowWindow){$extra+='--allow-window'}; if($Frames){$extra+='--stop-after-frames',$Frames}; if($InputScript){$extra+='--input-script',$InputScript}; if($CaptureEvery){$extra+='--capture-every',$CaptureEvery}; if($Seconds){$extra+='--stop-after-seconds',$Seconds}; if($Widescreen){$extra+='--widescreen'}
& $py scripts\development.py --log "work\pc\logs\$Log.log" --profile $Profile -- work\pc\build-host\mp2_pc_host.exe --game-root "Max Payne 2 The Fall of Max Payne" --guest-dir work\pc\guest --sandbox work\pc\sandbox @extra
"boot rc=$LASTEXITCODE (0x{0:X})" -f $LASTEXITCODE
$l=Get-Content "work\pc\logs\$Log.log" -Encoding UTF8
"lineas de log: $($l.Count)"
$l | Where-Object { $_ -match '^\[MP2:(LOAD|POLICY|FILE|SEH|WIN|D3D|WSF)|MILESTONE|entry point|línea de comandos' } | Select-Object -Last 60
$i=($l | Select-String '^==== MP2' | Select-Object -First 1).LineNumber
if($i){ $l[($i-1)..([Math]::Min($i+34,$l.Count-1))] } else { $l | Select-Object -Last 12 }
