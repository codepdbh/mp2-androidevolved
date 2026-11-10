package com.mp2r.android;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Process;
import android.util.Log;
import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Registro de la sesión de juego y avisos de fallo para enviarlos como issue de GitHub.
 *
 * El proceso :game guarda su propio logcat en un archivo. Al volver al launcher, Android dice
 * cómo terminó ese proceso; si fue un fallo, el launcher ofrece el informe. No se envía nada
 * por sí solo: el informe se guarda, se copia al portapapeles y se abre la página de nuevo
 * issue con un resumen, que la persona revisa y envía con su propia cuenta.
 */
final class CrashReports {
    static final String REPOSITORY = "codepdbh/maxpayne2pc-androidevolved";
    private static final String TAG = "MP2Report";
    private static final String GAME_PROCESS = ":game";
    /** La URL de un issue nuevo no admite cuerpos largos: el log completo va en el archivo. */
    private static final int URL_BODY_LIMIT = 6000, CLIPBOARD_LIMIT = 200_000, TAIL_LINES = 60;

    private CrashReports() {}

    private static File logDir(Context context) {
        File dir = new File(context.getFilesDir(), "logs");
        dir.mkdirs();
        return dir;
    }
    static File currentLog(Context context) { return new File(logDir(context), "partida.log"); }
    static File previousLog(Context context) { return new File(logDir(context), "partida_anterior.log"); }

    // ---- Proceso del juego ----

    /** Lo llama GameActivity al arrancar: conserva el log anterior y empieza a guardar este. */
    static void startGameLog(Context context) {
        File current = currentLog(context), previous = previousLog(context);
        if (current.exists()) { previous.delete(); current.renameTo(previous); }
        try {
            // Un hijo del proceso del juego: Android lo termina junto con él.
            java.lang.Process capture = new ProcessBuilder("logcat", "-v", "threadtime", "--pid=" + Process.myPid(),
                "-f", current.getAbsolutePath(), "-r", "4096", "-n", "1").redirectErrorStream(true).start();
            new Thread(() -> {
                try {
                    if (!capture.waitFor(2, java.util.concurrent.TimeUnit.SECONDS)) { Log.i(TAG, "Guardando el log de la partida en " + current); return; }
                    byte[] output = new byte[2048];
                    int read = capture.getInputStream().read(output);
                    Log.w(TAG, "logcat terminó con " + capture.exitValue() + ": " + (read > 0 ? new String(output, 0, read, StandardCharsets.UTF_8) : ""));
                } catch (Exception e) { Log.w(TAG, "logcat", e); }
            }, "mp2-log").start();
        } catch (IOException e) {
            Log.w(TAG, "No se pudo guardar el log de la partida", e);
        }
    }

    // ---- Launcher ----

    /** Cierre anómalo del juego que aún no se ha mostrado, o null. */
    static String pendingCrash(Context context) {
        if (Build.VERSION.SDK_INT < 30) return null;
        SharedPreferences prefs = context.getSharedPreferences("mp2_informes", Context.MODE_PRIVATE);
        long seen = prefs.getLong("visto", 0);
        ApplicationExitInfo exit = lastGameExit(context);
        if (exit == null || exit.getTimestamp() <= seen) return null;
        Log.i(TAG, "Último cierre del juego: " + describe(exit));
        prefs.edit().putLong("visto", exit.getTimestamp()).apply();
        return isFailure(exit) ? describe(exit) : null;
    }

