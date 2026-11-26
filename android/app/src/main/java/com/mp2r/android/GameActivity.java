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
    static native float nativeFps();
    static native boolean nativeGamepadUsed();
    static native void nativeHudEditing(boolean editing);
    private static native void nativeHiddenFiles(String names);
    private static native void nativeFlags(String flags);
    private static native void nativeRenderScale(int percent);
    private static native void nativeFrameLimit(int cap);
    private static native void nativeWidescreen(boolean enabled);

    private TextView status;
    private PopupWindow statusWindow, hudWindow;
    private PopupWindow fpsWindow;
    private TextView fpsText;
    private boolean showFps;
    private android.app.AlertDialog failureDialog;
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
        showFps = options.showFps;
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
        fpsText = new TextView(this);
        fpsText.setTextColor(Color.WHITE);
        fpsText.setBackgroundColor(0x90000000);
        fpsText.setPadding(12, 4, 12, 4);
        fpsWindow = new PopupWindow(fpsText, ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        fpsWindow.setTouchable(false);
    }

    @Override protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager() && !requested) {
            requested = true;
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        }
        getWindow().getDecorView().post(this::showOverlays);
        if (showFps) { handler.removeCallbacks(updateFps); handler.post(updateFps); }
    }

    @Override protected void onPause() {
        handler.removeCallbacks(updateFps);
        fpsWindow.dismiss();
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
        if (showFps && !fpsWindow.isShowing()) fpsWindow.showAtLocation(decor, Gravity.TOP | Gravity.CENTER_HORIZONTAL, 0, 0);
    }

    private final Runnable updateFps = new Runnable() {
        @Override public void run() {
            if (isFinishing() || isDestroyed()) return;
            fpsText.setText(String.format(java.util.Locale.ROOT, "%.1f FPS", nativeFps()));
            fpsWindow.update();
            handler.postDelayed(this, 500);
        }
    };

    /** Called after the guest has released its audio and graphics resources. */
    public void finishGame() {
        runOnUiThread(this::finishAndRemoveTask);
    }

    /** Native boot failures remain visible and are included even when logcat is empty. */
    public void setFailure(String message) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            handler.removeCallbacks(hideStatus);
            status.setText(message);
            statusVisible = true;
            CrashReports.recordFailure(this, message);
            showOverlays();
            statusWindow.update();
            if (failureDialog == null) {
                GameOptions options = GameOptions.load(this);
                failureDialog = new android.app.AlertDialog.Builder(this).setTitle("Max Payne 2")
                    .setMessage(message).setCancelable(false)
                    .setPositiveButton(options.tr("Volver al inicio", "Back to launcher", "Voltar ao in\u00edcio"), (dialog, which) -> finish())
                    .show();
            }
        });
    }

    /** Lo llama el hilo nativo con el estado de arranque. */
    public void setStatus(String message) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
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

    @Override protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        if (failureDialog != null) failureDialog.dismiss();
        if (statusWindow != null) statusWindow.dismiss();
        if (hudWindow != null) hudWindow.dismiss();
        if (fpsWindow != null) fpsWindow.dismiss();
        super.onDestroy();
    }
}
