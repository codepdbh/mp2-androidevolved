package com.mp2r.android;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/** Editor de los controles táctiles: los mismos botones que en el juego, sobre la imagen del launcher. */
public final class ControlsEditorActivity extends Activity {
    private GameHudView hud;
    private SeekBar size;
    private TextView hint;
    private boolean updating;
    private GameOptions labels;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        ImageView art = new ImageView(this);
        art.setImageResource(R.drawable.mp2r_launcher_art);
        art.setScaleType(ImageView.ScaleType.CENTER_CROP);
        art.setAlpha(0.45f);
        root.addView(art, new FrameLayout.LayoutParams(-1, -1));
        GameOptions options = GameOptions.load(this);
        labels = options;
        options.hudOpacity = Math.max(options.hudOpacity, 70);
        hud = new GameHudView(this, options);
        hud.startEditing(fraction -> {
            updating = true;
            size.setProgress(Math.round((fraction - 0.06f) * 200));
            updating = false;
            hint.setText(labels.tr("Tamaño", "Size", "Tamanho"));
        });
        root.addView(hud, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(0xC0101010);
        bar.setPadding(dp(12), dp(4), dp(12), dp(4));
        hint = new TextView(this);
        hint.setText(labels.tr("Arrastra un control; pellizca para cambiar su tamaño", "Drag a control; pinch to resize it", "Arraste um controle; faça pinça para redimensionar"));
        hint.setTextColor(0xFFF0F0F0);
        bar.addView(hint);
        size = new SeekBar(this);
        size.setMax(108);
        size.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int progress, boolean user) { if (!updating && user) hud.setSelectedSize(0.06f + progress / 200f); }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        });
        bar.addView(size, new LinearLayout.LayoutParams(0, -2, 1f));
        bar.addView(action(labels.tr("Restablecer", "Reset", "Restaurar"), v -> hud.resetLayout()));
        bar.addView(action(labels.tr("Guardar", "Save", "Salvar"), v -> { hud.saveLayout(); finish(); }));
        FrameLayout.LayoutParams place = new FrameLayout.LayoutParams(dp(640), -2, Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        root.addView(bar, place);
        setContentView(root);
    }

    @Override protected void onResume() {
        super.onResume();
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
    private Button action(String label, View.OnClickListener click) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setOnClickListener(click);
        return button;
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
}
