package com.mp2r.android;

import android.app.NativeActivity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.PopupWindow;
import android.widget.TextView;

/** Pide el permiso de archivos para la carpeta fija mp2 y pone el HUD sobre la superficie del juego. */
public final class GameActivity extends NativeActivity {
    static { System.loadLibrary("mp2_android"); }
    static native void nativeKey(int scan, boolean down);
    static native void nativeMouseMotion(float dx, float dy);
    static native void nativeMouseButton(int button, boolean down);
    static native void nativeMouseWheel(int delta);
    static native void nativePointer(float x, float y, int phase);
    static native int nativeScreenMode();
    static native boolean nativeGamepadUsed();
    static native void nativeHudEditing(boolean editing);
    private static native void nativeHiddenFiles(String names);
    private static native void nativeFlags(String flags);
    private static native void nativeRenderScale(int percent);
    private static native void nativeFrameLimit(int cap);
    private static native void nativeWidescreen(boolean enabled);

    private TextView status;
    private PopupWindow statusWindow, hudWindow;
    private boolean requested;
    private final android.os.Handler handler = new android.os.Handler(android.os.Looper.getMainLooper());

    /**
     * El juego recompilado solo se puede iniciar una vez por proceso. Si Android crea otra
     * GameActivity en el mismo proceso (la anterior se destruyó al salir o al recrearse),
     * el juego ya se detuvo y no puede volver a arrancar: se reinicia el proceso.
     */
    private static boolean gameStarted;
    static final String EXTRA_RELAUNCH = "com.mp2r.android.RELAUNCH_GAME";

    @Override protected void onCreate(Bundle state) {
        if (gameStarted) {
            android.util.Log.w("NFSC", "GameActivity nueva en un proceso ya usado: se reinicia el proceso del juego");
            startActivity(new Intent(this, LauncherActivity.class).putExtra(EXTRA_RELAUNCH, true)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP));
            android.os.Process.killProcess(android.os.Process.myPid());
            return;
        }
        gameStarted = true;
        // Antes de super.onCreate: el hilo nativo arranca el juego con estos valores.
        GameOptions options = GameOptions.load(this);
        CrashReports.startGameLog(this);
        nativeFlags(options.gameFlags());
        nativeHiddenFiles(options.hiddenLanguageFiles());
        nativeRenderScale(options.renderScale);
        nativeFrameLimit(options.frameLimit);
        nativeWidescreen(options.widescreen);
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        getWindow().setAttributes(attributes);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setBackgroundColor(0xB0000000);
        status.setPadding(24, 16, 24, 16);
        statusWindow = new PopupWindow(status, ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        statusWindow.setTouchable(false);
        statusWindow.setClippingEnabled(false);
        hudWindow = new PopupWindow(new GameHudView(this, options), ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        hudWindow.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        hudWindow.setClippingEnabled(false);
        hudWindow.setSplitTouchEnabled(true);
    }

    @Override protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager() && !requested) {
            requested = true;
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        }
        getWindow().getDecorView().post(this::showOverlays);
    }

    @Override protected void onPause() {
        statusWindow.dismiss();
        hudWindow.dismiss();
        super.onPause();
    }

    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) hideSystemBars();
    }

    private void hideSystemBars() {
        WindowInsetsController controller = getWindow().getInsetsController();
        if (controller == null) return;
        controller.hide(WindowInsets.Type.systemBars());
        controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
    }

    private void showOverlays() {
        View decor = getWindow().getDecorView();
        if (decor.getWindowToken() == null) return;
        if (!hudWindow.isShowing()) hudWindow.showAtLocation(decor, Gravity.TOP | Gravity.START, 0, 0);
        if (statusVisible && !statusWindow.isShowing()) statusWindow.showAtLocation(decor, Gravity.TOP | Gravity.START, 0, 0);
    }

    /** Lo llama el hilo nativo con el estado de arranque. */
    public void setStatus(String message) {
        runOnUiThread(() -> {
            status.setText(message);
            statusVisible = true;
            android.util.Log.i("NFSC", "Estado: " + message);
            statusWindow.update();
            showOverlays();
            handler.removeCallbacks(hideStatus);
            handler.postDelayed(hideStatus, 8000);
        });
    }
    private boolean statusVisible;
    private final Runnable hideStatus = () -> { statusVisible = false; statusWindow.dismiss(); };
}
