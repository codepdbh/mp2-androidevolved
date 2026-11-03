#include "pc_action_mapper.h"
#include "touch_engine.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace mp2r;
static void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FALLO: " << what << "\n"; std::exit(1); }
}
int main() {
    TouchEngine touch;
    PcActionMapper mapper;

    // Mover, mirar y disparar con dedos independientes, sin interferir.
    touch.down(1, Action::Move, -0.8f, -0.8f);
    touch.down(2, Action::Look, 0.10f, 0.10f);
    touch.down(3, Action::Fire);
    auto f = mapper.map(touch.consume());
    check(f[PcControl::Run].held && f[PcControl::StrafeLeft].held, "diagonal adelante-izquierda");
    check(!f[PcControl::WalkBack].held && !f[PcControl::StrafeRight].held, "sin direcciones opuestas");
    check(f[PcControl::Shoot].pressed && f[PcControl::Shoot].held, "disparo con flanco");
    check(f.lookX == 0 && f.lookY == 0, "la cámara no salta al apoyar el dedo");

    touch.move(2, 0.30f, 0.05f);
    f = mapper.map(touch.consume());
    check(f.lookX > 0.19f && f.lookX < 0.21f && f.lookY < 0, "delta de cámara");
    check(f[PcControl::Shoot].held && !f[PcControl::Shoot].pressed, "disparo sostenido sin flanco nuevo");

    // Shootdodge: modificador original junto con la dirección ya mantenida.
    touch.down(4, Action::Shootdodge);
    f = mapper.map(touch.consume());
    check(f[PcControl::DodgeModifier].pressed, "Shootdodge usa DODGEMODIFIER");
    check(f[PcControl::StrafeLeft].held && f[PcControl::Run].held, "Shootdodge conserva el vector de movimiento");
    check(!f[PcControl::SlowMotion].held, "Shootdodge y Bullet Time son controles distintos");
    touch.up(4);
    f = mapper.map(touch.consume());
    check(f[PcControl::DodgeModifier].released, "liberación de Shootdodge");

    // Una pulsación entre dos frames conserva ambos flancos.
    touch.down(5, Action::BulletTime);
    touch.up(5);
    f = mapper.map(touch.consume());
    check(f[PcControl::SlowMotion].pressed && f[PcControl::SlowMotion].released && !f[PcControl::SlowMotion].held,
          "toque corto de Bullet Time");

    // Zona muerta del joystick.
    touch.move(1, 0.1f, -0.1f);
    f = mapper.map(touch.consume());
    check(!f[PcControl::Run].held && !f[PcControl::StrafeLeft].held, "zona muerta");
    check(f[PcControl::Run].released, "flanco al entrar en la zona muerta");

    // Soltar todo (pérdida de foco) no deja controles retenidos.
    touch.move(1, 0.9f, 0.9f);
    mapper.map(touch.consume());
    touch.cancel();
    f = mapper.map(touch.consume());
    for (const auto& c : f.controls) check(!c.held, "cancelación libera todo");
    check(f[PcControl::WalkBack].released && f[PcControl::Shoot].released, "flancos de liberación tras cancelar");

    // Eje invertido configurable.
    PcActionMapper inverted({0.35f, 2.0f, 1.0f, true});
    TouchEngine t2;
    t2.down(1, Action::Look, 0.0f, 0.0f);
    inverted.map(t2.consume());
    t2.move(1, 0.1f, 0.1f);
    f = inverted.map(t2.consume());
    check(f.lookX > 0.19f && f.lookY < -0.09f, "sensibilidad e inversión");

    // Los nombres son los de la tabla de MaxPayne2.exe.
    check(!std::strcmp(pcControlName(PcControl::SlowMotion), "SLOWMOTION"), "nombre SLOWMOTION");
    check(pcDefaultBinding(PcControl::DodgeModifier).code == 0x2A, "Shootdodge por Mayús izquierda");
    check(pcDefaultBinding(PcControl::SlowMotion).type == 2 && pcDefaultBinding(PcControl::SlowMotion).code == 1, "Bullet Time por botón derecho");
    check(pcDefaultBinding(PcControl::BulletTime).type == 0, "BULLETTIME sin asignar");
    check(!std::strcmp(pcControlName(PcControl::DodgeModifier), "DODGEMODIFIER"), "nombre DODGEMODIFIER");
    check(!std::strcmp(pcControlName(PcControl::Shoot), "SHOOT"), "nombre SHOOT");
    std::cout << "PASS: mapeador PC, multitouch, Shootdodge con vector, toques cortos, cancelación e inversión\n";
}
