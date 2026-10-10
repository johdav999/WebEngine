#include "engine/particle_emitter_component.hpp"
#include <limits>
#include <functional>

namespace engine {
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool ordered(Vec3 a,Vec3 b) { return a.x<=b.x&&a.y<=b.y&&a.z<=b.z; }
float scaleOf(const Mat4& world) {
    return std::max({std::sqrt(dot(transformDirection(world,{1,0,0}),transformDirection(world,{1,0,0}))),
        std::sqrt(dot(transformDirection(world,{0,1,0}),transformDirection(world,{0,1,0}))),
        std::sqrt(dot(transformDirection(world,{0,0,1}),transformDirection(world,{0,0,1})))});
}
template<class Key> std::pair<std::size_t,float> segment(const std::vector<Key>& keys,float t) {
    t=std::clamp(t,0.0f,1.0f);
    for(std::size_t i=1;i<keys.size();++i) if(t<keys[i].time)
        return {i-1,std::clamp((t-keys[i-1].time)/(keys[i].time-keys[i-1].time),0.0f,1.0f)};
    return {keys.size()-1,0};
}
template<class Key> void validateKeys(const std::vector<Key>& keys) {
    if(keys.empty()||keys.size()>256) throw std::invalid_argument("Particle curves require 1 to 256 keys");
    float previous=-1;
    for(const auto& k:keys) {
        if(!std::isfinite(k.time)||k.time<0||k.time>1||k.time<=previous)
            throw std::invalid_argument("Particle curve times must increase within [0,1]");
        previous=k.time;
    }
}
}
void Flipbook::validate(const Texture& texture) const {
    if(!columns||!rows||columns>texture.width||rows>texture.height||!frames
        ||frames>std::uint64_t(columns)*rows||texture.width%columns||texture.height%rows
        ||!std::isfinite(cycles)||cycles<=0||cycles>1024)
        throw std::invalid_argument("Invalid flipbook grid, frame count or cycles");
}
std::uint32_t Flipbook::frame(float age) const {
    if(!std::isfinite(age)||!frames||!std::isfinite(cycles)||cycles<=0) throw std::invalid_argument("Invalid flipbook age");
    const double progress=std::clamp(double(age),0.0,1.0)*cycles;
    const auto phase=age>=1 ? 1.0 : progress-std::floor(progress);
    return std::min(frames-1,static_cast<std::uint32_t>(phase*frames));
}
void ParticleEmitterDefinition::validate() const {
    if(!texture||!texture->width||!texture->height||texture->pixels.size()!=std::uint64_t(texture->width)*texture->height*4)
        throw std::invalid_argument("Particle emitter requires a valid RGBA texture");
    if(blend!=BlendMode::Alpha&&blend!=BlendMode::Additive) throw std::invalid_argument("Particles require alpha or additive blending");
    if(space!=ParticleSpace::Local&&space!=ParticleSpace::World) throw std::invalid_argument("Invalid particle simulation space");
    if(!maxParticles||maxParticles>100000||!std::isfinite(spawnRate)||spawnRate<0||spawnRate>100000
        ||!std::isfinite(duration)||duration<0.001||duration>86400||!std::isfinite(lifetimeMin)
        ||!std::isfinite(lifetimeMax)||lifetimeMin<=0||lifetimeMax<lifetimeMin||lifetimeMax>86400)
        throw std::invalid_argument("Invalid particle capacity, rate, duration or lifetime");
    if(!finite(positionMin)||!finite(positionMax)||!ordered(positionMin,positionMax)
        ||!finite(velocityMin)||!finite(velocityMax)||!ordered(velocityMin,velocityMax)||!finite(gravity)
        ||!std::isfinite(rotationMin)||!std::isfinite(rotationMax)||rotationMin>rotationMax||!std::isfinite(angularVelocity))
        throw std::invalid_argument("Invalid particle position, velocity, gravity or rotation");
    if(bursts.size()>1024) throw std::invalid_argument("Too many particle bursts");
    double previous=-1;
    for(const auto& burst:bursts) {
        if(!std::isfinite(burst.time)||burst.time<0||burst.time>=duration||burst.time<previous||!burst.count||burst.count>maxParticles)
            throw std::invalid_argument("Particle bursts must be ordered within the emitter duration and capacity");
        previous=burst.time;
    }
    validateKeys(size);validateKeys(color);
    for(const auto& k:size) if(!std::isfinite(k.value)||k.value<0) throw std::invalid_argument("Invalid particle size curve");
    for(const auto& k:color) {
        const auto v=k.value;
        if(!std::isfinite(v.r)||!std::isfinite(v.g)||!std::isfinite(v.b)||!std::isfinite(v.a)||v.r<0||v.g<0||v.b<0||v.a<0||v.a>1)
            throw std::invalid_argument("Invalid particle color curve");
    }
    flipbook.validate(*texture);
}
float ParticleEmitterDefinition::sizeAt(float age) const {
    const auto [i,t]=segment(size,age);return i+1==size.size() ? size[i].value : size[i].value+(size[i+1].value-size[i].value)*t;
}
Color4 ParticleEmitterDefinition::colorAt(float age) const {
    const auto [i,t]=segment(color,age);const auto a=color[i].value;
    if(i+1==color.size()) return a;
    const auto b=color[i+1].value;return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};
}
ParticleEmitterComponent::ParticleEmitterComponent(std::string name,std::shared_ptr<const ParticleEmitterDefinition> definition)
    : Component(std::move(name)) { setTickEnabled(true);setDefinition(std::move(definition)); }
