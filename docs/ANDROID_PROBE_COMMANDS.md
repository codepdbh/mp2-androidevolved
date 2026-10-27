# Reproducir la prueba ARM64 aislada

Tras generar C++ privado y verificar los bytes PAL, usar PowerShell:

```powershell
$ndkPath = "$env:LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358"
$ninjaPath = "$((Get-Location).Path)/.venv/Scripts/ninja.exe"
.\.venv\Scripts\python.exe scripts/development.py --log private/logs/configure-arm64-probe.log -- .venv/Scripts/cmake.exe -S . -B build/android-input -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaPath" "-DCMAKE_TOOLCHAIN_FILE=$ndkPath/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release -DMP2R_RECOMP_PROBE=ON
.\.venv\Scripts\python.exe scripts/development.py --log private/logs/build-arm64-probe.log -- .venv/Scripts/cmake.exe --build build/android-input --parallel 2
$adbPath = "$env:LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
& $adbPath devices
# Elegir un serial conectado y comprobar ABI antes de copiar.
# Sustituir SERIAL por ese dispositivo; no ejecutar sobre todos los dispositivos.
& $adbPath -s SERIAL shell getprop ro.product.cpu.abilist
& $adbPath -s SERIAL push build/android-input/recomp_leaf_probe /data/local/tmp/mp2r-recomp-leaf-probe
& $adbPath -s SERIAL shell chmod 700 /data/local/tmp/mp2r-recomp-leaf-probe
& $adbPath -s SERIAL shell /data/local/tmp/mp2r-recomp-leaf-probe
```

Resultado observado en esta iteración (transcripción, no gameplay):

```text
arm64-v8a
4096  [getconf PAGE_SIZE]
PASS: two original PAL leaf translations, 12 register/return cases; ADDIU delay slot and LUI. No game boot.
```

El binario quedó en esa ruta temporal del dispositivo para reproducibilidad;
no se instaló aplicación. No contiene el juego completo y no debe distribuirse.
