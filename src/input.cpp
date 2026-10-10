#include "engine/input.hpp"
#include <limits>

namespace engine {
namespace {
bool integer(double value,double low,double high) {
    return std::isfinite(value)&&value>=low&&value<=high&&std::floor(value)==value;
}
bool valid(const InputPacket& p) {
    if(!integer(p.type,0,6)||!integer(p.source,0,UINT32_MAX)||
       !std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.pixelX)||!std::isfinite(p.pixelY)||
       std::abs(p.x)>1e7||std::abs(p.y)>1e7||std::abs(p.pixelX)>1e7||std::abs(p.pixelY)>1e7||
       !integer(p.buttons,0,UINT32_MAX)||!integer(p.device,0,2)||
       !std::isfinite(p.pressure)||p.pressure<0||p.pressure>1||!std::isfinite(p.timestamp)) return false;
    return p.type<=1 ? integer(p.id,1,UINT32_MAX) : integer(p.id,INT32_MIN,INT32_MAX);
}
}
bool InputSystem::submit(std::span<const InputPacket> packets) {
    if(packets.size()>maxQueuedEvents-pending_.size()||
       !std::all_of(packets.begin(),packets.end(),valid)) {
        // Discard the rejected batch atomically and release existing held input.
        pending_.clear();InputPacket reset;reset.type=6;pending_.push_back(reset);return false;
    }
    pending_.insert(pending_.end(),packets.begin(),packets.end());return true;
}
const InputActionState& InputSystem::action(InputActionId id) const {
    static const InputActionState empty;
    const auto it=actions_.find(id);return it==actions_.end() ? empty : it->second.state;
}
void InputSystem::cancel() {
    for(auto& [id,a]:actions_) {
        (void)id;a.state.released|=a.state.held;a.state.held=false;
        a.state.pressed=false;a.state.value={};a.state.delta={};a.sources.clear();
    }
    pointers_.clear();
}
void InputSystem::reset() {
    cancel();pending_.clear();events_.clear();
    for(auto& [id,a]:actions_) { (void)id;a.state={}; }
}
void InputSystem::beginFrame() {
    for(auto& [id,a]:actions_) { (void)id;a.state.pressed=false;a.state.released=false;a.state.delta={}; }
    events_.clear();events_.swap(pending_);
    for(const auto& packet:events_) apply(packet);
}
void InputSystem::apply(const InputPacket& p) {
    const auto type=static_cast<InputEventType>(int(p.type));
    if(type==InputEventType::CancelAll) { cancel();return; }
    if(type==InputEventType::ActionValue||type==InputEventType::ActionDelta) {
        auto& a=actions_[static_cast<InputActionId>(p.id)];
        const Vec2 value{float(p.x),float(p.y)};
        if(type==InputEventType::ActionDelta) { a.state.delta.x+=value.x;a.state.delta.y+=value.y;return; }
        const bool wasHeld=a.state.held;
        if(value.x!=0||value.y!=0) a.sources[static_cast<std::uint32_t>(p.source)]=value;
        else a.sources.erase(static_cast<std::uint32_t>(p.source));
        Vec2 sum{};for(const auto& [source,v]:a.sources) { (void)source;sum.x+=v.x;sum.y+=v.y; }
        a.state.value={std::clamp(sum.x,-1.0f,1.0f),std::clamp(sum.y,-1.0f,1.0f)};
        a.state.held=!a.sources.empty();a.state.pressed|=!wasHeld&&a.state.held;
        a.state.released|=wasHeld&&!a.state.held;return;
    }
    const auto id=static_cast<std::int32_t>(p.id);
    if(type==InputEventType::PointerUp||type==InputEventType::PointerCancel) { pointers_.erase(id);return; }
    if(type==InputEventType::PointerDown||pointers_.contains(id))
        pointers_[id]={id,{float(p.x),float(p.y)},{float(p.pixelX),float(p.pixelY)},
            static_cast<std::uint32_t>(p.buttons),static_cast<InputDevice>(int(p.device)),float(p.pressure)};
}
InputBindings::InputBindings(InputSystem& input,std::uint32_t firstSource)
    : input_(input),firstSource_(firstSource) {
    if(firstSource==0||firstSource>UINT32_MAX-1023) throw std::invalid_argument("Invalid binding source range");
}
InputBindings::~InputBindings() { cancel(); }
void InputBindings::cancel() {
    std::vector<InputPacket> releases;
    for(std::size_t i=0;i<held_.size();++i) if(held_[i]) {
        InputPacket p;p.id=bindings_[i].action;p.source=firstSource_+i;releases.push_back(p);
    }
    input_.submit(releases);std::fill(held_.begin(),held_.end(),false);
}
void InputBindings::setBindings(std::span<const InputBinding> bindings) {
    if(bindings.size()>1024) throw std::invalid_argument("Too many input bindings");
    for(const auto& b:bindings) if(!b.context||!b.control||!b.action||
        !std::isfinite(b.value.x)||!std::isfinite(b.value.y)||std::abs(b.value.x)>1||std::abs(b.value.y)>1)
        throw std::invalid_argument("Invalid input binding");
    std::vector<InputBinding> candidate(bindings.begin(),bindings.end());
    std::vector<bool> held(candidate.size(),false);
    cancel();bindings_.swap(candidate);held_.swap(held);
}
void InputBindings::setContexts(std::span<const InputContext> contexts) {
    if(contexts.size()>64) throw std::invalid_argument("Too many input contexts");
    for(std::size_t i=0;i<contexts.size();++i) {
        if(!contexts[i].id) throw std::invalid_argument("Invalid input context");
        for(std::size_t j=0;j<i;++j) if(contexts[i].id==contexts[j].id) throw std::invalid_argument("Duplicate input context");
    }
    std::vector<InputContext> candidate(contexts.begin(),contexts.end());
    cancel();contexts_.swap(candidate);
}
bool InputBindings::control(InputControlId control,float value) {
    if(!control||!std::isfinite(value)||std::abs(value)>1e7f) throw std::invalid_argument("Invalid input control");
    std::vector<InputPacket> packets;
    std::vector<std::size_t> matched;
    for(auto context=contexts_.rbegin();context!=contexts_.rend();++context) {
        for(std::size_t i=0;i<bindings_.size();++i) {
            const auto& b=bindings_[i];if(b.context!=context->id||b.control!=control) continue;
            InputPacket p;p.type=b.displacement ? 1 : 0;p.id=b.action;p.source=firstSource_+i;
            const float amount=b.displacement ? value : std::clamp(value,-1.f,1.f);
            p.x=b.value.x*amount;p.y=b.value.y*amount;packets.push_back(p);matched.push_back(i);
        }
        if(!matched.empty()||context->blockLower) break;
    }
    if(packets.empty()) return false;
    if(!input_.submit(packets)) { std::fill(held_.begin(),held_.end(),false);return false; }
    for(const auto i:matched) if(!bindings_[i].displacement) held_[i]=value!=0&&(bindings_[i].value.x!=0||bindings_[i].value.y!=0);
    return true;
}
} // namespace engine
