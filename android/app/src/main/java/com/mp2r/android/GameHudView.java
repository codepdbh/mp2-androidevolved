package com.mp2r.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.hardware.input.InputManager;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.graphics.RectF;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.List;

/**
 * Controles táctiles sobre el juego, con los iconos del proyecto tal cual: los efectos
 * son solo alfa y escala. Cada acción llega al juego como la tecla o el botón de ratón
 * que tiene asignado en sus controles (config/pc: medidos en Windows). Bullet Time es
 * SLOWMOTION real (botón derecho) y Shootdodge es DODGEMODIFIER (Mayús izquierda).
 *
 * Con un menú en pantalla un toque es un clic en esa posición; si no, joystick y mirada.
 */
final class GameHudView extends View {
    private static final int KEY_W = 0x11, KEY_S = 0x1F, KEY_A = 0x1E, KEY_D = 0x20;

    private static final class Button {
        final String id; final Bitmap icon; final float x, y, defaultSize; final int key, mouse, wheel; boolean down; final RectF rect = new RectF();
        float centerX, centerY, size;  // Fracción del ancho, del alto y tamaño en fracción del alto.
        Button(String id, Bitmap icon, float x, float y, float size, int key, int mouse, int wheel) {
            this.id = id; this.icon = icon; this.x = x; this.y = y; this.defaultSize = size; this.key = key; this.mouse = mouse; this.wheel = wheel;
        }
    }
    private static final int ROLE_NONE = 0, ROLE_STICK = 1, ROLE_LOOK = 2, ROLE_BUTTON = 3, ROLE_MENU = 4;
    private static final class Touch { int role; float lastX, lastY; Button button; }

    private final List<Button> buttons = new ArrayList<>();
    private final Button pause;
    private final SparseArray<Touch> touches = new SparseArray<>();
    private final Paint iconPaint = new Paint(Paint.FILTER_BITMAP_FLAG), stickPaint = new Paint(Paint.ANTI_ALIAS_FLAG), textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean menuMode = true;
    private float stickCenterX, stickCenterY, stickX, stickY, stickRadius;
    private boolean stickActive, forward, back, left, right;

