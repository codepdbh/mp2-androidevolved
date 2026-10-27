#pragma once
#include <array>
#include <cstddef>
#include <map>
#include <mutex>
#include <stdexcept>

namespace mp2r {
enum class Action { Move, Look, Fire, Aim, Reload, Jump, Interact, BulletTime,
                    Shootdodge, ChangeWeapon, UsePainkiller, Pause, Count };
struct ActionState { float x{}, y{}; bool held{}, pressed{}, released{}; };
using Snapshot = std::array<ActionState, static_cast<std::size_t>(Action::Count)>;
// Semantic events only. No guessed PAL bindings or guest addresses.
class TouchEngine {
public:
    void down(int pointer, Action action, float x=0, float y=0);
    void move(int pointer, float x, float y);
    void up(int pointer);
    void cancel();
    Snapshot consume();
private:
    struct Contact { Action action; float x,y; };
    std::mutex mutex_;
    std::map<int,Contact> contacts_;
    Snapshot state_{};
    void update(Action action);
};
}
