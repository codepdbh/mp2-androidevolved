package com.mp2r.android;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import java.io.File;
import java.util.function.IntConsumer;
import java.util.function.IntFunction;

/** Pantalla previa al juego: comprueba los datos y deja cambiar los ajustes antes de jugar. */
public final class LauncherActivity extends Activity {
    private static final int RED = 0xFFB01818, PANEL = 0xD0101010, TEXT = 0xFFF0F0F0, DIM = 0xFFA8A8A8;
    private GameOptions options;
    private TextView dataStatus;
    private Button play;
    /** Aviso de fallo de la última partida, encima del botón de jugar. */
    private LinearLayout crashBanner;
    private TextView crashText;
    private String crash;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        options = GameOptions.load(this);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        ImageView art = new ImageView(this);
        art.setImageResource(R.drawable.mp2r_launcher_art);
        art.setScaleType(ImageView.ScaleType.CENTER_CROP);
        art.setAlpha(0.55f);
        root.addView(art, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout columns = new LinearLayout(this);
        columns.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(columns, new FrameLayout.LayoutParams(-1, -1));

        // Izquierda: título y botón de jugar.
        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setGravity(Gravity.BOTTOM | Gravity.START);
        left.setPadding(dp(32), dp(24), dp(16), dp(24));
        columns.addView(left, new LinearLayout.LayoutParams(0, -1, 1f));
        TextView title = text("MAX PAYNE 2", 34, TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        left.addView(title);
        left.addView(text("Android Evolved", 16, DIM));
        dataStatus = text("", 13, DIM);
        dataStatus.setPadding(0, dp(10), 0, dp(14));
        left.addView(dataStatus);
        crashBanner = new LinearLayout(this);
        crashBanner.setOrientation(LinearLayout.VERTICAL);
        crashBanner.setPadding(dp(14), dp(10), dp(14), dp(10));
        crashBanner.setBackground(rounded(0xE0301010));
        crashBanner.setVisibility(View.GONE);
        crashText = text("", 13, TEXT);
        crashBanner.addView(crashText);
        LinearLayout crashButtons = new LinearLayout(this);
        crashButtons.addView(smallButton(options.tr("Enviar informe", "Send report", "Enviar relatório"), RED, v -> report()));
        crashButtons.addView(smallButton(options.tr("Ignorar", "Dismiss", "Ignorar"), 0xFF303030, v -> { crash = null; crashBanner.setVisibility(View.GONE); }));
        crashBanner.addView(crashButtons);
        LinearLayout.LayoutParams bannerSize = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        bannerSize.setMargins(0, 0, 0, dp(12));
        left.addView(crashBanner, bannerSize);
        play = new Button(this);
        play.setText(options.tr("JUGAR", "PLAY", "JOGAR"));
        play.setTextColor(Color.WHITE);
        play.setTextSize(18);
        play.setTypeface(Typeface.DEFAULT_BOLD);
        play.setBackground(rounded(RED));
        play.setOnClickListener(v -> startGame());
        left.addView(play, new LinearLayout.LayoutParams(dp(220), dp(56)));

        // Derecha: ajustes.
        ScrollView scroll = new ScrollView(this);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(22), dp(16), dp(22), dp(24));
        panel.setBackgroundColor(PANEL);
        scroll.addView(panel, new FrameLayout.LayoutParams(-1, -2));
        columns.addView(scroll, new LinearLayout.LayoutParams(0, -1, 1.25f));

        section(panel, options.tr("Idioma", "Language", "Idioma"));
        LinearLayout languages = new LinearLayout(this);
        String[][] choices = {{"es", "Español"}, {"en", "English"}, {"pt", "Português"}};
        for (String[] choice : choices) {
            Button pick = new Button(this);
            pick.setText(choice[1]);
            pick.setAllCaps(false);
            pick.setTextColor(Color.WHITE);
            pick.setBackground(rounded(choice[0].equals(options.language) ? RED : 0xFF303030));
            pick.setOnClickListener(v -> { options.language = choice[0]; options.save(this); recreate(); });
            LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(0, dp(40), 1f);
            size.setMargins(dp(3), dp(4), dp(3), dp(4));
            languages.addView(pick, size);
        }
        panel.addView(languages);
        if (!gameHasLanguage()) panel.addView(text(options.tr("", "This installation has no English data: the game may show its base text.", "Esta instalação não traz português: o jogo mostrará o idioma base."), 12, DIM));

        section(panel, options.tr("Rendimiento", "Performance", "Desempenho"));
        slider(panel, options.tr("Resolución", "Resolution", "Resolução"), new String[] {"50 %", "67 %", "75 %", "85 %", options.tr("100 % (nativa)", "100 % (native)", "100 % (nativa)")}, indexOf(new int[] {50, 67, 75, 85, 100}, options.renderScale),
            i -> options.renderScale = new int[] {50, 67, 75, 85, 100}[i]);
        slider(panel, options.tr("Límite de FPS", "FPS limit", "Limite de FPS"), new String[] {"30", "60", "90", "120", options.tr("Sin límite", "Unlimited", "Sem limite")}, indexOf(new int[] {30, 60, 90, 120, 0}, options.frameLimit),
            i -> options.frameLimit = new int[] {30, 60, 90, 120, 0}[i]);
        toggle(panel, options.tr("Saltar los vídeos de inicio", "Skip startup videos", "Pular vídeos de início"), options.tr("Llegas antes al menú.", "Reach the menu sooner.", "Chega ao menu mais rápido."), options.skipIntro, on -> options.skipIntro = on);
        toggle(panel, options.tr("Carga rápida", "Fast loading", "Carregamento rápido"), options.tr("Omite la precarga 3D: menos espera al entrar, posibles tirones la primera vez que aparece algo.", "Skips 3D preloading: shorter waits, possible stutter the first time something appears.", "Pula o pré-carregamento 3D: menos espera, possíveis travadas na primeira vez que algo aparece."), options.fastLoad, on -> options.fastLoad = on);

        section(panel, options.tr("Pantalla panorámica", "Widescreen", "Tela panorâmica"));
        toggle(panel, "WidescreenFix", options.tr("Sin él, el juego se ve en 4:3 estirado.", "Without it the game looks stretched from 4:3.", "Sem ele, o jogo fica esticado em 4:3."), options.widescreen, on -> options.widescreen = on);
        slider(panel, options.tr("Campo de visión", "Field of view", "Campo de visão"), percentLabels(80, 140, 5), Math.round((options.fieldOfView - 0.8f) / 0.05f),
            i -> options.fieldOfView = 0.8f + i * 0.05f);
        toggle(panel, options.tr("HUD en los bordes", "HUD at the edges", "HUD nas bordas"), options.tr("Lleva salud y munición a las esquinas de la pantalla.", "Moves health and ammo to the screen corners.", "Leva saúde e munição aos cantos da tela."), options.widescreenHud, on -> options.widescreenHud = on);
        toggle(panel, options.tr("Novela gráfica a pantalla completa", "Full-screen graphic novel", "Graphic novel em tela cheia"), null, options.comicsFullscreen, on -> options.comicsFullscreen = on);
        slider(panel, options.tr("Bandas en cinemáticas", "Cutscene bars", "Barras nas cenas"), new String[] {options.tr("Originales", "Original", "Originais"), options.tr("Sin bandas", "None", "Sem barras"), options.tr("Según la pantalla", "Match the screen", "Conforme a tela")}, options.cutsceneBorders,
            i -> options.cutsceneBorders = i);

        section(panel, options.tr("Controles táctiles", "Touch controls", "Controles de toque"));
        slider(panel, options.tr("Sensibilidad de la mirada", "Look sensitivity", "Sensibilidade da mira"), percentLabels(20, 200, 10), Math.round((options.lookSensitivity / 0.45f * 100 - 20) / 10),
            i -> options.lookSensitivity = 0.45f * (20 + i * 10) / 100f);
        slider(panel, options.tr("Opacidad de los botones", "Button opacity", "Opacidade dos botões"), percentLabels(20, 100, 5), (options.hudOpacity - 20) / 5,
            i -> options.hudOpacity = 20 + i * 5);

        Button editor = new Button(this);
        editor.setText(options.tr("Mover y cambiar tamaño de los botones", "Move and resize the buttons", "Mover e redimensionar os botões"));
        editor.setAllCaps(false);
        editor.setOnClickListener(v -> { options.save(this); startActivity(new Intent(this, ControlsEditorActivity.class)); });
        panel.addView(editor);

        section(panel, options.tr("Errores", "Errors", "Erros"));
        panel.addView(text(options.tr(
            "Guarda el log de la última partida y abre un issue en GitHub con los datos del dispositivo. Revisa el texto antes de enviarlo: el issue es público.",
            "Saves the last session's log and opens a GitHub issue with the device details. Review the text before sending it: the issue is public.",
            "Salva o log da última partida e abre uma issue no GitHub com os dados do dispositivo. Revise o texto antes de enviar: a issue é pública."), 12, DIM));
        Button reportButton = new Button(this);
        reportButton.setText(options.tr("Reportar un problema en GitHub", "Report a problem on GitHub", "Relatar um problema no GitHub"));
        reportButton.setAllCaps(false);
        reportButton.setOnClickListener(v -> report());
        panel.addView(reportButton);

        setContentView(root);
    }

