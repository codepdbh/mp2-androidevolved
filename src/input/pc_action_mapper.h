#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "touch_engine.h"

namespace mp2r {
// Acciones del jugador que MaxPayne2.exe lee por nombre de su tabla de controles
// (cadenas RUN...BULLETTIME, docs/INPUT_MAPPING_PC.md). El orden es propio; el
// valor que se entrega al motor sale del nombre, nunca de este índice.
enum class PcControl { Run, WalkBack, StrafeLeft, StrafeRight, Shoot, Attack, Reload, Jump, Crouch,
                       DodgeLeft, DodgeRight, DodgeForward, DodgeBackward, DodgeModifier, Use,
                       BestWeapon, NextWeapon, PreviousWeapon, Pause, Painkiller, SniperZoom,
                       SlowMotion, BulletTime, Count };
const char* pcControlName(PcControl control);

// Lo que el juego asigna por defecto a un control (X_InputControlButton::set medido
// en ejecución). El tipo y el código son los del motor: 1 tecla con su DIK, 2 botón
// del ratón; 0 es sin asignar.
struct PcBinding { int type{}, code{}; };
PcBinding pcDefaultBinding(PcControl control);

struct PcControlState { bool held{}, pressed{}, released{}; };

struct PcInputFrame {
    std::array<PcControlState, static_cast<std::size_t>(PcControl::Count)> controls{};
    float lookX{}, lookY{};   // desplazamiento de cámara de este frame, ya con sensibilidad
    const PcControlState& operator[](PcControl c) const { return controls[static_cast<std::size_t>(c)]; }
};

struct PcMapperConfig {
    float moveThreshold{0.35f};
    float lookSensitivityX{1.0f}, lookSensitivityY{1.0f};
    bool invertY{false};
};

// Convierte acciones semánticas del HUD (TouchEngine) en controles del juego de PC.
// Solo elige qué controles originales se mantienen: la lógica de movimiento,
// Shootdodge y Bullet Time sigue siendo la del juego.
class PcActionMapper {
public:
    explicit PcActionMapper(PcMapperConfig config = {}) : config_(config) {}
    PcInputFrame map(const Snapshot& touch);
    void reset();  // pérdida de foco o cambio de dispositivo: libera todo
private:
    PcMapperConfig config_;
    std::array<bool, static_cast<std::size_t>(PcControl::Count)> held_{};
    bool haveLook_{false};
    float lastLookX_{}, lastLookY_{};
};
}
