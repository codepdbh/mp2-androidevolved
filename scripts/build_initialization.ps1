$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$python = Join-Path $projectRoot '.venv\Scripts\python.exe'
& $python scripts/prepare_alarm_kernel.py
if ($LASTEXITCODE -ne 0) { throw 'Alarm payload identity verification failed' }
& $python scripts/development.py --log private/logs/recompile-alarm.log -- build/ps2recomp/ps2xRecomp/Release/ps2_recomp.exe config/mp2-alarm-kernel.toml
if ($LASTEXITCODE -ne 0) { throw 'Alarm payload recompilation failed' }
& $python scripts/prepare_alarm_kernel.py --kind tlb
if ($LASTEXITCODE -ne 0) { throw 'TLB payload identity verification failed' }
& $python scripts/development.py --log private/logs/recompile-tlb.log -- build/ps2recomp/ps2xRecomp/Release/ps2_recomp.exe config/mp2-tlb-kernel.toml
if ($LASTEXITCODE -ne 0) { throw 'TLB payload recompilation failed' }
& $python scripts/prepare_initialization_probe.py --map-only
if ($LASTEXITCODE -ne 0) { throw 'Run Ghidra callback recovery before preparing the selection' }
& $python scripts/development.py --log private/logs/recompile-init-selection.log -- build/ps2recomp/ps2xRecomp/Release/ps2_recomp.exe config/mp2-initialization.toml
if ($LASTEXITCODE -ne 0) { throw 'Original initialization recompilation failed' }
& $python scripts/prepare_initialization_probe.py
if ($LASTEXITCODE -ne 0) { throw 'Generated-code provenance verification failed' }
& $python scripts/development.py --log private/logs/configure-init.log -- .venv/Scripts/cmake.exe -S . -B build/mp2r -DMP2R_INITIALIZATION_PROBE=ON
if ($LASTEXITCODE -ne 0) { throw 'Initialization configuration failed' }
& $python scripts/development.py --log private/logs/build-init.log -- .venv/Scripts/cmake.exe --build build/mp2r --config Release --target initialization_probe ee_semaphores_test ee_timers_intc_test ee_threads_test ee_tlb_test --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Initialization build failed' }
& "$projectRoot\.venv\Scripts\ctest.exe" --test-dir build/mp2r -C Release --output-on-failure -R 'initialization_probe|ee_semaphores|ee_timers_intc|ee_threads|ee_tlb'
if ($LASTEXITCODE -ne 0) { throw 'Initialization contract tests failed' }
