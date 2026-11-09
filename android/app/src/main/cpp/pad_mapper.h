#pragma once
// Mando físico para Max Payne 2: cada botón se traduce a la tecla o al botón de ratón
// que el juego tiene asignado de fábrica (docs/INPUT_MAPPING_PC.md), igual que el HUD
// táctil. El stick derecho mueve el ratón, así que mira en el juego y mueve el cursor
// en los menús. El significado de A, B y la cruceta cambia entre menú y gameplay.
#include <android/input.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include "guest_runtime.h"

class PadMapper {
public:
    // DIK de las acciones medidas en el juego.
    enum : unsigned { kW = 0x11, kS = 0x1F, kA = 0x1E, kD = 0x20, kJump = 0x39, kCrouch = 0x2E, kReload = 0x13, kUse = 0x12,
        kDodge = 0x2A, kNext = 0x1A, kPrev = 0x1B, kPainkiller = 0x0F, kZoom = 0x10, kBest = 0x22, kEscape = 0x01,
        kUp = 0xC8, kDown = 0xD0, kLeft = 0xCB, kRight = 0xCD };
    static constexpr unsigned kMouse = 0x1000;  // kMouse + n: botón n del ratón

    static bool isPad(const AInputEvent* event) {
        int32_t source = AInputEvent_getSource(event);
        return (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD || (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
    }

    // Devuelve true si el evento era del mando.
    bool key(const AInputEvent* event) {
        if (!isPad(event)) return false;
        int32_t code = AKeyEvent_getKeyCode(event), action = AKeyEvent_getAction(event);
        if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) return true;
        bool down = action == AKEY_EVENT_ACTION_DOWN;
        if (down && AKeyEvent_getRepeatCount(event) > 0) return true;
        used = true;
        std::lock_guard<std::mutex> lock(mutex);
        if (down) {
            unsigned target = mapButton(code, guestScreenMode() == 2);
            if (!target) return true;
            held[code] = target;
            send(target, true);
        } else {
            auto found = held.find(code);
            if (found == held.end()) return true;
            send(found->second, false);
            held.erase(found);
        }
        return true;
    }

    bool motion(const AInputEvent* event) {
        if (!isPad(event)) return false;
        auto axis = [&](int32_t a) { return AMotionEvent_getAxisValue(event, a, 0); };
        std::lock_guard<std::mutex> lock(mutex);
        leftX = axis(AMOTION_EVENT_AXIS_X); leftY = axis(AMOTION_EVENT_AXIS_Y);
        rightX = axis(AMOTION_EVENT_AXIS_Z); rightY = axis(AMOTION_EVENT_AXIS_RZ);
        float lt = std::max(axis(AMOTION_EVENT_AXIS_LTRIGGER), axis(AMOTION_EVENT_AXIS_BRAKE));
        float rt = std::max(axis(AMOTION_EVENT_AXIS_RTRIGGER), axis(AMOTION_EVENT_AXIS_GAS));
        // Gatillos: RT dispara, LT es Bullet Time (botones izquierdo y derecho del ratón).
        trigger(rtDown, rt, kMouse + 0);
        trigger(ltDown, lt, kMouse + 1);
        // Cruceta como ejes (muchos mandos la envían así): flechas en menú, acciones en juego.
        hat(hatX, axis(AMOTION_EVENT_AXIS_HAT_X), AKEYCODE_DPAD_LEFT, AKEYCODE_DPAD_RIGHT);
        hat(hatY, axis(AMOTION_EVENT_AXIS_HAT_Y), AKEYCODE_DPAD_UP, AKEYCODE_DPAD_DOWN);
        if (std::fabs(leftX) > 0.3f || std::fabs(leftY) > 0.3f || std::fabs(rightX) > 0.3f || std::fabs(rightY) > 0.3f || lt > 0.3f || rt > 0.3f) used = true;
        return true;
    }

    // Cada vuelta del bucle principal: movimiento continuo del stick izquierdo y del derecho.
    void update() {
        auto now = std::chrono::steady_clock::now();
        float seconds = std::min(0.05f, std::chrono::duration<float>(now - last).count());
        last = now;
        std::lock_guard<std::mutex> lock(mutex);
        bool gameplay = guestScreenMode() == 2;
        // Stick izquierdo: WASD en juego; en los menús también mueve el cursor.
        direction(forward, gameplay && leftY < -kStickThreshold, kW);
        direction(back, gameplay && leftY > kStickThreshold, kS);
        direction(left, gameplay && leftX < -kStickThreshold, kA);
        direction(right, gameplay && leftX > kStickThreshold, kD);
        float lookX = curve(rightX), lookY = curve(rightY);
        if (!gameplay) { lookX += curve(leftX); lookY += curve(leftY); }
        float speed = gameplay ? kLookSpeed : kCursorSpeed;
        if (lookX != 0.f || lookY != 0.f) addGuestMouseMotion(lookX * speed * seconds, lookY * speed * seconds);
    }

    void releaseAll() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& [code, target] : held) send(target, false);
        held.clear();
        for (auto* pressed : {&forward, &back, &left, &right}) *pressed = false;
        for (unsigned scan : {unsigned(kW), unsigned(kS), unsigned(kA), unsigned(kD)}) setGuestKey(scan, false);
        if (rtDown) send(kMouse + 0, false);
        if (ltDown) send(kMouse + 1, false);
        rtDown = ltDown = false;
        leftX = leftY = rightX = rightY = 0;
    }

    std::atomic<bool> used{false};  // El HUD lo consulta para ocultar los botones táctiles.

