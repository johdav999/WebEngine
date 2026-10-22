#include "engine/particle_emitter_component.hpp"
#include <limits>
#include <functional>
#include "engine/scene.hpp"
#include "engine/collider_component.hpp"
#include <numbers>

namespace engine {
namespace {
float length(Vec3 v) { return std::sqrt(dot(v,v)); }
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool ordered(Vec3 a,Vec3 b) { return a.x<=b.x&&a.y<=b.y&&a.z<=b.z; }
float scaleOf(const Mat4& world) {
    return std::max({std::sqrt(dot(transformDirection(world,{1,0,0}),transformDirection(world,{1,0,0}))),
        std::sqrt(dot(transformDirection(world,{0,1,0}),transformDirection(world,{0,1,0}))),
        std::sqrt(dot(transformDirection(world,{0,0,1}),transformDirection(world,{0,0,1})))});
}
Vec3 planeNormalAt(Vec3 normal,const Mat4& matrix) {
    try{return normalized(transformDirection(transpose(inverseAffine(matrix)),normal));}
    catch(const std::invalid_argument&){return {};}
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
Vec3 vectorAt(const std::vector<VectorKey>& keys,float t) {
    if(keys.empty()) return {};
    const auto [i,f]=segment(keys,t);
    return i+1==keys.size() ? keys[i].value : keys[i].value+(keys[i+1].value-keys[i].value)*f;
}
double triangleArea(const Mesh& mesh,std::size_t i) {
    const auto a=mesh.vertices.at(mesh.indices.at(i)).position;
    const auto b=mesh.vertices.at(mesh.indices.at(i+1)).position;
    const auto c=mesh.vertices.at(mesh.indices.at(i+2)).position;
    return double(length(cross(b-a,c-a)))*0.5;
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
    if(blend!=BlendMode::Alpha&&blend!=BlendMode::Additive&&!(renderMode==ParticleRenderMode::Mesh&&blend==BlendMode::Opaque)) throw std::invalid_argument("Particles require alpha or additive blending");
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
    if(shape<ParticleShape::Box||shape>ParticleShape::MeshSurface||alignment<ParticleAlignment::Camera||alignment>ParticleAlignment::Plane
        ||!std::isfinite(radius)||radius<0||!std::isfinite(coneAngle)||coneAngle<0||coneAngle>std::numbers::pi_v<float>*0.5f
        ||!std::isfinite(speedMin)||!std::isfinite(speedMax)||speedMin<0||speedMax<speedMin
        ||!std::isfinite(drag)||drag<0||drag>1000||!finite(wind)||!finite(turbulence)
        ||turbulence.x<0||turbulence.y<0||turbulence.z<0||!std::isfinite(turbulenceFrequency)||turbulenceFrequency<0||turbulenceFrequency>1000
        ||!std::isfinite(stretch)||stretch<0||!std::isfinite(softDistance)||softDistance<0)
        throw std::invalid_argument("Invalid particle shape, motion or rendering settings");
    for(const auto* curve:{&velocityOverLife,&forceOverLife}) {
        if(!curve->empty()) validateKeys(*curve);
        for(const auto& key:*curve) if(!finite(key.value)) throw std::invalid_argument("Invalid particle vector curve");
    }
    if(shape==ParticleShape::MeshSurface) {
        if(!emissionMesh||emissionMesh->indices.empty()||emissionMesh->indices.size()%3||emissionMesh->indices.size()>3000000)
            throw std::invalid_argument("Particle surface requires bounded triangle mesh");
        for(const auto index:emissionMesh->indices)
            if(index>=emissionMesh->vertices.size()||!finite(emissionMesh->vertices[index].position))
                throw std::invalid_argument("Invalid particle surface vertex");
        double area=0;for(std::size_t i=0;i<emissionMesh->indices.size();i+=3) area+=triangleArea(*emissionMesh,i);
        if(!std::isfinite(area)||area<=0) throw std::invalid_argument("Particle surface has no finite area");
    }
    if(!std::isfinite(startDelay)||startDelay<0||startDelay>86400) throw std::invalid_argument("Invalid particle start delay");
    if(renderMode<ParticleRenderMode::Billboard||renderMode>ParticleRenderMode::SurfaceTrail||trailMaxPoints<2||trailMaxPoints>4096
        ||!std::isfinite(trailLifetime)||trailLifetime<=0||trailLifetime>86400
        ||!std::isfinite(trailMinDistance)||trailMinDistance<.0001f
        ||!std::isfinite(trailBreakDistance)||trailBreakDistance<trailMinDistance
        ||!std::isfinite(trailTextureLength)||trailTextureLength<.0001f)
        throw std::invalid_argument("Invalid ribbon trail settings");
    if(!finite(planeNormal)||!std::isfinite(dot(planeNormal,planeNormal))||length(planeNormal)<1e-6f||!std::isfinite(surfaceOffset))
        throw std::invalid_argument("Invalid particle plane normal or offset");
    if(!finite(meshRotationMin)||!finite(meshRotationMax)||!ordered(meshRotationMin,meshRotationMax)||!finite(meshAngularVelocity))
        throw std::invalid_argument("Invalid mesh particle rotation");
    if(renderMode==ParticleRenderMode::Mesh){
        if(!particleMesh||particleMesh->vertices.empty()||particleMesh->vertices.size()>1000000||particleMesh->indices.empty()
            ||particleMesh->indices.size()>3000000||particleMesh->indices.size()%3||particleMesh->skeleton||!particleMesh->skin.empty()||!particleMesh->morphTargets.empty()
            ||std::uint64_t(maxParticles)*std::max(std::size_t(1),particleMesh->parts.size())>1000000)
            throw std::invalid_argument("Mesh particles require bounded static triangle geometry");
        for(const auto& v:particleMesh->vertices)if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.uv.x)||!std::isfinite(v.uv.y))throw std::invalid_argument("Invalid particle mesh vertex");
        for(auto index:particleMesh->indices)if(index>=particleMesh->vertices.size())throw std::invalid_argument("Invalid particle mesh index");
        for(const auto& part:particleMesh->parts)if(part.materialIndex>=particleMesh->materials.size()||part.firstIndex>particleMesh->indices.size()||!part.indexCount||part.indexCount>particleMesh->indices.size()-part.firstIndex||part.indexCount%3)throw std::invalid_argument("Invalid particle mesh material range");
        for(const auto& m:particleMesh->materials)if(m){m->validate();if(m->shader==MaterialShader::Water)throw std::invalid_argument("Mesh particles do not support water materials");}
        if(softDistance>0)throw std::invalid_argument("Mesh particles use mesh depth testing; soft fade is not supported");
    }
    collision.validate();
    if(renderMode==ParticleRenderMode::SurfaceTrail&&(space!=ParticleSpace::World||std::abs(surfaceOffset)>1))throw std::invalid_argument("Surface trails require World space and offset within one metre");
    if(collision.enabled&&(renderMode==ParticleRenderMode::Ribbon||renderMode==ParticleRenderMode::SurfaceTrail))throw std::invalid_argument("Ribbon sampling does not support particle collision");
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
    std::vector<double> areas;
    if(definition->shape==ParticleShape::MeshSurface) {
        const auto& mesh=*definition->emissionMesh;areas.reserve(mesh.indices.size()/3);
        double sum=0;for(std::size_t i=0;i<mesh.indices.size();i+=3) {sum+=triangleArea(mesh,i);areas.push_back(sum);}
    }
    std::vector<TrailPoint> trail;if(definition->renderMode==ParticleRenderMode::Ribbon||definition->renderMode==ParticleRenderMode::SurfaceTrail)trail.resize(definition->trailMaxPoints);
    auto collisionShape=definition->collision.enabled?CollisionShape::sphere(definition->collision.radius):nullptr;
    if(definition->collision.enabled)impacts_.reserve(definition->collision.maxImpacts);
    collisionShape_=std::move(collisionShape);
    trail_=std::move(trail);surfaceAreas_=std::move(areas);definition_=std::move(definition);play();
}
void ParticleEmitterComponent::play(bool restart) {
    if(restart) {
        events_.clear();droppedEvents_=0;
        impacts_.clear();collisionQueries_=0;skippedCollisionSteps_=droppedImpacts_=0;
        particles_.clear();trailHead_=trailCount_=0;trailDistance_=0;breakTrail();time_=0;burstCycle_=0;burstIndex_=0;dropped_=0;random_=definition_->seed ? definition_->seed : 1;
        nextRate_=definition_->spawnRate>0 ? definition_->startDelay+1/definition_->spawnRate : std::numeric_limits<double>::infinity();
    }
    emitting_=true;paused_=false;
}
void ParticleEmitterComponent::stop(bool clear) { if(!clear)finishTrailHead(time_);emitting_=false;paused_=false;breakTrail();if(clear) {impacts_.clear();particles_.clear();trailHead_=trailCount_=0;} }
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
    const auto& d=*definition_;
    Vec3 direction{0,1,0},offset{};
    if(d.shape==ParticleShape::Sphere) {
        const float y=range(-1,1),angle=range(0,2*std::numbers::pi_v<float>),r=std::sqrt(std::max(0.0f,1-y*y));
        direction={r*std::cos(angle),y,r*std::sin(angle)};
        offset=direction*(d.radius*(d.surfaceOnly ? 1.0f : std::cbrt(randomUnit())));
    } else if(d.shape==ParticleShape::Disc||d.shape==ParticleShape::Cone) {
        const float angle=range(0,2*std::numbers::pi_v<float>),r=d.radius*std::sqrt(randomUnit());
        offset={r*std::cos(angle),0,r*std::sin(angle)};
        if(d.shape==ParticleShape::Cone) {
            const float y=range(std::cos(d.coneAngle),1),a=range(0,2*std::numbers::pi_v<float>),x=std::sqrt(std::max(0.0f,1-y*y));
            direction={x*std::cos(a),y,x*std::sin(a)};
        }
    } else if(d.shape==ParticleShape::MeshSurface) {
        const auto& mesh=*d.emissionMesh;
        const auto chosen=std::upper_bound(surfaceAreas_.begin(),surfaceAreas_.end(),double(randomUnit())*surfaceAreas_.back());
        const auto i=std::min(std::size_t(chosen-surfaceAreas_.begin()),surfaceAreas_.size()-1)*3;
        const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
        const float u=std::sqrt(randomUnit()),v=randomUnit();offset=a*(1-u)+b*(u*(1-v))+c*(u*v);
        direction=normalized(cross(b-a,c-a));
    }
    p.position=p.position+offset;
    if(d.speedMax>0) p.velocity=p.velocity+direction*range(d.speedMin,d.speedMax);
    if(length(d.turbulence)>0) p.turbulencePhase=range(0,2*std::numbers::pi_v<float>);
    if(d.renderMode==ParticleRenderMode::Mesh)p.meshRotation=vec(d.meshRotationMin,d.meshRotationMax);
    p.planeNormal=normalized(definition_->planeNormal);
    if(definition_->space==ParticleSpace::World) {
        const auto world=worldMatrix();
        if(d.renderMode==ParticleRenderMode::Billboard&&d.alignment==ParticleAlignment::Plane){p.planeNormal=planeNormalAt(p.planeNormal,world);if(dot(p.planeNormal,p.planeNormal)<.5f){addDropped(1);return;}}p.position=transformPoint(world,p.position);p.velocity=transformDirection(world,p.velocity);p.sizeScale=scaleOf(world);
        if(d.renderMode==ParticleRenderMode::Mesh){p.meshBasis=world;p.meshBasis.m[12]=p.meshBasis.m[13]=p.meshBasis.m[14]=0;}
    }
    if(d.space==ParticleSpace::World)p.velocity=p.velocity+inheritedVelocity_;
    else if(dot(inheritedVelocity_,inheritedVelocity_)>0){try{p.velocity=p.velocity+transformDirection(inverseAffine(worldMatrix()),inheritedVelocity_);}catch(const std::invalid_argument&){addDropped(1);return;}}
    this->event(ParticleEventType::Birth,p);
    deaths_.push_back(event+p.lifetime);std::push_heap(deaths_.begin(),deaths_.end(),std::greater<double>{});
    if(end-event>=p.lifetime&&!d.collision.enabled&&!(eventMask_&4)) return;
    const float age=static_cast<float>(std::min(end-event,double(p.lifetime)));
    integrate(p,age);
    if(p.age<p.lifetime)particles_.push_back(p);else this->event(ParticleEventType::Death,p);
}
void ParticleEmitterComponent::integrate(Particle& p,double seconds) {
    const auto& d=*definition_;
    const bool varying=!d.forceOverLife.empty()||!d.velocityOverLife.empty()||length(d.turbulence)>0;
    const auto steps=(varying||d.collision.enabled) ? std::max(1,std::min(128,int(std::ceil(std::min(seconds*60,128.0))))) : 1;
    const float dt=static_cast<float>(seconds/steps);
    for(int i=0;i<steps;++i) {
        if(p.age>=p.lifetime)break;
        if(p.settled){p.age+=dt;continue;}
        const Vec3 previousPosition=p.position,previousVelocity=p.velocity;
        const float mid=p.age+dt*0.5f,t=mid/p.lifetime;
        const float phase=mid*d.turbulenceFrequency*2*std::numbers::pi_v<float>+p.turbulencePhase;
        const Vec3 gust{d.turbulence.x*std::sin(phase),d.turbulence.y*std::sin(phase*1.37f+1),d.turbulence.z*std::sin(phase*0.73f+2)};
        const Vec3 acceleration=d.gravity+vectorAt(d.forceOverLife,t)+gust;
        const auto added=vectorAt(d.velocityOverLife,t);
        if(d.drag>0) {
            // Exact constant-acceleration linear drag for this midpoint interval.
            const float x=d.drag*dt;
            const float f=x<.001f ? dt*(1-x*.5f+x*x/6) : -std::expm1(-x)/d.drag;
            const float g=x<.001f ? dt*dt*(.5f-x/6+x*x/24) : (dt-f)/d.drag;
            const Vec3 effective=acceleration+(d.wind-p.velocity)*d.drag;
            p.position=p.position+p.velocity*dt+effective*g+added*dt;
            p.velocity=p.velocity+effective*f;
        } else {
            p.position=p.position+p.velocity*dt+acceleration*(0.5f*dt*dt)+added*dt;
            p.velocity=p.velocity+acceleration*dt;
        }
        if(d.collision.enabled&&dt>0)collide(p,previousPosition,previousVelocity,added,dt);
        p.age+=dt;p.rotation+=d.angularVelocity*dt;
        if(d.renderMode==ParticleRenderMode::Mesh)p.meshRotation=p.meshRotation+d.meshAngularVelocity*dt;
    }
}
void ParticleEmitterComponent::advance(double seconds) {
    for(std::size_t i=0;i<particles_.size();) {
        auto& p=particles_[i];
        if(double(p.age)+seconds>=p.lifetime&&!definition_->collision.enabled&&!(eventMask_&4)) { p=particles_.back();particles_.pop_back();continue; }
        integrate(p,std::min(seconds,double(p.lifetime-p.age)));
        if(p.age>=p.lifetime){event(ParticleEventType::Death,p);p=particles_.back();particles_.pop_back();continue;}++i;
    }
}

