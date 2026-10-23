#include "engine/planet_physics.hpp"

namespace engine {
namespace {
double scalar(PlanetPosition a,PlanetPosition b){return a.x*b.x+a.y*b.y+a.z*b.z;}
PlanetPosition vector(PlanetPosition a,PlanetPosition b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
PlanetPosition planet(Vec3 v){return {v.x,v.y,v.z};}
bool validPosition(PlanetPosition p){const double n=p.length();return p.finite()&&std::isfinite(n)&&n>=1&&n<=1e14;}
bool validVelocity(PlanetPosition p){return p.finite()&&p.length()<=2e7;}
}
PlanetPosition planetGravity(PlanetPosition position,double radius,double acceleration){
    if(!validPosition(position)||!std::isfinite(radius)||radius<1||radius>1e10||!std::isfinite(acceleration)||acceleration<0||acceleration>1000)
        throw std::invalid_argument("Invalid planetary gravity");
    const double distance=position.length(),ratio=radius/std::max(radius,distance);
    return position.unit()*(-acceleration*ratio*ratio);
}
void PlanetCollisionSettings::validate() const {
    if(!std::isfinite(halfExtent)||halfExtent<32||halfExtent>1024||cells<16||cells>128||(cells&(cells-1)))
        throw std::invalid_argument("Invalid planet collision settings");
}
bool PlanetCollisionPatch::contains(PlanetPosition position,double margin) const {
    if(!position.finite()||!std::isfinite(margin)||margin<0||margin>=halfExtent)return false;
    const auto d=position-origin;
    return std::abs(scalar(d,east))<=halfExtent-margin&&std::abs(scalar(d,north))<=halfExtent-margin;
}
std::shared_ptr<const PlanetCollisionPatch> preparePlanetCollision(const PlanetTerrain& terrain,PlanetPosition focus,PlanetCollisionSettings settings){
    settings.validate();if(!validPosition(focus))throw std::invalid_argument("Invalid planet collision focus");
    auto patch=std::make_shared<PlanetCollisionPatch>();patch->up=focus.unit();patch->origin=terrain.surface(patch->up);
    patch->east=vector(std::abs(patch->up.y)<.95?PlanetPosition{0,1,0}:PlanetPosition{1,0,0},patch->up).unit();
    patch->north=vector(patch->up,patch->east);patch->halfExtent=settings.halfExtent;patch->cells=settings.cells;
    const auto n=settings.cells,stride=n+1;std::vector<Vec3> vertices;std::vector<std::uint32_t> indices;
    vertices.reserve(stride*stride);indices.reserve(n*n*6);
    for(unsigned y=0;y<=n;++y)for(unsigned x=0;x<=n;++x){
        const double u=(2.*x/n-1)*settings.halfExtent,v=(2.*y/n-1)*settings.halfExtent;
        const auto direction=patch->up*terrain.settings().radius+patch->east*u+patch->north*v;
        vertices.push_back(terrain.surface(direction).relativeTo(patch->origin));
    }
    for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){const auto a=y*stride+x,b=a+1,c=a+stride,d=c+1;
        indices.insert(indices.end(),{a,b,c,b,d,c});}
    patch->shape=CollisionShape::triangleMesh(vertices,indices);return patch;
}
void PlanetMotionSettings::validate() const {
    if(!std::isfinite(radius)||radius<.1f||radius>10||!std::isfinite(skin)||skin<.001f||skin>radius*.25f||
       !std::isfinite(snapDistance)||snapDistance<0||snapDistance>1||!std::isfinite(jumpSpeed)||jumpSpeed<0||jumpSpeed>50||
       !std::isfinite(maxSlopeAngle)||maxSlopeAngle<.1f||maxSlopeAngle>=pi/2||!std::isfinite(surfaceGravity)||surfaceGravity<0||surfaceGravity>1000||
       slideIterations<1||slideIterations>8)throw std::invalid_argument("Invalid planet motor settings");
}
PlanetSurfaceMotor::PlanetSurfaceMotor(PlanetMotionSettings settings):settings_(settings){settings_.validate();shape_=CollisionShape::sphere(settings_.radius);}
void PlanetSurfaceMotor::reset(PlanetPosition position,PlanetPosition velocity){
    if(!validPosition(position)||!validVelocity(velocity))throw std::invalid_argument("Invalid planet motor reset");
    PlanetMotionState next;next.position=position;next.velocity=velocity;state_=next;initialized_=true;
}
void PlanetSurfaceMotor::step(const PlanetTerrain& terrain,const PlanetMotionCommand& command,double seconds,const PhysicsWorld* world,const PlanetCollisionPatch* patch){
    if(!initialized_)throw std::logic_error("Reset planet motor before stepping");
    if(!std::isfinite(seconds)||seconds<=0||seconds>.05||!validVelocity(command.velocity)||bool(world)!=bool(patch)||
       (patch&&(!patch->shape||!patch->origin.finite())))throw std::invalid_argument("Invalid planet motion step");
    auto next=state_;next.landed=next.jumped=next.waitingForCollision=next.contactLimited=false;
    next.gravity=planetGravity(next.position,terrain.settings().radius,settings_.surfaceGravity);
    const auto up=next.position.unit();const bool wasGrounded=next.grounded;
    next.grounded=false;next.groundBody={};next.groundNormal={};
    if(command.poweredFlight)next.velocity=command.velocity;
    else {
        auto lateral=command.velocity-up*scalar(command.velocity,up);
        if(wasGrounded){const auto normal=planet(state_.groundNormal);lateral=lateral-normal*scalar(lateral,normal);
            // Static support/friction cancels downhill acceleration on walkable
            // ground. A small normal load refreshes contact without ground drift.
            next.velocity=lateral+normal*(std::min(0.,scalar(next.gravity,normal))*seconds);
        } else next.velocity=lateral+up*scalar(next.velocity,up)+next.gravity*seconds;
        if(wasGrounded&&command.jump){next.velocity=lateral+up*settings_.jumpSpeed+next.gravity*seconds;next.jumped=true;++next.jumps;}
    }
    const auto destination=next.position+next.velocity*seconds;
    const double altitude=std::min(next.position.length()-terrain.settings().radius-terrain.height(next.position),
        destination.length()-terrain.settings().radius-terrain.height(destination));
    if(altitude<1500&&(!patch||!patch->contains(next.position,settings_.radius+4)||!patch->contains(destination,settings_.radius+4))){
        // Retain the accepted position/velocity while the owner prepares collision.
        state_.landed=state_.jumped=false;state_.waitingForCollision=true;return;
    }
    if(!patch||altitude>1500){next.position=destination;next.grounded=false;++next.steps;state_=next;return;}
    PhysicsQueryFilter filter;filter.layers=settings_.collisionLayers;filter.includeSensors=false;
    auto pose=[&]{Transform p;p.position=next.position.relativeTo(patch->origin);return p;};
    const double slope=std::cos(settings_.maxSlopeAngle);
    auto support=[&](const PhysicsSweepHit& hit){if(dot(hit.normal,up.relativeTo({}))>=slope&&!next.jumped){
        next.grounded=true;next.groundNormal=hit.normal;next.groundBody=hit.body;}};
    // Recover bounded initial penetration against registered geometry, not a height clamp.
    for(unsigned iteration=0;iteration<3;++iteration){const auto hits=world->overlap(*shape_,pose(),filter);bool penetrating=false;
        for(const auto& hit:hits)if(hit.penetrationDepth>1e-4f){next.position=next.position+planet(hit.normal)*(hit.penetrationDepth+settings_.skin);penetrating=true;}
        if(!penetrating)break;if(iteration==2)next.contactLimited=true;}
    auto remaining=next.velocity*seconds;
    for(unsigned iteration=0;iteration<settings_.slideIterations;++iteration){const double length=remaining.length();if(length<1e-7)break;
        const auto direction=remaining*(1/length);++next.sweeps;
        const auto hit=world->sweep(*shape_,pose(),direction.relativeTo({}),float(length+settings_.skin),filter);
        if(!hit){next.position=next.position+remaining;remaining={};break;}
        const auto normal=planet(hit->normal);const double travel=std::clamp(double(hit->distance)-settings_.skin,0.,length);
        next.position=next.position+direction*travel;remaining=remaining-direction*travel;
        support(*hit);
        const double inward=scalar(remaining,normal);if(inward<0)remaining=remaining-normal*inward;
        const double into=scalar(next.velocity,normal);if(into<0)next.velocity=next.velocity-normal*into;
        if(!next.grounded&&scalar(normal,up)>0&&scalar(normal,up)<slope){
            const auto horizontal=normal-up*scalar(normal,up);const double n=scalar(horizontal,horizontal);
            if(n>1e-8){remaining=remaining-horizontal*(std::min(0.,scalar(remaining,horizontal))/n);
                next.velocity=next.velocity-horizontal*(std::min(0.,scalar(next.velocity,horizontal))/n);}}
        if(iteration+1==settings_.slideIterations&&remaining.length()>1e-4)next.contactLimited=true;
    }
    if(!next.jumped&&scalar(next.velocity,up)<=.2){
        ++next.sweeps;const float distance=settings_.skin+(wasGrounded?settings_.snapDistance:.06f);
        const auto hit=world->sweep(*shape_,pose(),(up*-1).relativeTo({}),distance,filter);
        if(hit&&dot(hit->normal,up.relativeTo({}))>=slope){
            next.position=next.position-up*std::max(0.,double(hit->distance)-settings_.skin);support(*hit);
            const auto normal=planet(hit->normal);const double into=scalar(next.velocity,normal);if(into<0)next.velocity=next.velocity-normal*into;
        }
    }
    next.landed=next.grounded&&!wasGrounded;if(next.landed)++next.landings;++next.steps;
    if(!validPosition(next.position)||!validVelocity(next.velocity))throw std::runtime_error("Planet motion produced invalid state");
    state_=next;
}
} // namespace engine