void ParticleEmitterComponent::setDefinition(std::shared_ptr<const ParticleEmitterDefinition> definition) {
    if(!definition) throw std::invalid_argument("Missing particle emitter definition");
    definition->validate();particles_.reserve(definition->maxParticles);deaths_.reserve(definition->maxParticles);
    definition_=std::move(definition);play();
}
void ParticleEmitterComponent::play(bool restart) {
    if(restart) {
        particles_.clear();time_=0;burstCycle_=0;burstIndex_=0;dropped_=0;random_=definition_->seed ? definition_->seed : 1;
        nextRate_=definition_->spawnRate>0 ? 1/definition_->spawnRate : std::numeric_limits<double>::infinity();
    }
    emitting_=true;paused_=false;
}
void ParticleEmitterComponent::stop(bool clear) { emitting_=false;paused_=false;if(clear) particles_.clear(); }
float ParticleEmitterComponent::randomUnit() {
    random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;
    return float(random_>>8)*(1.0f/16777216.0f);
}
void ParticleEmitterComponent::spawn(double event,double end) {
    // The CPU pool holds end-of-frame survivors. The heap tracks occupancy at
    // each birth time, including newborns that expire before this frame ends.
    while(!deaths_.empty()&&deaths_.front()<=event) { std::pop_heap(deaths_.begin(),deaths_.end(),std::greater<double>{});deaths_.pop_back(); }
    if(deaths_.size()==definition_->maxParticles) { addDropped(1);return; }
    auto range=[this](float a,float b) { return a+(b-a)*randomUnit(); };
    auto vec=[&](Vec3 a,Vec3 b) { return Vec3{range(a.x,b.x),range(a.y,b.y),range(a.z,b.z)}; };
    Particle p;p.position=vec(definition_->positionMin,definition_->positionMax);p.velocity=vec(definition_->velocityMin,definition_->velocityMax);
    p.lifetime=range(definition_->lifetimeMin,definition_->lifetimeMax);p.rotation=range(definition_->rotationMin,definition_->rotationMax);
    if(definition_->space==ParticleSpace::World) {
        const auto world=worldMatrix();p.position=transformPoint(world,p.position);p.velocity=transformDirection(world,p.velocity);p.sizeScale=scaleOf(world);
    }
    deaths_.push_back(event+p.lifetime);std::push_heap(deaths_.begin(),deaths_.end(),std::greater<double>{});
    if(end-event>=p.lifetime) return;
    const float age=static_cast<float>(end-event);
    p.position=p.position+p.velocity*age+definition_->gravity*(0.5f*age*age);
    p.velocity=p.velocity+definition_->gravity*age;p.age=age;p.rotation+=definition_->angularVelocity*age;
    particles_.push_back(p);
}
void ParticleEmitterComponent::advance(double seconds) {
    for(std::size_t i=0;i<particles_.size();) {
        auto& p=particles_[i];
        if(double(p.age)+seconds>=p.lifetime) { p=particles_.back();particles_.pop_back();continue; }
        const float dt=static_cast<float>(seconds);
        p.position=p.position+p.velocity*dt+definition_->gravity*(0.5f*dt*dt);
        p.velocity=p.velocity+definition_->gravity*dt;p.age+=dt;p.rotation+=definition_->angularVelocity*dt;++i;
    }
}
double ParticleEmitterComponent::nextBurst() const {
    if(definition_->bursts.empty()||burstIndex_>=definition_->bursts.size()) return std::numeric_limits<double>::infinity();
    return double(burstCycle_)*definition_->duration+definition_->bursts[burstIndex_].time;
}
void ParticleEmitterComponent::skipEvents(double end) {
    long double skipped=0;
    if(nextRate_<=end) skipped+=std::floor((end-nextRate_)*definition_->spawnRate+1e-7)+1;
    for(std::size_t i=0;i<definition_->bursts.size();++i) {
        const auto& burst=definition_->bursts[i];
        if(definition_->looping) {
            const auto first=burstCycle_+(i<burstIndex_ ? 1u : 0u);
            if(end>=burst.time) {
                const auto last=static_cast<std::uint64_t>(std::floor((end-burst.time)/definition_->duration));
                if(last>=first) skipped+=(static_cast<long double>(last-first)+1)*burst.count;
            }
        } else if(i>=burstIndex_&&burst.time<=end) skipped+=burst.count;
    }
    addDropped(skipped);
    if(definition_->spawnRate>0) nextRate_=(std::floor(end*definition_->spawnRate)+1)/definition_->spawnRate;
    if(definition_->looping) burstCycle_=static_cast<std::uint64_t>(std::floor(end/definition_->duration));
    const auto cycleTime=end-double(burstCycle_)*definition_->duration;
    burstIndex_=std::upper_bound(definition_->bursts.begin(),definition_->bursts.end(),cycleTime,
        [](double t,const ParticleBurst& b) { return t<b.time; })-definition_->bursts.begin();
    if(definition_->looping&&burstIndex_==definition_->bursts.size()) { ++burstCycle_;burstIndex_=0; }
}
void ParticleEmitterComponent::addDropped(long double count) {
    const auto room=std::numeric_limits<std::uint64_t>::max()-dropped_;
    dropped_+=static_cast<std::uint64_t>(std::min(static_cast<long double>(room),std::max(0.0L,count)));
}
void ParticleEmitterComponent::tick(double delta) {
    if(paused_) return;
    if(!std::isfinite(delta)||delta<0||time_+delta>1e12) throw std::invalid_argument("Invalid particle update delta");
    const auto end=time_+delta;
    if(!emitting_) { advance(delta);return; }
    if(delta==0&&std::min(nextRate_,nextBurst())>time_) return;
    const double emitEnd=definition_->looping ? end : std::min(end,definition_->duration);
    deaths_.clear();for(const auto& p:particles_) deaths_.push_back(time_+double(p.lifetime)-p.age);
    std::make_heap(deaths_.begin(),deaths_.end(),std::greater<double>{});
    advance(delta);std::uint64_t attempts=0;
    // Bounded work even after a huge delta/rate; overload drops emissions, never grows the pool.
    const auto budget=std::min<std::uint64_t>(65536,std::uint64_t(definition_->maxParticles)*4);
    while(attempts<budget) {
        const auto burstTime=nextBurst(),event=std::min(nextRate_,burstTime);
        if(event>emitEnd) break;
        if(burstTime<=nextRate_) {
            const auto count=definition_->bursts[burstIndex_].count;
            for(std::uint32_t i=0;i<count;++i) { if(attempts++<budget) spawn(event,end);else addDropped(1); }
            if(++burstIndex_==definition_->bursts.size()&&definition_->looping) { burstIndex_=0;++burstCycle_; }
        } else { spawn(event,end);++attempts;nextRate_+=1/definition_->spawnRate; }
    }
    if(attempts>=budget) skipEvents(emitEnd);
    time_=end;
    if(!definition_->looping&&end>=definition_->duration) emitting_=false;
}
void ParticleEmitterComponent::appendDraws(std::vector<ParticleDraw>& output,const Mat4& world) const {
    if(particles_.size()>output.max_size()-output.size()) throw std::length_error("Too many particle draws");
    const auto required=output.size()+particles_.size();
    if(required>output.capacity()) output.reserve(std::max(required,output.capacity()<=output.max_size()/2 ? output.capacity()*2 : output.max_size()));
    const bool local=definition_->space==ParticleSpace::Local;const auto scale=local ? scaleOf(world) : 1;
    for(const auto& p:particles_) {
        const auto age=std::clamp(p.age/p.lifetime,0.0f,1.0f);
        output.push_back({local ? transformPoint(world,p.position) : p.position,definition_->sizeAt(age)*p.sizeScale*scale,
            definition_->colorAt(age),age,p.rotation,definition_->flipbook,definition_->texture,definition_->blend,visibilityMask});
    }
}
}