private:
    static constexpr float kStickThreshold = 0.35f, kDeadZone = 0.15f, kLookSpeed = 900.f, kCursorSpeed = 420.f;
    std::mutex mutex;
    std::map<int32_t, unsigned> held;  // botón físico -> lo que se envió al pulsarlo
    float leftX = 0, leftY = 0, rightX = 0, rightY = 0, hatX = 0, hatY = 0;
    bool forward = false, back = false, left = false, right = false, rtDown = false, ltDown = false;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();

    static float curve(float v) {
        float a = std::fabs(v);
        if (a < kDeadZone) return 0.f;
        float t = (a - kDeadZone) / (1.f - kDeadZone);
        return std::copysign(t * t, v);  // Cuadrática: precisión cerca del centro.
    }
    static void send(unsigned target, bool down) {
        if (target >= kMouse) setGuestMouseButton(target - kMouse, down);
        else setGuestKey(target, down);
    }
    static unsigned mapButton(int32_t code, bool gameplay) {
        if (!gameplay) {
            switch (code) {
            case AKEYCODE_BUTTON_A: return kMouse + 0;  // Clic: menús y botones de la novela gráfica
            case AKEYCODE_BUTTON_B: case AKEYCODE_BUTTON_START: case AKEYCODE_BUTTON_SELECT: case AKEYCODE_BACK: return kEscape;
            case AKEYCODE_DPAD_UP: return kUp;
            case AKEYCODE_DPAD_DOWN: return kDown;
            case AKEYCODE_DPAD_LEFT: return kLeft;
            case AKEYCODE_DPAD_RIGHT: return kRight;
            default: return 0;
            }
        }
        switch (code) {
        case AKEYCODE_BUTTON_A: return kJump;
        case AKEYCODE_BUTTON_B: return kCrouch;
        case AKEYCODE_BUTTON_X: return kReload;
        case AKEYCODE_BUTTON_Y: return kUse;
        case AKEYCODE_BUTTON_L1: return kDodge;       // Shootdodge con la dirección
        case AKEYCODE_BUTTON_R1: return kNext;
        case AKEYCODE_DPAD_LEFT: return kPrev;
        case AKEYCODE_DPAD_RIGHT: return kNext;
        case AKEYCODE_DPAD_UP: return kPainkiller;
        case AKEYCODE_DPAD_DOWN: return kBest;
        case AKEYCODE_BUTTON_THUMBL: return kCrouch;
        case AKEYCODE_BUTTON_THUMBR: return kZoom;    // Zoom del francotirador
        case AKEYCODE_BUTTON_L2: return kMouse + 1;   // Mandos que envían los gatillos como botones
        case AKEYCODE_BUTTON_R2: return kMouse + 0;
        case AKEYCODE_BUTTON_START: case AKEYCODE_BUTTON_SELECT: case AKEYCODE_BACK: return kEscape;
        default: return 0;
        }
    }
    void trigger(bool& state, float value, unsigned target) {
        bool now = state ? value > 0.25f : value > 0.5f;
        if (now != state) { state = now; send(target, now); }
    }
    void direction(bool& state, bool now, unsigned scan) {
        if (now != state) { state = now; setGuestKey(scan, now); }
    }
    // Cruceta enviada como ejes: se convierte en pulsaciones de las teclas de la cruceta.
    void hat(float& state, float value, int32_t negative, int32_t positive) {
        int previous = state < -0.5f ? -1 : state > 0.5f ? 1 : 0, now = value < -0.5f ? -1 : value > 0.5f ? 1 : 0;
        state = value;
        if (previous == now) return;
        auto release = [&](int32_t code) { auto f = held.find(code); if (f != held.end()) { send(f->second, false); held.erase(f); } };
        if (previous) release(previous < 0 ? negative : positive);
        if (now) {
            int32_t code = now < 0 ? negative : positive;
            unsigned target = mapButton(code, guestScreenMode() == 2);
            if (target) { held[code] = target; send(target, true); }
        }
    }
};
