#include "touch_engine.h"
#include <iostream>
#include <cstdlib>
using namespace mp2r;
void check(bool ok) { if(!ok) { std::cerr<<"Touch test failed\n"; std::exit(1); } }
auto index(Action a) { return static_cast<std::size_t>(a); }
int main() {
    TouchEngine t;
    t.down(7,Action::Move,0.7f,-0.7f);
    t.down(21,Action::Look,0.3f,0.2f);
    t.down(42,Action::Fire);
    t.down(99,Action::Shootdodge);
    auto s=t.consume();
    check(s[index(Action::Move)].x==0.7f && s[index(Action::Look)].held);
    check(s[index(Action::Fire)].pressed && s[index(Action::Shootdodge)].held);
    t.down(42,Action::Fire); check(!t.consume()[index(Action::Fire)].pressed);
    t.down(43,Action::Fire); t.up(42); check(t.consume()[index(Action::Fire)].held);
    t.up(43); check(t.consume()[index(Action::Fire)].released);
    t.down(8,Action::BulletTime); t.up(8);
    s=t.consume(); check(s[index(Action::BulletTime)].pressed && s[index(Action::BulletTime)].released);
    t.cancel(); s=t.consume(); for(auto a:s) check(!a.held && a.x==0 && a.y==0);
    std::cout<<"PASS: move + look + fire + shootdodge, independent contacts, edges, cancel\n";
}
