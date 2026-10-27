#include "touch_engine.h"
#include <algorithm>
#include <cmath>

namespace mp2r {
static float axis(float v) {
    if (!std::isfinite(v)) throw std::invalid_argument("Non-finite input");
    return std::clamp(v,-1.0f,1.0f);
}
void TouchEngine::update(Action a) {
    auto& s=state_[static_cast<std::size_t>(a)];
    bool held=false; float x=0,y=0;
    for (const auto& [id,c]:contacts_) if(c.action==a) { held=true; x=c.x; y=c.y; }
    s.pressed |= held && !s.held;
    s.released |= !held && s.held;
    s.held=held; s.x=x; s.y=y;
}
void TouchEngine::down(int p,Action a,float x,float y) {
    if (p<0 || static_cast<std::size_t>(a)>=state_.size()) throw std::invalid_argument("Invalid contact");
    x=axis(x); y=axis(y);
    std::scoped_lock lock(mutex_);
    if(contacts_.contains(p)) return;
    contacts_.emplace(p,Contact{a,x,y}); update(a);
}
void TouchEngine::move(int p,float x,float y) {
    x=axis(x); y=axis(y);
    std::scoped_lock lock(mutex_);
    auto it=contacts_.find(p);
    if(it==contacts_.end()) return;
    it->second.x=x; it->second.y=y; update(it->second.action);
}
void TouchEngine::up(int p) {
    std::scoped_lock lock(mutex_);
    auto it=contacts_.find(p); if(it==contacts_.end()) return;
    auto a=it->second.action; contacts_.erase(it); update(a);
}
void TouchEngine::cancel() {
    std::scoped_lock lock(mutex_); contacts_.clear();
    for(std::size_t i=0;i<state_.size();++i) update(static_cast<Action>(i));
}
Snapshot TouchEngine::consume() {
    std::scoped_lock lock(mutex_); auto result=state_;
    for(auto& s:state_) { s.pressed=false; s.released=false; }
    return result;
}
}