    private static ApplicationExitInfo lastGameExit(Context context) {
        if (Build.VERSION.SDK_INT < 30) return null;
        ActivityManager manager = context.getSystemService(ActivityManager.class);
        List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 10);
        for (ApplicationExitInfo exit : exits) if (exit.getProcessName().endsWith(GAME_PROCESS)) return exit;
        return null;
    }

    private static boolean isFailure(ApplicationExitInfo exit) {
        switch (exit.getReason()) {
            case ApplicationExitInfo.REASON_CRASH:
            case ApplicationExitInfo.REASON_CRASH_NATIVE:
            case ApplicationExitInfo.REASON_ANR:
            case ApplicationExitInfo.REASON_LOW_MEMORY:
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE:
                return true;
            case ApplicationExitInfo.REASON_SIGNALED:
                return exit.getStatus() != 9;  // SIGKILL: lo cerró el sistema o la persona
            default:
                return false;
        }
    }

    private static String describe(ApplicationExitInfo exit) {
        String reason;
        switch (exit.getReason()) {
            case ApplicationExitInfo.REASON_CRASH: reason = "CRASH (Java)"; break;
            case ApplicationExitInfo.REASON_CRASH_NATIVE: reason = "CRASH_NATIVE"; break;
            case ApplicationExitInfo.REASON_ANR: reason = "ANR"; break;
            case ApplicationExitInfo.REASON_LOW_MEMORY: reason = "LOW_MEMORY"; break;
            case ApplicationExitInfo.REASON_SIGNALED: reason = "SIGNALED " + exit.getStatus(); break;
            case ApplicationExitInfo.REASON_EXIT_SELF: reason = "EXIT_SELF " + exit.getStatus(); break;
            default: reason = "reason " + exit.getReason();
        }
        String detail = exit.getDescription();
        return reason + (detail != null && !detail.isEmpty() ? " — " + detail : "") + " — " + stamp(exit.getTimestamp())
            + (exit.getPss() > 0 ? " — PSS " + exit.getPss() / 1024 + " MB" : "");
    }

    /** Arma el informe, lo guarda, lo copia y abre el formulario de issue. Devuelve dónde quedó guardado. */
    static String sendToGitHub(Context context, GameOptions options, String crash) {
        String header = header(context, options, crash);
        String log = gameLog(context);
        String full = header + "\n## Log\n\n" + log;
        File saved = save(context, full);
        ClipboardManager clipboard = context.getSystemService(ClipboardManager.class);
        String clip = full.length() > CLIPBOARD_LIMIT ? header + "\n## Log (final)\n\n" + log.substring(log.length() - (CLIPBOARD_LIMIT - header.length() - 32)) : full;
        clipboard.setPrimaryClip(ClipData.newPlainText("Max Payne 2 log", clip));

        String where = saved != null ? displayPath(saved) : null;
        String title = (crash != null ? "[Crash] " : "[Problema] ") + Build.MANUFACTURER + " " + Build.MODEL + " — " + version(context);
        StringBuilder body = new StringBuilder();
        body.append(options.tr("<!-- Describe qué estabas haciendo cuando falló. -->\n\n",
            "<!-- Describe what you were doing when it failed. -->\n\n",
            "<!-- Descreva o que você estava fazendo quando falhou. -->\n\n"));
        body.append(header);
        body.append(options.tr("\n## Log completo\n\nAdjunta el archivo ", "\n## Full log\n\nAttach the file ", "\n## Log completo\n\nAnexe o arquivo "))
            .append(where != null ? "`" + where + "`" : "")
            .append(options.tr(" (también está copiado en el portapapeles).\n", " (it is also on the clipboard).\n", " (também está copiado na área de transferência).\n"));
        String tail = errorLines(log, TAIL_LINES);
        int room = URL_BODY_LIMIT - body.length() - 40;
        if (room > 200) {
            if (tail.length() > room) tail = tail.substring(tail.length() - room);
            body.append("\n<details><summary>").append(options.tr("Últimas líneas", "Last lines", "Últimas linhas"))
                .append("</summary>\n\n```\n").append(tail).append("\n```\n</details>\n");
        }
        Uri uri = Uri.parse("https://github.com/" + REPOSITORY + "/issues/new?title=" + encode(title) + "&body=" + encode(body.toString()));
        context.startActivity(new Intent(Intent.ACTION_VIEW, uri).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        return where;
    }

    private static String header(Context context, GameOptions o, String crash) {
        StringBuilder s = new StringBuilder();
        s.append("## ").append(o.tr("Dispositivo", "Device", "Dispositivo")).append("\n\n");
        s.append("- App: ").append(version(context)).append('\n');
        s.append("- Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" (").append(Build.DEVICE).append(")\n");
        s.append("- Android: ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        if (Build.VERSION.SDK_INT >= 31) s.append("- SoC: ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        s.append("- ABI: ").append(String.join(", ", Build.SUPPORTED_ABIS)).append('\n');
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        context.getSystemService(ActivityManager.class).getMemoryInfo(memory);
        s.append("- RAM: ").append(memory.totalMem >> 20).append(" MB\n");
        s.append("\n## ").append(o.tr("Ajustes", "Settings", "Ajustes")).append("\n\n");
        s.append("- language=").append(o.language).append(", renderScale=").append(o.renderScale).append(", fps=").append(o.frameLimit)
            .append(", widescreen=").append(o.widescreen).append(", fov=").append(String.format(Locale.ROOT, "%.2f", o.fieldOfView))
            .append(", skipIntro=").append(o.skipIntro).append(", fastLoad=").append(o.fastLoad).append('\n');
        s.append("\n## ").append(o.tr("Cierre del juego", "Game exit", "Encerramento do jogo")).append("\n\n");
        String exit = crash;
        if (exit == null && Build.VERSION.SDK_INT >= 30) { ApplicationExitInfo last = lastGameExit(context); if (last != null) exit = describe(last); }
        s.append(exit != null ? exit : o.tr("Sin datos", "No data", "Sem dados")).append('\n');
        return s.toString();
    }

    /** El log de la última partida terminada (o de la actual), más lo que aún quede en el logcat de la app. */
    private static String gameLog(Context context) {
        StringBuilder s = new StringBuilder();
        File current = currentLog(context), previous = previousLog(context);
        File log = current.exists() ? current : previous;
        if (log.exists()) {
            try { s.append(new String(Files.readAllBytes(log.toPath()), StandardCharsets.UTF_8)); }
            catch (IOException e) { s.append("(").append(e).append(")\n"); }
        }
        // El volcado del fallo nativo (tombstone) lo escribe el sistema en el logcat de la app.
        s.append("\n## logcat -d\n\n");
        try {
            java.lang.Process dump = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "1500").redirectErrorStream(true).start();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(dump.getInputStream(), StandardCharsets.UTF_8))) {
                for (String line; (line = reader.readLine()) != null; ) s.append(line).append('\n');
            }
        } catch (IOException e) { s.append("(").append(e).append(")\n"); }
        return s.toString();
    }

    private static File save(Context context, String text) {
        String name = "informe_" + new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.ROOT).format(new Date()) + ".txt";
        File[] dirs = {new File(Environment.getExternalStorageDirectory(), "mp2/logs"), context.getExternalFilesDir("logs"), logDir(context)};
        for (File dir : dirs) {
            if (dir == null) continue;
            try {
                dir.mkdirs();
                File file = new File(dir, name);
                Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8));
                return file;
            } catch (IOException | SecurityException e) {
                Log.w(TAG, "No se pudo guardar en " + dir, e);
            }
        }
        return null;
    }

    private static String displayPath(File file) {
        String path = file.getAbsolutePath(), root = Environment.getExternalStorageDirectory().getAbsolutePath();
        return path.startsWith(root) ? "Internal storage" + path.substring(root.length()) : path;
    }

    /**
     * Para el cuerpo del issue: los errores y avisos (E, W, F) del final del log, sin los del
     * propio launcher. Si no hay ninguno, el final del log tal cual.
     */
    private static String errorLines(String text, int lines) {
        String launcher = String.valueOf(Process.myPid()), previous = null;
        int repeats = 0;
        ArrayDeque<String> last = new ArrayDeque<>();
        for (String line : text.split("\n")) {
            // threadtime: fecha hora pid tid nivel etiqueta: mensaje
            String[] field = line.trim().split(" +", 6);
            if (field.length < 6 || field[2].equals(launcher) || field[4].length() != 1) continue;
            char level = field[4].charAt(0);
            if (level != 'E' && level != 'W' && level != 'F') continue;
            // Avisos repetidos en ráfaga (DXVK): una línea y la cuenta.
            String message = field[5];
            if (message.equals(previous)) { repeats++; continue; }
            if (repeats > 0) addLine(last, "    (×" + (repeats + 1) + ")", lines);
            previous = message; repeats = 0;
            addLine(last, line, lines);
        }
        if (repeats > 0) addLine(last, "    (×" + (repeats + 1) + ")", lines);
        return last.isEmpty() ? tail(text, lines) : String.join("\n", last);
    }

    private static void addLine(ArrayDeque<String> last, String line, int lines) {
        last.addLast(line);
        if (last.size() > lines) last.removeFirst();
    }

    private static String tail(String text, int lines) {
        ArrayDeque<String> last = new ArrayDeque<>();
        for (String line : text.split("\n")) { last.addLast(line); if (last.size() > lines) last.removeFirst(); }
        return String.join("\n", last);
    }

    static String version(Context context) {
        try { return context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName; }
        catch (Exception e) { return "?"; }
    }
    private static String stamp(long millis) { return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new Date(millis)); }
    private static String encode(String value) {
        try { return URLEncoder.encode(value, "UTF-8").replace("+", "%20"); }
        catch (java.io.UnsupportedEncodingException e) { throw new IllegalStateException(e); }
    }
}