    private final float lookSensitivity;
    /** Puntería fina: mientras se mantiene el botón de apuntar, la mirada va más lenta. */
    private static final float PRECISION_FACTOR = 0.35f;
    private Button aim;
    private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final PorterDuffColorFilter pressedTint = new PorterDuffColorFilter(0xFFFFD27A, PorterDuff.Mode.SRC_ATOP);
    /** Con un mando conectado y en uso, los controles táctiles se ocultan. */
    private boolean gamepadActive;
    private InputManager inputManager;
    private final InputManager.InputDeviceListener deviceListener = new InputManager.InputDeviceListener() {
        @Override public void onInputDeviceAdded(int id) { refreshGamepads(); }
        @Override public void onInputDeviceRemoved(int id) { refreshGamepads(); }
        @Override public void onInputDeviceChanged(int id) { refreshGamepads(); }
    };
    private static boolean isGamepad(InputDevice device) {
        if (device == null || device.isVirtual()) return false;
        int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
            || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }
    private void refreshGamepads() {
        boolean any = false;
        for (int id : InputDevice.getDeviceIds()) any |= isGamepad(InputDevice.getDevice(id));
        if (!any && gamepadActive) { gamepadActive = false; invalidate(); }
        if (any && !gamepadActive && hasGamepadInput) { gamepadActive = true; releaseAll(); invalidate(); }
        gamepadConnected = any;
    }
    private boolean gamepadConnected, hasGamepadInput;
    private void releaseAll() {
        for (Button b : buttons) if (b.down) press(b, false);
        updateStick(stickCenterX, stickCenterY);
        touches.clear();
    }
    private final int idleAlpha;
    /** El juego dice si hay un menú en pantalla: el modo cambia solo. */
    private final Runnable pollMode = new Runnable() {
        @Override public void run() {
            if (!editing) {
                boolean menu = GameActivity.nativeScreenMode() != 2;
                if (menu != menuMode) { setMenuMode(menu); invalidate(); }
                // El mando se usó: fuera los botones táctiles hasta que se vuelva a tocar la pantalla.
                if (GameActivity.nativeGamepadUsed() && gamepadConnected && !gamepadActive) {
                    hasGamepadInput = true; gamepadActive = true; releaseAll(); invalidate();
                }
            }
            postDelayed(this, 200);
        }
    };
    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (editorOnly) return;
        post(pollMode);
        inputManager = (InputManager) getContext().getSystemService(Context.INPUT_SERVICE);
        inputManager.registerInputDeviceListener(deviceListener, null);
        refreshGamepads();
    }
    @Override protected void onDetachedFromWindow() {
        removeCallbacks(pollMode);
        if (inputManager != null) inputManager.unregisterInputDeviceListener(deviceListener);
        super.onDetachedFromWindow();
    }

    GameHudView(Context context, GameOptions options) {
        super(context);
        labels = options;
        lookSensitivity = options.lookSensitivity;
        idleAlpha = options.hudOpacity * 255 / 100;
        setBackgroundColor(0);
        stickPaint.setColor(0xFFFFFFFF);
        glowPaint.setStyle(Paint.Style.FILL);
        textPaint.setColor(0xFFFFFFFF);
        textPaint.setTextAlign(Paint.Align.CENTER);
        // Posición en fracción del alto desde abajo a la derecha, y tamaño en fracción del alto.
        buttons.add(new Button("fire", icon(R.drawable.hud_fire), 0.20f, 0.24f, 0.26f, 0, 0, 0));
        aim = new Button("aim", icon(R.drawable.hud_aim), 0.48f, 0.15f, 0.17f, 0x10, -1, 0);                  // SNIPERZOOM: Q + puntería fina
        buttons.add(aim);
        buttons.add(new Button("bullet_time", icon(R.drawable.hud_bullet_time), 0.46f, 0.38f, 0.19f, 0, 1, 0));      // SLOWMOTION: ratón 1
        buttons.add(new Button("shootdodge", icon(R.drawable.hud_shootdodge), 0.25f, 0.55f, 0.19f, 0x2A, -1, 0));   // DODGEMODIFIER: Mayús izq.
        buttons.add(new Button("jump", icon(R.drawable.hud_jump), 0.70f, 0.20f, 0.17f, 0x39, -1, 0));         // JUMP: Espacio
        buttons.add(new Button("reload", icon(R.drawable.hud_reload), 0.70f, 0.42f, 0.15f, 0x13, -1, 0));       // RELOAD: R
        buttons.add(new Button("interact", icon(R.drawable.hud_interact), 0.47f, 0.60f, 0.15f, 0x12, -1, 0));     // USE: E
        buttons.add(new Button("painkiller", icon(R.drawable.hud_painkiller), 0.70f, 0.62f, 0.14f, 0x0F, -1, 0));   // PAINKILLER: Tab
        buttons.add(new Button("weapons", icon(R.drawable.hud_weapons), 0.92f, 0.14f, 0.15f, 0x1A, -1, 0));      // NEXT: [ (la rueda no cambiaba de arma)
        pause = new Button("pause", icon(R.drawable.hud_pause), 0.10f, 0.90f, 0.12f, 0x01, -1, 0);             // Esc
        buttons.add(pause);
        stick = new Button("stick", null, 0, 0, 0.32f, 0, -1, 0);
    }

    // ---- Editor de controles: mover con un dedo, cambiar el tamaño con dos o con el deslizador ----
    interface EditorListener { void onSelected(float sizeFraction); }
    private static final String LAYOUT = "mp2_controles";
    private final Button stick;
    private boolean editing;
    private Button selected;
    private EditorListener editorListener;
    private float pinchStart, pinchSize, dragOffsetX, dragOffsetY;
    private int dragPointer = -1;

    private boolean editorOnly;  // En el launcher: sin juego detrás y con barra propia.
    private final GameOptions labels;
    private final RectF editChip = new RectF(), saveChip = new RectF(), resetChip = new RectF(), smallerChip = new RectF(), biggerChip = new RectF();
    void startEditing(EditorListener listener) { editorOnly = true; editing = true; editorListener = listener; menuMode = false; }
    private void setInGameEditing(boolean on) {
        editing = on;
        selected = null;
        if (on) { updateStick(stickCenterX, stickCenterY); for (Button b : buttons) if (b.down) press(b, false); }
        GameActivity.nativeHudEditing(on);
        invalidate();
    }
    private void drawChip(Canvas canvas, RectF chip, String label, int alpha) {
        stickPaint.setStyle(Paint.Style.FILL);
        stickPaint.setColor(0xFF101010);
        stickPaint.setAlpha(alpha);
        canvas.drawRoundRect(chip, 14, 14, stickPaint);
        stickPaint.setColor(0xFFFFFFFF);
        glowPaint.setStyle(Paint.Style.FILL);
        canvas.drawText(label, chip.centerX(), chip.centerY() + textPaint.getTextSize() / 3, textPaint);
    }
    void setSelectedSize(float size) {
        if (selected == null) return;
        selected.size = Math.max(0.06f, Math.min(0.6f, size));
        place(selected);
        invalidate();
    }
    void saveLayout() {
        android.content.SharedPreferences.Editor editor = getContext().getSharedPreferences(LAYOUT, Context.MODE_PRIVATE).edit();
        for (Button b : allControls()) editor.putFloat(b.id + "_x", b.centerX).putFloat(b.id + "_y", b.centerY).putFloat(b.id + "_s", b.size);
        editor.apply();
    }
    void resetLayout() {
        getContext().getSharedPreferences(LAYOUT, Context.MODE_PRIVATE).edit().clear().apply();
        selected = null;
        layoutControls(getWidth(), getHeight());
        invalidate();
    }
    private List<Button> allControls() {
        List<Button> all = new ArrayList<>(buttons);
        all.add(stick);
        return all;
    }
    private void layoutControls(int w, int h) {
        if (w <= 0 || h <= 0) return;
        android.content.SharedPreferences saved = getContext().getSharedPreferences(LAYOUT, Context.MODE_PRIVATE);
        float unit = h;
        for (Button b : allControls()) {
            float defaultX = b == stick ? 0.28f * unit / w : (w - b.x * unit) / w;
            float defaultY = b == stick ? (h - 0.28f * unit) / h : (h - b.y * unit) / h;
            b.centerX = saved.getFloat(b.id + "_x", defaultX);
            b.centerY = saved.getFloat(b.id + "_y", defaultY);
            b.size = saved.getFloat(b.id + "_s", b.defaultSize);
            place(b);
        }
        stickX = stickCenterX;
        stickY = stickCenterY;
    }
    private void place(Button b) {
        float half = b.size * getHeight() / 2, cx = b.centerX * getWidth(), cy = b.centerY * getHeight();
        b.rect.set(cx - half, cy - half, cx + half, cy + half);
        if (b == stick) { stickCenterX = cx; stickCenterY = cy; stickRadius = half; stickX = cx; stickY = cy; }
    }
    private boolean editTouch(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            float x = event.getX(), y = event.getY();
            if (!editorOnly) {
                if (saveChip.contains(x, y)) { saveLayout(); setInGameEditing(false); return true; }
                if (resetChip.contains(x, y)) { resetLayout(); return true; }
                if (smallerChip.contains(x, y)) { if (selected != null) setSelectedSize(selected.size - 0.02f); return true; }
                if (biggerChip.contains(x, y)) { if (selected != null) setSelectedSize(selected.size + 0.02f); return true; }
            }
            selected = null;
            for (Button b : allControls()) if (b.rect.contains(x, y)) selected = b;
            if (selected != null) {
                dragPointer = event.getPointerId(0);
                dragOffsetX = selected.rect.centerX() - x;
                dragOffsetY = selected.rect.centerY() - y;
                if (editorListener != null) editorListener.onSelected(selected.size);
            }
        } else if (action == MotionEvent.ACTION_POINTER_DOWN && selected != null && event.getPointerCount() == 2) {
            pinchStart = fingerDistance(event);
            pinchSize = selected.size;
            dragPointer = -1;
        } else if (action == MotionEvent.ACTION_MOVE && selected != null) {
            if (event.getPointerCount() >= 2 && pinchStart > 0) {
                setSelectedSize(pinchSize * fingerDistance(event) / pinchStart);
                if (editorListener != null) editorListener.onSelected(selected.size);
            } else if (dragPointer >= 0) {
                int index = event.findPointerIndex(dragPointer);
                if (index >= 0) {
                    float half = selected.rect.width() / 2;
                    selected.centerX = Math.max(half, Math.min(getWidth() - half, event.getX(index) + dragOffsetX)) / getWidth();
                    selected.centerY = Math.max(half, Math.min(getHeight() - half, event.getY(index) + dragOffsetY)) / getHeight();
                    place(selected);
                }
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            dragPointer = -1;
            pinchStart = 0;
        }
        invalidate();
        return true;
    }
    private static float fingerDistance(MotionEvent event) {
        return (float) Math.hypot(event.getX(0) - event.getX(1), event.getY(0) - event.getY(1));
    }

    private Bitmap icon(int id) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        return BitmapFactory.decodeResource(getResources(), id, options);
    }

    @Override protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        layoutControls(w, h);
        float u = h * 0.085f, top = h * 0.02f, gap = h * 0.015f;
        textPaint.setTextSize(h * 0.036f);
        editChip.set(h * 0.02f, top, h * 0.02f + u * 3f, top + u);
        float x = w * 0.5f - u * 4.6f;
        smallerChip.set(x, top, x + u, top + u); x += u + gap;
        biggerChip.set(x, top, x + u, top + u); x += u + gap;
        resetChip.set(x, top, x + u * 3.2f, top + u); x += u * 3.2f + gap;
        saveChip.set(x, top, x + u * 3.2f, top + u);
    }

    @Override protected void onDraw(Canvas canvas) {
        if (gamepadActive && !editing) return;  // Con mando no se dibuja nada.
        // Fuera del gameplay solo queda Pausa (Esc): es lo que salta cinemáticas y novela gráfica.
        if (menuMode && !editing) { draw(canvas, pause); return; }
        stickPaint.setStyle(Paint.Style.STROKE);
        stickPaint.setStrokeWidth(4);
        stickPaint.setAlpha(110);
        canvas.drawCircle(stickCenterX, stickCenterY, stickRadius, stickPaint);
        stickPaint.setStyle(Paint.Style.FILL);
        stickPaint.setAlpha(stickActive ? 150 : 80);
        canvas.drawCircle(stickX, stickY, stickRadius * 0.42f, stickPaint);
        for (Button b : buttons) draw(canvas, b);
        if (editing && selected != null) {
            stickPaint.setStyle(Paint.Style.STROKE);
            stickPaint.setStrokeWidth(5);
            stickPaint.setAlpha(255);
            canvas.drawRoundRect(selected.rect, 16, 16, stickPaint);
        }
        if (editorOnly) return;
        textPaint.setAlpha(230);
        if (editing) {
            drawChip(canvas, smallerChip, "−", 200);
            drawChip(canvas, biggerChip, "+", 200);
            drawChip(canvas, resetChip, labels.tr("Restablecer", "Reset", "Restaurar"), 200);
            drawChip(canvas, saveChip, labels.tr("Guardar", "Save", "Salvar"), 200);
        } else {
            drawChip(canvas, editChip, labels.tr("Controles", "Controls", "Controles"), 110);
        }
    }

    private void draw(Canvas canvas, Button b) {
        if (!b.down) {
            iconPaint.setAlpha(idleAlpha);
            iconPaint.setColorFilter(null);
            canvas.drawBitmap(b.icon, null, b.rect, iconPaint);
            return;
        }
        // Pulsado: halo detrás, icono más grande, opaco y teñido. Solo alfa, tinte y escala sobre el icono original.
        float cx = b.rect.centerX(), cy = b.rect.centerY(), radius = b.rect.width() * 0.62f;
        glowPaint.setShader(new android.graphics.RadialGradient(cx, cy, radius, 0x99FFC04D, 0x00FFC04D, android.graphics.Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, radius, glowPaint);
        RectF rect = new RectF(b.rect);
        float grow = b.rect.width() * 0.08f;
        rect.inset(-grow, -grow);
        iconPaint.setAlpha(255);
        iconPaint.setColorFilter(pressedTint);
        canvas.drawBitmap(b.icon, null, rect, iconPaint);
        iconPaint.setColorFilter(null);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (editing) return editTouch(event);
        // Tocar la pantalla con el mando conectado vuelve a mostrar los controles táctiles.
        if (gamepadActive && event.getActionMasked() == MotionEvent.ACTION_DOWN) { gamepadActive = false; invalidate(); }
        int action = event.getActionMasked(), index = event.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN: case MotionEvent.ACTION_POINTER_DOWN:
                down(event.getPointerId(index), event.getX(index), event.getY(index));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) move(event.getPointerId(i), event.getX(i), event.getY(i));
                break;
            case MotionEvent.ACTION_UP: case MotionEvent.ACTION_POINTER_UP:
                up(event.getPointerId(index), event.getX(index), event.getY(index));
                break;
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < event.getPointerCount(); i++) up(event.getPointerId(i), event.getX(i), event.getY(i));
                break;
        }
        invalidate();
        return true;
    }

    private Button buttonAt(float x, float y) {
        if (menuMode) return pause.rect.contains(x, y) ? pause : null;
        for (Button b : buttons) if (b.rect.contains(x, y)) return b;
        return null;
    }

    private void down(int id, float x, float y) {
        Touch touch = new Touch();
        touch.lastX = x; touch.lastY = y;
        touches.put(id, touch);
        if (!menuMode && editChip.contains(x, y)) { setInGameEditing(true); touches.remove(id); return; }
        Button b = buttonAt(x, y);
        if (b != null) { touch.role = ROLE_BUTTON; touch.button = b; press(b, true); return; }
        if (menuMode) {
            touch.role = ROLE_MENU;
            GameActivity.nativePointer(x / getWidth(), y / getHeight(), 0);
            return;
        }
        if (Math.hypot(x - stickCenterX, y - stickCenterY) < stickRadius * 2.2f) { touch.role = ROLE_STICK; stickActive = true; updateStick(x, y); return; }
        touch.role = ROLE_LOOK;
    }

    private void move(int id, float x, float y) {
        Touch touch = touches.get(id);
        if (touch == null) return;
        if (touch.role == ROLE_STICK) updateStick(x, y);
        else if (touch.role == ROLE_LOOK) GameActivity.nativeMouseMotion((x - touch.lastX) * lookSensitivity * (aim.down ? PRECISION_FACTOR : 1f), (y - touch.lastY) * lookSensitivity * (aim.down ? PRECISION_FACTOR : 1f));
        else if (touch.role == ROLE_MENU) GameActivity.nativePointer(x / getWidth(), y / getHeight(), 1);
        touch.lastX = x; touch.lastY = y;
    }

    private void up(int id, float x, float y) {
        Touch touch = touches.get(id);
        if (touch == null) return;
        touches.remove(id);
        switch (touch.role) {
            case ROLE_STICK: stickActive = false; updateStick(stickCenterX, stickCenterY); break;
            case ROLE_BUTTON: press(touch.button, false); break;
            case ROLE_MENU: GameActivity.nativePointer(x / getWidth(), y / getHeight(), 2); break;
            default: break;
        }
    }

    private void setMenuMode(boolean menu) {
        menuMode = menu;
        if (menu) {
            updateStick(stickCenterX, stickCenterY);
            for (Button b : buttons) if (b.down) press(b, false);
        }
    }

    private void press(Button b, boolean down) {
        if (b.down == down) return;
        b.down = down;
        if (down) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        if (b.wheel != 0) { if (down) GameActivity.nativeMouseWheel(b.wheel * 120); return; }
        if (b.mouse >= 0) GameActivity.nativeMouseButton(b.mouse, down);
        else GameActivity.nativeKey(b.key, down);
    }

    private void updateStick(float x, float y) {
        float dx = x - stickCenterX, dy = y - stickCenterY, length = (float) Math.hypot(dx, dy);
        if (length > stickRadius) { dx *= stickRadius / length; dy *= stickRadius / length; }
        stickX = stickCenterX + dx;
        stickY = stickCenterY + dy;
        float threshold = stickRadius * 0.28f;
        forward = key(KEY_W, forward, dy < -threshold);
        back = key(KEY_S, back, dy > threshold);
        left = key(KEY_A, left, dx < -threshold);
        right = key(KEY_D, right, dx > threshold);
    }

    private boolean key(int scan, boolean previous, boolean now) {
        if (previous != now) GameActivity.nativeKey(scan, now);
        return now;
    }
}
