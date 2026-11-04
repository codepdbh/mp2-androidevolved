#pragma once
#include <string>
#include <vector>
// Maps the owner's PE data, binds imports, tests lifted code and attempts CRT
// entry until the first unsupported host service. Returns an honest status.
std::string connectGuestRuntime(const char* executable);
void requestGuestRuntimeStop();
void setGuestPaused(bool paused);
void setGuestDisplaySize(unsigned width,unsigned height);
void setGuestKey(unsigned scan,bool down);
void clearGuestInput();
bool guestDrivingControls();
// Physical gamepad seen by the game as a DirectInput joystick: axes X, Y, Z
// (triggers), Rx, Ry, Rz in -1..1; buttons A B X Y LB RB Back Start LS RS as bits.
void setGuestPad(const float axes[6],uint32_t buttons,int hatX,int hatY);
void setGuestPadConnected(bool connected);
// True while the game has the joystick acquired (then the pad stops emulating keys).
bool guestGamepadInUse();
// Launcher option; only before the guest boots.
void setGuestWidescreen(bool enabled);
// Ratón del juego: la mirada es relativa, y SHOOT y SLOWMOTION son botones de ratón.
void addGuestMouseMotion(float dx,float dy);
void setGuestMouseButton(unsigned button,bool down);
void addGuestMouseWheel(int delta);
// True mientras corre X_MenuModeBase::update: hay un menú en pantalla.
bool guestMenuActive();
int guestScreenMode();
// Archivos que el juego no debe ver, separados por ";" (los .ras de otros idiomas).
void setGuestHiddenFiles(const char* names);
// Opciones del launcher para la línea de órdenes del juego; antes de arrancar.
void setGuestFlags(const char* flags);
/** Touch as mouse: x/y as a fraction of the game view; phase 0 down, 1 move, 2 up (left button). */
void setGuestPointer(float x,float y,int phase);
/** Posts WM_CHAR messages (the game's text fields read those); 8 is backspace. */
void typeGuestText(const std::vector<uint32_t>& characters);
void setGuestResolution(unsigned width,unsigned height);
void setGuestGraphicsQuality(unsigned level);
void setGuestFrameLimit(unsigned framesPerSecond);