double ParticleEmitterComponent::nextBurst() const {
    if(definition_->bursts.empty()||burstIndex_>=definition_->bursts.size()) return std::numeric_limits<double>::infinity();
    return definition_->startDelay+double(burstCycle_)*definition_->duration+definition_->bursts[burstIndex_].time;
}
void ParticleEmitterComponent::skipEvents(double end) {
    const double localEnd=end-definition_->startDelay;
    long double skipped=0;
    if(nextRate_<=end) skipped+=std::floor((end-nextRate_)*definition_->spawnRate+1e-7)+1;
    for(std::size_t i=0;i<definition_->bursts.size();++i) {
        const auto& burst=definition_->bursts[i];
        if(definition_->looping) {
            const auto first=burstCycle_+(i<burstIndex_ ? 1u : 0u);
            if(localEnd>=burst.time) {
                const auto last=static_cast<std::uint64_t>(std::floor((localEnd-burst.time)/definition_->duration));
                if(last>=first) skipped+=(static_cast<long double>(last-first)+1)*burst.count;
            }
        } else if(i>=burstIndex_&&burst.time<=localEnd) skipped+=burst.count;
    }
    addDropped(skipped);
    if(definition_->spawnRate>0) nextRate_=definition_->startDelay+(std::floor(localEnd*definition_->spawnRate)+1)/definition_->spawnRate;
    if(definition_->looping) burstCycle_=static_cast<std::uint64_t>(std::floor(std::max(0.0,localEnd)/definition_->duration));
    const auto cycleTime=localEnd-double(burstCycle_)*definition_->duration;
    burstIndex_=std::upper_bound(definition_->bursts.begin(),definition_->bursts.end(),cycleTime,
        [](double t,const ParticleBurst& b) { return t<b.time; })-definition_->bursts.begin();
    if(definition_->looping&&burstIndex_==definition_->bursts.size()) { ++burstCycle_;burstIndex_=0; }
}
void ParticleEmitterComponent::addDropped(long double count) {
    const auto room=std::numeric_limits<std::uint64_t>::max()-dropped_;
    dropped_+=static_cast<std::uint64_t>(std::min(static_cast<long double>(room),std::max(0.0L,count)));
}
void ParticleEmitterComponent::tick(double delta) {
    events_.clear();
    if(paused_) return;
    if(!std::isfinite(delta)||delta<0||time_+delta>1e12) throw std::invalid_argument("Invalid particle update delta");
    if(definition_->renderMode==ParticleRenderMode::Ribbon||definition_->renderMode==ParticleRenderMode::SurfaceTrail){tickTrail(delta);return;}
    collisionQueries_=0;
    if(definition_->collision.enabled&&delta>0&&particles_.size()>1)std::rotate(particles_.begin(),particles_.begin()+1,particles_.end());
    const auto end=time_+delta;
    if(!emitting_) { advance(delta);return; }
    if(delta==0&&std::min(nextRate_,nextBurst())>time_) return;
    const double emitEnd=definition_->looping ? end : std::min(end,definition_->startDelay+definition_->duration);
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
    if(!definition_->looping&&end>=definition_->startDelay+definition_->duration) emitting_=false;
}
void ParticleEmitterComponent::appendDraws(std::vector<ParticleDraw>& output,const Mat4& world) const {
    if(definition_->renderMode!=ParticleRenderMode::Billboard)return;
    if(particles_.size()>output.max_size()-output.size()) throw std::length_error("Too many particle draws");
    const auto required=output.size()+particles_.size();
    if(required>output.capacity()) output.reserve(std::max(required,output.capacity()<=output.max_size()/2 ? output.capacity()*2 : output.max_size()));
    const bool local=definition_->space==ParticleSpace::Local;const auto scale=local ? scaleOf(world) : 1;
    for(const auto& p:particles_) {
        const auto age=std::clamp(p.age/p.lifetime,0.0f,1.0f);
        const auto normal=definition_->alignment==ParticleAlignment::Plane&&local?planeNormalAt(p.planeNormal,world):p.planeNormal;
        if(definition_->alignment==ParticleAlignment::Plane&&dot(normal,normal)<.5f)continue;
        output.push_back({local ? transformPoint(world,p.position) : p.position,definition_->sizeAt(age)*p.sizeScale*scale,
            definition_->colorAt(age),age,p.rotation,definition_->flipbook,definition_->texture,definition_->blend,visibilityMask});
        auto& draw=output.back();const auto velocity=p.velocity+vectorAt(definition_->velocityOverLife,age);
        draw.velocity=local ? transformDirection(world,velocity) : velocity;
        draw.planeNormal=normal;
        if(definition_->alignment==ParticleAlignment::Plane)draw.position=draw.position+normal*definition_->surfaceOffset;
        draw.alignment=definition_->alignment;draw.stretch=definition_->stretch;draw.softDistance=definition_->softDistance;
    }
}

bool softParticlesRequired(const Scene& scene,std::uint32_t visibilityMask) {
    bool required=false;
    auto visit=[&](auto&& self,const Node& node)->void {
        if(!node.enabled||node.isPendingRemoval()) return;
        if(const auto* emitter=dynamic_cast<const ParticleEmitterComponent*>(&node))
            required|=(emitter->visibilityMask&visibilityMask)&&emitter->definition().renderMode!=ParticleRenderMode::Mesh&&emitter->definition().softDistance>0;
        for(const auto& child:node.children()) self(self,*child);
    };
    visit(visit,scene);return required;
}
}
