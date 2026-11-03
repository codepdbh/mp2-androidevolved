package com.mp2r.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
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
        final Bitmap icon; final float x, y, size; final int key, mouse, wheel; boolean down; final RectF rect = new RectF();
        Button(Bitmap icon, float x, float y, float size, int key, int mouse, int wheel) {
            this.icon = icon; this.x = x; this.y = y; this.size = size; this.key = key; this.mouse = mouse; this.wheel = wheel;
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
    private final int idleAlpha;
    /** El juego dice si hay un menú en pantalla: el modo cambia solo. */
    private final Runnable pollMode = new Runnable() {
        @Override public void run() {
            boolean menu = GameActivity.nativeMenuActive();
            if (menu != menuMode) { setMenuMode(menu); invalidate(); }
            postDelayed(this, 200);
        }
    };
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); post(pollMode); }
    @Override protected void onDetachedFromWindow() { removeCallbacks(pollMode); super.onDetachedFromWindow(); }

    GameHudView(Context context, GameOptions options) {
        super(context);
        lookSensitivity = options.lookSensitivity;
        idleAlpha = options.hudOpacity * 255 / 100;
        setBackgroundColor(0);
        stickPaint.setColor(0xFFFFFFFF);
        textPaint.setColor(0xFFFFFFFF);
        textPaint.setTextAlign(Paint.Align.CENTER);
        // Posición en fracción del alto desde abajo a la derecha, y tamaño en fracción del alto.
        buttons.add(new Button(icon(R.drawable.hud_fire), 0.20f, 0.24f, 0.26f, 0, 0, 0));
        buttons.add(new Button(icon(R.drawable.hud_aim), 0.48f, 0.15f, 0.17f, 0x10, -1, 0));          // SNIPERZOOM: Q
        buttons.add(new Button(icon(R.drawable.hud_bullet_time), 0.46f, 0.38f, 0.19f, 0, 1, 0));      // SLOWMOTION: ratón 1
        buttons.add(new Button(icon(R.drawable.hud_shootdodge), 0.25f, 0.55f, 0.19f, 0x2A, -1, 0));   // DODGEMODIFIER: Mayús izq.
        buttons.add(new Button(icon(R.drawable.hud_jump), 0.70f, 0.20f, 0.17f, 0x39, -1, 0));         // JUMP: Espacio
        buttons.add(new Button(icon(R.drawable.hud_reload), 0.70f, 0.42f, 0.15f, 0x13, -1, 0));       // RELOAD: R
        buttons.add(new Button(icon(R.drawable.hud_interact), 0.47f, 0.60f, 0.15f, 0x12, -1, 0));     // USE: E
        buttons.add(new Button(icon(R.drawable.hud_painkiller), 0.70f, 0.62f, 0.14f, 0x0F, -1, 0));   // PAINKILLER: Tab
        buttons.add(new Button(icon(R.drawable.hud_weapons), 0.92f, 0.14f, 0.15f, 0, -1, 1));         // NEXT: rueda
        pause = new Button(icon(R.drawable.hud_pause), 0.10f, 0.90f, 0.12f, 0x01, -1, 0);             // Esc
        buttons.add(pause);
    }

    private Bitmap icon(int id) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        return BitmapFactory.decodeResource(getResources(), id, options);
    }

    @Override protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        float unit = h;
        for (Button b : buttons) {
            float half = b.size * unit / 2, cx = w - b.x * unit, cy = h - b.y * unit;
            b.rect.set(cx - half, cy - half, cx + half, cy + half);
        }
        stickRadius = 0.16f * unit;
        stickCenterX = 0.28f * unit;
        stickCenterY = h - 0.28f * unit;
        stickX = stickCenterX;
        stickY = stickCenterY;
    }

    @Override protected void onDraw(Canvas canvas) {
        if (menuMode) {
            draw(canvas, pause);
            return;
        }
        stickPaint.setStyle(Paint.Style.STROKE);
        stickPaint.setStrokeWidth(4);
        stickPaint.setAlpha(110);
        canvas.drawCircle(stickCenterX, stickCenterY, stickRadius, stickPaint);
        stickPaint.setStyle(Paint.Style.FILL);
        stickPaint.setAlpha(stickActive ? 150 : 80);
        canvas.drawCircle(stickX, stickY, stickRadius * 0.42f, stickPaint);
        for (Button b : buttons) draw(canvas, b);
    }

    private void draw(Canvas canvas, Button b) {
        iconPaint.setAlpha(b.down ? 255 : idleAlpha);
        RectF rect = b.rect;
        if (b.down) {
            rect = new RectF(b.rect);
            rect.inset(b.rect.width() * 0.05f, b.rect.height() * 0.05f);
        }
        canvas.drawBitmap(b.icon, null, rect, iconPaint);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
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
        Button b = buttonAt(x, y);
        if (b != null) { touch.role = ROLE_BUTTON; touch.button = b; press(b, true); return; }
        if (menuMode) {
            touch.role = ROLE_MENU;
            GameActivity.nativePointer(x / getWidth(), y / getHeight(), 0);
            return;
        }
        if (x < getWidth() * 0.45f) { touch.role = ROLE_STICK; stickActive = true; updateStick(x, y); return; }
        touch.role = ROLE_LOOK;
    }

    private void move(int id, float x, float y) {
        Touch touch = touches.get(id);
        if (touch == null) return;
        if (touch.role == ROLE_STICK) updateStick(x, y);
        else if (touch.role == ROLE_LOOK) GameActivity.nativeMouseMotion((x - touch.lastX) * lookSensitivity, (y - touch.lastY) * lookSensitivity);
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