    @Override protected void onResume() {
        super.onResume();
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        refreshData();
        // Android anota el cierre del proceso del juego un poco después de volver aquí.
        checkCrash();
        for (int delay : new int[] {1000, 3000, 6000}) crashBanner.postDelayed(checkCrash, delay);
    }

    // El sistema anota el fallo al cerrar su propio diálogo de error, sin pasar por onResume.
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) { checkCrash(); crashBanner.postDelayed(checkCrash, 1000); }
    }

    @Override protected void onStop() { crashBanner.removeCallbacks(checkCrash); super.onStop(); }

    private final Runnable checkCrash = this::checkCrash;
    private void checkCrash() {
        String fresh = CrashReports.pendingCrash(this);
        if (fresh != null) crash = fresh;
        if (crash != null) {
            crashText.setText(options.tr("El juego se cerró por un error.\n", "The game closed because of an error.\n", "O jogo fechou por causa de um erro.\n") + crash);
            crashBanner.setVisibility(View.VISIBLE);
        }
    }

    private void report() {
        try {
            String where = CrashReports.sendToGitHub(this, options, crash);
            android.widget.Toast.makeText(this, where != null
                ? options.tr("Informe guardado en ", "Report saved to ", "Relatório salvo em ") + where + options.tr(" y copiado. Adjúntalo al issue.", " and copied. Attach it to the issue.", " e copiado. Anexe-o à issue.")
                : options.tr("Informe copiado al portapapeles.", "Report copied to the clipboard.", "Relatório copiado para a área de transferência."), android.widget.Toast.LENGTH_LONG).show();
            crash = null;
            crashBanner.setVisibility(View.GONE);
        } catch (RuntimeException e) {
            android.util.Log.e("MP2Report", "No se pudo preparar el informe", e);
            android.widget.Toast.makeText(this, options.tr("No se pudo preparar el informe: ", "Could not prepare the report: ", "Não foi possível preparar o relatório: ") + e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
        }
    }

    private boolean gameHasLanguage() {
        File root = new File(Environment.getExternalStorageDirectory(), "mp2");
        if (options.language.equals("es")) return true;
        return new File(root, options.language.equals("en") ? "MP2_English.ras" : "MP2_Portuguese.ras").exists();
    }

    private boolean hasPermission() { return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager(); }

    private void refreshData() {
        if (!hasPermission()) { dataStatus.setText(options.tr("Falta el permiso de archivos: pulsa JUGAR para concederlo.", "File permission missing: press PLAY to grant it.", "Falta a permissão de arquivos: toque em JOGAR para conceder.")); return; }
        File exe = new File(Environment.getExternalStorageDirectory(), "mp2/MaxPayne2.exe");
        dataStatus.setText(exe.exists() ? options.tr("Datos del juego: Memoria interna/mp2", "Game data: Internal storage/mp2", "Dados do jogo: Armazenamento interno/mp2") : options.tr("No encuentro Memoria interna/mp2/MaxPayne2.exe. Copia ahí tu instalación.", "Internal storage/mp2/MaxPayne2.exe not found. Copy your installation there.", "Não encontrei Armazenamento interno/mp2/MaxPayne2.exe. Copie sua instalação para lá."));
        play.setEnabled(exe.exists());
    }

    private void startGame() {
        if (!hasPermission()) {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
            return;
        }
        options.save(this);
        options.applyWidescreenIni();
        startActivity(new Intent(this, GameActivity.class));
    }

    @Override protected void onPause() { options.save(this); super.onPause(); }

    // ---- Construcción de la interfaz ----
    private static String[] percentLabels(int from, int to, int step) {
        String[] labels = new String[(to - from) / step + 1];
        for (int i = 0; i < labels.length; i++) labels[i] = (from + i * step) + " %";
        return labels;
    }
    private static int indexOf(int[] values, int value) {
        for (int i = 0; i < values.length; i++) if (values[i] == value) return i;
        return values.length - 1;
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }
    private Button smallButton(String label, int color, View.OnClickListener click) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.WHITE);
        button.setBackground(rounded(color));
        button.setOnClickListener(click);
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(0, dp(40), 1f);
        size.setMargins(0, dp(8), dp(8), 0);
        button.setLayoutParams(size);
        return button;
    }
    private GradientDrawable rounded(int color) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(dp(8));
        return shape;
    }
    private void section(LinearLayout parent, String name) {
        TextView header = text(name.toUpperCase(), 13, 0xFFE05050);
        header.setTypeface(Typeface.DEFAULT_BOLD);
        header.setPadding(0, dp(16), 0, dp(4));
        parent.addView(header);
    }
    private interface BoolConsumer { void accept(boolean value); }
    private void toggle(LinearLayout parent, String name, String detail, boolean value, BoolConsumer change) {
        Switch box = new Switch(this);
        box.setText(name);
        box.setTextColor(TEXT);
        box.setTextSize(15);
        box.setChecked(value);
        box.setMinHeight(dp(44));
        box.setOnCheckedChangeListener((v, on) -> change.accept(on));
        parent.addView(box);
        if (detail != null) parent.addView(text(detail, 12, DIM));
    }
    private void slider(LinearLayout parent, String name, String[] labels, int index, IntConsumer change) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(8), 0, 0);
        TextView label = text(name, 15, TEXT);
        row.addView(label, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        int start = Math.max(0, Math.min(labels.length - 1, index));
        TextView value = text(labels[start], 15, 0xFFE05050);
        row.addView(value);
        parent.addView(row);
        SeekBar bar = new SeekBar(this);
        bar.setMax(labels.length - 1);
        bar.setProgress(start);
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int progress, boolean user) { value.setText(labels[progress]); change.accept(progress); }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        });
        parent.addView(bar);
    }
}
