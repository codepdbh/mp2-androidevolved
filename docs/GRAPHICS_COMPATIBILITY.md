# Gráficos: Direct3D 8 a Vulkan

Estado en Windows: funciona sobre `d3d8.dll` del sistema, con el motor y el driver recompilados. **En Android, sin implementar.**

## Lo que usa el juego

De las cadenas e imports de `e2_d3d8_driver_mfc.dll`:

- Carga `D3D8.DLL` con `LoadLibraryA` y resuelve `Direct3DCreate8`. Las tablas de imports no lo muestran.
- D3DX está enlazado estáticamente (`D3DXCreateCubeTextureFromFileInMemoryEx`, `D3DXGetErrorString`), así que se recompila con el driver.
- Crea vertex y pixel shaders (`DRV_createVertexShader`, `DRV_createPixelShader`), vertex e index buffers, render targets y mapas cúbicos comprimidos. Maneja `D3DERR_DEVICELOST` y `DEVICENOTRESET`.
- Ventana y pantalla completa pasan por `P_D3D::DRV_setWindow` y `DRV_setFullscreen`: el punto natural para forzar el modo ventana en PC.
- Bink: el driver importa `_BinkOpen@8`, `_BinkCopyToBufferRect@44` y `_BinkOpenDirectSound@4` de `binkw32.dll`. Se recompila como módulo.
- D3DX contiene ramas 3DNow! (2 874 instrucciones sin modelar). El host las evita quitando 3DNow! de CPUID, igual que los ports de NFS.

## Uso medido

`desktop/pc_host/mp2_d3d8_trace.c` escribe `work/pc/sandbox/d3d8_usage.txt`. Ejecución hasta el primer nivel, 7 300 frames:

- **Dibujo:** `DrawIndexedPrimitive` 416 528, `DrawPrimitiveUP` 4 272. Ningún `DrawPrimitive`.
- **Fijo más shaders:** `SetLight` 108 504, `LightEnable` 868 528, `SetMaterial` 352 177 y `SetClipPlane` 52 551 conviven con solo 8 vertex shaders y 6 pixel shaders creados. La cobertura del pipeline fijo importa más que la de shaders.
- **Destino de render:** `SetRenderTarget` 16 470, `CopyRects` 102.
- **Formatos de textura:** `DXT1`, `DXT3`, `DXT5`, X8R8G8B8, R5G6B5, A4R4G4B4, A8R8G8B8. Cúbicas en `DXT1`.
- **Dispositivo:** 640×480 X8R8G8B8, un back buffer, D24S8, sin MSAA, `D3DSWAPEFFECT_DISCARD`.

Solo cubre hasta ese punto del juego: niveles posteriores pueden añadir estados. El vídeo Bink se dibuja en 6 texturas X8R8G8B8 con `DrawPrimitiveUP`.

## API medida frente al runtime de Carbon

`scripts/pc_analysis/api_gap.py` cruza `work/pc/sandbox/api_usage.txt` con las tablas de `guest_*.inc`. Hasta el primer nivel, el juego llama a unas 300 funciones nativas, no a las más de 700 que importa. Faltan en Carbon, además de D3D8, unas 18 de KERNEL32 y 24 de USER32, todas sencillas. DirectSound y DirectInput no se pudieron cruzar por nombre, porque se llaman por vtable.

## Alternativas

**Recomendada: D3D8 → D3D9 → DXVK, con el frontend `src/d3d8` de DXVK actual.** Reutiliza el puente COM de 32 bits y `ports/dxvk_android`, ya probados en Adreno y Mali.

El obstáculo verificado es que el árbol de Android usa `dxvk-native 1.9.2b`, que solo tiene `src/d3d9`. D8VK se integró en DXVK más tarde. Portarlo a la versión antigua es una tarea de tamaño desconocido hasta intentarlo: su interfaz con D3D9 puede haber cambiado.

**Alternativa: actualizar DXVK en Android.** Da el frontend sin retroportarlo, pero obliga a rehacer los parches de `prepare.py` y a revalidar GPUs. Afecta a una base probada.

**Último recurso: backend propio** con el subconjunto medido. Solo si las dos anteriores fallan.

## Política de escritorio

El juego tiene su propio flag `-window`. El host, además, fuerza `Windowed=1` en `CreateDevice` y `Reset`, y crea la ventana de 960×540 aunque el juego pida otra cosa. El back buffer sigue a 640×480, escalado a la ventana. Ver `docs/WINDOWED_TEST_POLICY.md`.

## Precaución con DllMain

`e2_d3d8_driver_mfc.dll` se carga con `LoadLibrary` desde el constructor de `P_Device`, no desde un `DllMain`. El mensaje `D3D driver dllMain - %s` indica trabajo dentro de `DllMain`: falta comprobar si incluye crear objetos D3D y si eso puede provocar el bloqueo del cargador.
