# Icono de aplicación

Imagen aportada por el usuario: `android/icono/icono.png`, 1254×1254 RGBA.
Se conserva intacta y se copia a
`android/app/src/main/res/drawable-nodpi/mp2r_launcher_art.png`.

El manifiesto referencia `@mipmap/ic_launcher` y `@mipmap/ic_launcher_round`.
En Android26+ ambos utilizan un icono adaptable con fondo oscuro e imagen al60%
del lienzo de foreground (inset20%) para conservar la composición y el título
al aplicar máscaras de launcher. No se regeneró ni retocó el diseño del usuario.

Validación: hashes SHA-256 de origen/copia idénticos; build Gradle correcta;
`aapt2 dump badging` confirma la referencia al icono adaptable dentro del APK.
La APK actualizada se instaló en el teléfono conectado conservando datos y ajustes.
