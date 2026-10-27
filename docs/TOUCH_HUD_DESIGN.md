# MP2 Touch Engine — primera iteración

Actualización iteración 2: vista Canvas Android propia y JNI por lotes implementados.
Se probaron MotionEvent con IDs independientes, movimiento+disparo, UP parcial,
CANCEL global, anclaje de cámara sin salto, edición persistente y cambio de aspecto.
Diez imágenes PNG originales con alpha real sustituyen texto en botones; el joystick
usa una palanca visual. Los nombres aparecen en el editor y se conservan como
metadatos de acciones; no se afirma accesibilidad completa con nodos virtuales.
No se han identificado bindings del juego ni estados reales de Bullet Time/Shootdodge.
Véase [HUD_ASSETS.md](HUD_ASSETS.md). El texto siguiente conserva el diseño inicial.

Estado: IMPLEMENTACIÓN PARCIAL. Núcleo C++ sin interfaz ni enlace al juego.

Flujo previsto: eventos Android por ID de dedo → contactos semánticos → snapshot
sincronizado por frame → mapeador PAL validado → mando PS2 virtual → runtime.
No se han inferido botones PS2 ni escrito direcciones de memoria del juego.

El núcleo conserva MOVE/LOOK/FIRE/AIM/RELOAD/JUMP/INTERACT/BULLET_TIME/
SHOOTDODGE/CHANGE_WEAPON/USE_PAINKILLER/PAUSE. Las pulsaciones rápidas conservan
ambos flancos hasta consumir el snapshot. Cancelación global libera todos los dedos.
Bullet Time y Shootdodge son eventos distintos; no implementan sus mecánicas.

Diseño previsto: joystick inferior izquierdo, área de cámara derecha, disparo grande,
Bullet Time y Shootdodge separados, recarga e interacción contextual, selector compacto
de armas. Coordenadas normalizadas dentro de los insets seguros; perfiles clásico,
moderno, zurdo, tablet y mando mediante configuración versionada. Persistencia/editor,
gamepad, cambio automático de dispositivo y conexión JNI están pendientes.
Los indicadores deben proceder del estado real del juego; no duplicar su HUD sin evaluar.

Contextos previstos: menú, gameplay, Bullet Time, Shootdodge, cinemática, pausa,
selección de armas, inventario y carga. No se conocen todavía sus fuentes de estado.

Referencias primarias consultadas:

- [Rockstar: lanzamiento Android](https://www.rockstargames.com/newswire/article/9k124883897537/max-payne-mobile-coming-for-android-this-thursday-june-14th.html): personalización y mandos.
- [Rockstar: sensibilidad](https://support.rockstargames.com/articles/6NtpjmsxgKLKhfasvgm8Oa/how-to-adjust-look-sensitivity-in-max-payne-for-ios): ejes horizontal/vertical independientes.
- [Ficha oficial Android](https://play.google.com/store/apps/details?id=com.rockstar.maxpayne).

La inspección visual detallada de capturas/vídeos está pendiente. No se extrajeron
recursos del APK ni se presenta este diseño como gameplay.
