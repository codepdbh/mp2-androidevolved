$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$python = Join-Path $projectRoot '.venv\Scripts\python.exe'
& $python scripts/bootstrap_dependencies.py --restore
if ($LASTEXITCODE -ne 0) { throw 'Dependency verification failed' }
$cmakeArgs = @('-S','external/PS2Recomp','-B','build/ps2recomp',
    '-DPS2X_BUILD_RUNTIME=OFF','-DPS2X_BUILD_TEST=OFF','-DPS2X_BUILD_STUDIO=OFF',
    '-DTOML11_BUILD_TESTS=OFF',"-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$projectRoot/external/json")
foreach ($name in @('elfio','toml11','fmt','libdwarf','rabbitizer')) {
    $cmakeArgs += "-DFETCHCONTENT_SOURCE_DIR_$($name.ToUpperInvariant())=$projectRoot/build/ps2recomp/_deps/$name-src"
}
& $python scripts/development.py --log private/logs/configure-ps2recomp.log -- .venv/Scripts/cmake.exe @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw 'Tool configuration failed' }
$jobs = & $python -c "import sys; sys.path.insert(0,'scripts'); from development import policy; print(policy()['analysis']['jobs'])"
& $python scripts/development.py --log private/logs/build-ps2recomp.log -- .venv/Scripts/cmake.exe --build build/ps2recomp --config Release --target ps2_recomp ps2_analyzer --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw 'Tool build failed; inspect private/logs/build-ps2recomp.log' }
