package com.mp2r.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Environment;
import android.util.Log;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;

/** Ajustes del launcher, guardados entre sesiones y aplicados antes de arrancar el juego. */
final class GameOptions {
    int renderScale = 100;
    int frameLimit = 60;
    boolean widescreen = true;
    float fieldOfView = 1.0f;
    boolean widescreenHud = true;
    int cutsceneBorders = 2;
    boolean comicsFullscreen = true;
    boolean skipIntro = true;
    boolean fastLoad = false;
    float lookSensitivity = 0.45f;
    int hudOpacity = 65;
    /** es, en, pt: el launcher siempre; el juego, si la instalación trae ese idioma. */
    String language = "es";

    static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences("mp2_opciones", Context.MODE_PRIVATE);
    }

    static GameOptions load(Context context) {
        SharedPreferences p = preferences(context);
        GameOptions o = new GameOptions();
        o.renderScale = p.getInt("escala", o.renderScale);
        o.frameLimit = p.getInt("fps", o.frameLimit);
        o.widescreen = p.getBoolean("panoramico", o.widescreen);
        o.fieldOfView = p.getFloat("fov", o.fieldOfView);
        o.widescreenHud = p.getBoolean("hudPanoramico", o.widescreenHud);
        o.cutsceneBorders = p.getInt("bordes", o.cutsceneBorders);
        o.comicsFullscreen = p.getBoolean("comics", o.comicsFullscreen);
        o.skipIntro = p.getBoolean("saltarIntro", o.skipIntro);
        o.fastLoad = p.getBoolean("cargaRapida", o.fastLoad);
        o.lookSensitivity = p.getFloat("sensibilidad", o.lookSensitivity);
        o.hudOpacity = p.getInt("opacidad", o.hudOpacity);
        o.language = p.getString("idioma", o.language);
        return o;
    }

    void save(Context context) {
        preferences(context).edit()
            .putInt("escala", renderScale).putInt("fps", frameLimit).putBoolean("panoramico", widescreen)
            .putFloat("fov", fieldOfView).putBoolean("hudPanoramico", widescreenHud).putInt("bordes", cutsceneBorders)
            .putBoolean("comics", comicsFullscreen).putBoolean("saltarIntro", skipIntro).putBoolean("cargaRapida", fastLoad)
            .putFloat("sensibilidad", lookSensitivity).putInt("opacidad", hudOpacity).putString("idioma", language).apply();
    }

    /** Opciones de la línea de órdenes del propio juego (medidas en MaxPayne2.exe). */
    String gameFlags() {
        StringBuilder flags = new StringBuilder();
        if (skipIntro) flags.append("-skipstartup ");
        if (fastLoad) flags.append("-disable3dpreloads ");
        return flags.toString().trim();
    }

    /** El motor carga todos los *.RAS de la carpeta: se ocultan los de otros idiomas. */
    String hiddenLanguageFiles() {
        StringBuilder hidden = new StringBuilder();
        if (!language.equals("es")) hidden.append("MP2_Spanish.ras;");
        if (!language.equals("pt")) hidden.append("MP2_Portuguese.ras;");
        if (!language.equals("en")) hidden.append("MP2_English.ras;");
        return hidden.toString();
    }

    /** Textos de la interfaz. */
    String tr(String spanish, String english, String portuguese) {
        return language.equals("en") ? english : language.equals("pt") ? portuguese : spanish;
    }

    /** Escribe en el .ini del WidescreenFix de la carpeta del juego solo los valores del launcher. */
    void applyWidescreenIni() {
        File ini = new File(Environment.getExternalStorageDirectory(), "mp2/scripts/MaxPayne2.WidescreenFix.ini");
        try {
            String text = new String(Files.readAllBytes(ini.toPath()), StandardCharsets.ISO_8859_1);
            text = setValue(text, "FOVFactor", String.format(Locale.ROOT, "%.2f", fieldOfView));
            text = setValue(text, "WidescreenHud", widescreenHud ? "1" : "0");
            text = setValue(text, "CutsceneBorders", Integer.toString(cutsceneBorders));
            text = setValue(text, "ComicsMode", comicsFullscreen ? "1" : "0");
            Files.write(ini.toPath(), text.getBytes(StandardCharsets.ISO_8859_1));
        } catch (Exception error) {
            Log.w("NFSC", "No se pudo escribir el .ini del WidescreenFix: " + error);
        }
    }

    private static String setValue(String text, String key, String value) {
        return text.replaceFirst("(?m)^(\\s*" + key + "\\s*=\\s*)[^\\s/;]*", "$1" + value);
    }
}
