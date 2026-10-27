# Carpeta de datos solicitada

Ubicación obligatoria: **Almacenamiento interno/mp2androidevolved**.
Ruta del dispositivo probado: `/storage/emulated/0/mp2androidevolved`;
`/sdcard/mp2androidevolved` apunta a esa misma ubicación.
Es la raíz del almacenamiento compartido, no `Android/data` ni la raíz del sistema.

La extracción debe quedar directamente dentro: SYSTEM.CNF, SLES_522.56, DATA.RAS,
ENG/, OTH/, módulos IRX y vídeos PSS. No colocar otra carpeta `extracted` en medio.
El nombre exacto del ELF se obtiene de BOOT2; Java bloquea rutas fuera del directorio,
calcula SHA-256 y comprueba el ELF PAL de referencia. JNI recibe la misma raíz y
abre su cabecera con C++: lectura ELF32/MIPS y entrada0x100008 comprobadas.
Esto verifica acceso e identidad; no significa que todos los recursos estén cargados.

Desde la iteración3, la APK v0.2 lee una instantánea completa del ELF de BOOT2,
comprueba tamaño y SHA-256 en Java y entrega esos mismos bytes a JNI. El cargador
C++ valida estructura/límites ELF y copia los segmentos originales a la RAM invitada
del ensayo de inicialización. No se ejecuta otro ELF oculto empaquetado en la APK.
Los cuerpos nativos corresponden al ELF fijado; su traducción se conserva privada.
Los recursos de niveles y audio aún no están conectados a este cargador.

Se transfirieron **504 archivos, 3990041090 bytes**, a la carpeta nueva del teléfono.
La comprobación `sha256sum -c mp2r-sha256.txt` dio 504 resultados OK. Evidencia privada:
`private/logs/deploy-phone-data.log` y `private/logs/verify-phone-data.log`.
No se modificó la ISO ni la extracción original. El script rechaza sobrescribir una
carpeta existente; `--verify-only` comprueba archivos ya transferidos.

Android11+ requiere acceso especial a todos los archivos para estas rutas directas.
La app ofrece “Acceso a datos”, abre ajustes de su propio paquete y comprueba
`Environment.isExternalStorageManager()`. En el dispositivo de desarrollo se habilitó
el permiso para **com.mp2r.android** mediante el comando de pruebas documentado por
[Android](https://developer.android.com/training/data-storage/manage-all-files).
En Android8–10 solicita permisos de almacenamiento y usa la ruta compartida heredada.
Esta rama antigua está compilada pero no se probó en un teléfono antiguo.

```powershell
.\.venv\Scripts\python.exe scripts/deploy_game_data.py --serial SERIAL
.\.venv\Scripts\python.exe scripts/deploy_game_data.py --serial SERIAL --verify-only
```

La APK no empaqueta los 4GB de archivos comerciales. Los datos permanecen en la
carpeta solicitada. El HUD y el informe diagnóstico usan preferencias/files de la app.
