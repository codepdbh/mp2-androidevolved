#include "pc_action_mapper.h"
#include <cmath>

namespace mp2r {
const char* pcControlName(PcControl control) {
    static constexpr const char* names[] = {
        "RUN", "WALKBACK", "STRAFELEFT", "STRAFERIGHT", "SHOOT", "ATTACK", "RELOAD", "JUMP", "CROUCH",
        "DODGELEFT", "DODGERIGHT", "DODGEFORWARD", "DODGEBACKWARD", "DODGEMODIFIER", "USE",
        "BESTWEAPON", "NEXTWEAPON", "PREVIOUSWEAPON", "PAUSE", "PAINKILLER", "SNIPERZOOM",
        "SLOWMOTION", "BULLETTIME"};
    return names[static_cast<std::size_t>(control)];
}

PcBinding pcDefaultBinding(PcControl control) {
    switch (control) {
    case PcControl::Run: return {1, 0x11};            // W
    case PcControl::WalkBack: return {1, 0x1F};       // S
    case PcControl::StrafeLeft: return {1, 0x1E};     // A
    case PcControl::StrafeRight: return {1, 0x20};    // D
    case PcControl::Shoot: return {2, 0};             // botón izquierdo
    case PcControl::Attack: return {1, 0x21};         // F
    case PcControl::Reload: return {1, 0x13};         // R
    case PcControl::Jump: return {1, 0x39};           // Espacio
    case PcControl::Crouch: return {1, 0x2E};         // C
    case PcControl::DodgeModifier: return {1, 0x2A};  // Mayús izquierda
    case PcControl::Use: return {1, 0x12};            // E
    case PcControl::BestWeapon: return {1, 0x22};     // G
    case PcControl::NextWeapon: return {1, 0x1A};     // [
    case PcControl::PreviousWeapon: return {1, 0x1B}; // ]
    case PcControl::Pause: return {1, 0x19};          // P
    case PcControl::Painkiller: return {1, 0x0F};     // Tab
    case PcControl::SniperZoom: return {1, 0x10};     // Q
    case PcControl::SlowMotion: return {2, 1};        // botón derecho
    default: return {};                               // DODGE* y BULLETTIME: sin asignar
    }
}

void PcActionMapper::reset() {
    held_.fill(false);
    haveLook_ = false;
}

PcInputFrame PcActionMapper::map(const Snapshot& touch) {
    const auto at = [&](Action a) -> const ActionState& { return touch[static_cast<std::size_t>(a)]; };
    std::array<bool, static_cast<std::size_t>(PcControl::Count)> now{};
    std::array<bool, static_cast<std::size_t>(PcControl::Count)> tap{};
    const auto set = [&](PcControl c, bool on) { now[static_cast<std::size_t>(c)] = on; };
    // Una pulsación más corta que un frame también debe llegar al juego.
    const auto press = [&](PcControl c, const ActionState& s) {
        set(c, s.held);
        if (s.pressed && !s.held) tap[static_cast<std::size_t>(c)] = true;
    };

    const auto& move = at(Action::Move);
    const bool walking = move.held;
    // Coordenadas de pantalla: y negativa es hacia arriba, es decir, hacia delante.
    set(PcControl::Run, walking && move.y <= -config_.moveThreshold);
    set(PcControl::WalkBack, walking && move.y >= config_.moveThreshold);
    set(PcControl::StrafeLeft, walking && move.x <= -config_.moveThreshold);
    set(PcControl::StrafeRight, walking && move.x >= config_.moveThreshold);

    press(PcControl::Shoot, at(Action::Fire));
    press(PcControl::SniperZoom, at(Action::Aim));
    press(PcControl::Reload, at(Action::Reload));
    press(PcControl::Jump, at(Action::Jump));
    press(PcControl::Use, at(Action::Interact));
    // El Bullet Time del jugador es SLOWMOTION: BULLETTIME no tiene tecla por defecto.
    press(PcControl::SlowMotion, at(Action::BulletTime));
    press(PcControl::NextWeapon, at(Action::ChangeWeapon));
    press(PcControl::Painkiller, at(Action::UsePainkiller));
    press(PcControl::Pause, at(Action::Pause));
    // Shootdodge: el modificador original junto con la dirección que ya se mantiene.
    // El salto lo ejecuta el juego; sin vector de movimiento, el original decide.
    press(PcControl::DodgeModifier, at(Action::Shootdodge));

    PcInputFrame frame;
    for (std::size_t i = 0; i < now.size(); ++i) {
        auto& out = frame.controls[i];
        out.held = now[i];
        out.pressed = (now[i] && !held_[i]) || tap[i];
        out.released = (!now[i] && held_[i]) || tap[i];
        held_[i] = now[i];
    }

    const auto& look = at(Action::Look);
    if (look.held) {
        if (haveLook_) {
            frame.lookX = (look.x - lastLookX_) * config_.lookSensitivityX;
            frame.lookY = (look.y - lastLookY_) * config_.lookSensitivityY * (config_.invertY ? -1.0f : 1.0f);
        }
        lastLookX_ = look.x;
        lastLookY_ = look.y;
        haveLook_ = true;
    } else {
        haveLook_ = false;
    }
    return frame;
}
}
