#include "engine/cloth.hpp"
#include "cloth_collision.hpp"
#include <set>
#include <utility>
#include <array>
#include <map>

namespace engine {
namespace {
constexpr double step=1.0/120.0;
float length(Vec3 v){return std::sqrt(dot(v,v));}
void vectorValid(Vec3 v,float limit=1000000) {
    for(float f:{v.x,v.y,v.z})if(!std::isfinite(f)||std::abs(f)>limit)throw std::invalid_argument("Invalid cloth vector");
}
void targetsValid(std::span<const Vec3> points,std::size_t size) {
    if(points.size()!=size)throw std::invalid_argument("Cloth target size mismatch");
    for(auto p:points)vectorValid(p);
}
std::vector<std::array<std::uint32_t,2>> surfaceEdges(const ClothDefinition& definition) {
    std::set<std::array<std::uint32_t,2>> edges;
    for(auto face:definition.surfaceTriangles)for(unsigned i=0;i<3;++i) {
        auto a=face[i],b=face[(i+1)%3];edges.insert({std::min(a,b),std::max(a,b)});
    }
    return {edges.begin(),edges.end()};
}
}
void ClothDefinition::validate(std::span<const Vec3> rest) const {
    if(rest.empty()||rest.size()>1024||maxDistance.size()!=rest.size()||links.size()>8192||!iterations||iterations>16)
        throw std::invalid_argument("Invalid cloth dimensions or iteration budget");
    for(auto p:rest)vectorValid(p);
    bool pinned=false;
    for(auto d:maxDistance) {if(!std::isfinite(d)||d<0||d>100)throw std::invalid_argument("Invalid cloth tether");pinned|=d==0;}
    if(!pinned)throw std::invalid_argument("Cloth requires at least one pinned vertex");
    if(!inverseMass.empty()&&inverseMass.size()!=rest.size())throw std::invalid_argument("Cloth inverse mass size mismatch");
    for(float w:inverseMass)if(!std::isfinite(w)||w<.001f||w>1000)throw std::invalid_argument("Invalid cloth inverse mass");
    if(!std::isfinite(aerodynamicDrag)||aerodynamicDrag<0||aerodynamicDrag>4||(aerodynamicDrag>0&&surfaceTriangles.empty()))
        throw std::invalid_argument("Invalid cloth aerodynamic drag or missing topology");
    if(surfaceTriangles.size()>2048||(!surfaceTriangles.empty()&&selfCollisionDistance<=0))
        throw std::invalid_argument("Invalid cloth surface budget or separation");
    std::set<std::array<std::uint32_t,3>> faces;
    for(auto face:surfaceTriangles) {
        for(auto index:face)if(index>=rest.size())throw std::invalid_argument("Invalid cloth surface index");
        if(!cloth_detail::triangleValid({rest[face[0]],rest[face[1]],rest[face[2]]}))throw std::invalid_argument("Degenerate cloth surface");
        std::sort(face.begin(),face.end());if(!faces.insert(face).second)throw std::invalid_argument("Duplicate cloth surface");
    }
    if(edgeSelfCollision&&(surfaceTriangles.empty()||surfaceEdges(*this).size()>2048))
        throw std::invalid_argument("Edge self collision requires a surface with at most 2048 unique edges");
    if(!std::isfinite(selfCollisionDistance)||selfCollisionDistance<0||selfCollisionDistance>1
       ||(selfCollisionDistance>0&&selfCollisionDistance<.001f))throw std::invalid_argument("Invalid cloth self collision distance");
    if(!std::isfinite(damping)||damping<0||damping>100||!std::isfinite(thickness)||thickness<0||thickness>1
       ||!std::isfinite(friction)||friction<0||friction>1||!std::isfinite(teleportDistance)||teleportDistance<=0||teleportDistance>100)
        throw std::invalid_argument("Invalid cloth settings");
    std::set<std::pair<std::uint32_t,std::uint32_t>> pairs;
    for(auto l:links) {
        if(l.a>=rest.size()||l.b>=rest.size()||l.a==l.b||!std::isfinite(l.compliance)||l.compliance<0||l.compliance>1
           ||!pairs.insert(std::minmax(l.a,l.b)).second)throw std::invalid_argument("Invalid/duplicate cloth link");
        if(length(rest[l.b]-rest[l.a])<1e-6f)throw std::invalid_argument("Degenerate cloth link");
    }
}
ClothSimulation::ClothSimulation(ClothDefinition definition,std::span<const Vec3> rest):definition_(std::move(definition)) {
    definition_.validate(rest);
    if(definition_.edgeSelfCollision)edges_=surfaceEdges(definition_);
    for(auto l:definition_.links)lengths_.push_back(length(rest[l.b]-rest[l.a]));
    positions_.assign(rest.begin(),rest.end());targets_=positions_;velocities_.resize(rest.size());
    if(definition_.selfCollisionDistance>0) {
        rest_=positions_;const auto n=rest.size();selfExcluded_.resize(n*n);
        for(std::size_t a=0;a<n;++a)for(std::size_t b=a+1;b<n;++b)
            if(length(rest[a]-rest[b])<=definition_.selfCollisionDistance*1.01f)
                selfExcluded_[a*n+b]=selfExcluded_[b*n+a]=true;
        for(auto l:definition_.links)selfExcluded_[l.a*n+l.b]=selfExcluded_[l.b*n+l.a]=true;
        for(auto face:definition_.surfaceTriangles)for(auto a:face)for(auto b:face)selfExcluded_[a*n+b]=true;
    }
}
void ClothSimulation::reset(std::span<const Vec3> targets) {
    targetsValid(targets,positions_.size());
    std::copy(targets.begin(),targets.end(),positions_.begin());std::copy(targets.begin(),targets.end(),targets_.begin());
    std::fill(velocities_.begin(),velocities_.end(),Vec3{});remainder_=0;++stats_.resets;
}
void ClothSimulation::advance(double delta,std::span<const Vec3> targets,std::span<const ClothCapsule> capsules,Vec3 acceleration,ClothEnvironment environment) {
    if(!std::isfinite(delta)||delta<0||delta>.25||capsules.size()>32)throw std::invalid_argument("Invalid cloth time/collider budget");
    targetsValid(targets,positions_.size());vectorValid(acceleration,10000);
    vectorValid(environment.wind.velocity,100);
    if(!std::isfinite(environment.wind.density)||environment.wind.density<0||environment.wind.density>10)
        throw std::invalid_argument("Invalid cloth air density");
    for(auto c:capsules){vectorValid(c.a);vectorValid(c.b);if(!std::isfinite(c.radius)||c.radius<=0||c.radius>100)throw std::invalid_argument("Invalid cloth capsule");}
    if(environment.planes.size()>16||environment.boxes.size()>32||environment.triangles.size()>256)throw std::invalid_argument("Invalid cloth environment budget");
    for(auto t:environment.triangles){vectorValid(t.a);vectorValid(t.b);vectorValid(t.c);
        if(!cloth_detail::triangleValid(t))throw std::invalid_argument("Degenerate cloth environment triangle");}
    for(auto p:environment.planes) {
        vectorValid(p.normal,1.001f);
        if(std::abs(dot(p.normal,p.normal)-1)>1e-4f||!std::isfinite(p.offset)||std::abs(p.offset)>1e6f)
            throw std::invalid_argument("Invalid cloth plane");
    }
    for(auto b:environment.boxes) {
        vectorValid(b.minimum);vectorValid(b.maximum);
        auto extent=b.maximum-b.minimum;
        if(extent.x<1e-5f||extent.y<1e-5f||extent.z<1e-5f)throw std::invalid_argument("Invalid cloth box");
    }
    auto motionValid=[](std::span<const Vec3> motion,std::size_t count){
        if(!motion.empty()&&motion.size()!=count)throw std::invalid_argument("Cloth collider motion size mismatch");
        for(auto v:motion)vectorValid(v,100);
    };
    motionValid(environment.capsuleTranslations,capsules.size());
    motionValid(environment.triangleTranslations,environment.triangles.size());
    for(std::size_t i=0;i<environment.capsuleTranslations.size();++i) {
        auto move=environment.capsuleTranslations[i];vectorValid(capsules[i].a-move);vectorValid(capsules[i].b-move);
    }
    for(std::size_t i=0;i<environment.triangleTranslations.size();++i) {
        auto move=environment.triangleTranslations[i];auto t=environment.triangles[i];
        vectorValid(t.a-move);vectorValid(t.b-move);vectorValid(t.c-move);
    }
    if(delta==0)return;
    auto candidate=*this;candidate.advanceValid(delta,targets,capsules,acceleration,environment);*this=std::move(candidate);
}
void ClothSimulation::separateSelf() {
    const float distance=definition_.selfCollisionDistance;if(distance==0)return;
    using Cell=std::array<std::int64_t,3>;
    auto cell=[&](Vec3 p)->Cell{return {std::int64_t(std::floor(double(p.x)/distance)),std::int64_t(std::floor(double(p.y)/distance)),std::int64_t(std::floor(double(p.z)/distance))};};
    std::map<Cell,std::vector<std::size_t>> grid;
    for(std::size_t i=0;i<positions_.size();++i)grid[cell(positions_[i])].push_back(i);
    // Stable cell/index order. Rebuild each iteration; corrections can create new
    // contacts, which are discovered on the next iteration rather than dropped.
    for(std::size_t i=0;i<positions_.size();++i) {
        auto c=cell(positions_[i]);
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
            auto found=grid.find({c[0]+x,c[1]+y,c[2]+z});if(found==grid.end())continue;
            for(auto j:found->second) {
                if(j<=i||selfExcluded_[i*positions_.size()+j])continue;
                const float wi=weight(i),wj=weight(j);
                if(wi+wj==0)continue;
                auto d=positions_[j]-positions_[i];float len=length(d);if(len>=distance)continue;
                auto n=len>1e-7f?d*(1/len):normalized(rest_[j]-rest_[i]);
                auto correction=n*((distance-len)/(wi+wj));
                positions_[i]=positions_[i]-correction*wi;positions_[j]=positions_[j]+correction*wj;
                ++stats_.selfContacts;
            }
        }
    }
}
void ClothSimulation::separateSurface(std::span<const Vec3> previous) {
    const auto count=positions_.size();
    for(auto face:definition_.surfaceTriangles)for(std::size_t i=0;i<count;++i) {
        if(i==face[0]||i==face[1]||i==face[2])continue;
        if(selfExcluded_[i*count+face[0]]||selfExcluded_[i*count+face[1]]||selfExcluded_[i*count+face[2]])continue;
        if(weight(i)+weight(face[0])+weight(face[1])+weight(face[2])==0)continue;
        ClothTriangle before{previous[face[0]],previous[face[1]],previous[face[2]]};
        ClothTriangle now{positions_[face[0]],positions_[face[1]],positions_[face[2]]};
        auto hit=cloth_detail::surface(previous[i],positions_[i],before,now,definition_.selfCollisionDistance,definition_.sweptCollision);
        if(!hit)continue;
        auto bary=hit->closest.weights;Vec3 point{};float denominator=weight(i);
        for(unsigned j=0;j<3;++j){point=point+positions_[face[j]]*bary[j];denominator+=weight(face[j])*bary[j]*bary[j];}
        float depth=hit->separation-dot(positions_[i]-point,hit->normal);
        if(depth<=0||denominator<1e-8f)continue;
        auto correction=hit->normal*(depth/denominator);
        positions_[i]=positions_[i]+correction*weight(i);
        for(unsigned j=0;j<3;++j)positions_[face[j]]=positions_[face[j]]-correction*(weight(face[j])*bary[j]);
        ++stats_.selfContacts;++stats_.surfaceContacts;if(definition_.sweptCollision&&hit->time<1)++stats_.sweptContacts;
    }
}
void ClothSimulation::separateEdges(std::span<const Vec3> previous) {
    if(edges_.empty())return;
    struct Bounds {Vec3 lo,hi;};
    std::vector<Bounds> bounds;bounds.reserve(edges_.size());
    const float radius=definition_.selfCollisionDistance;
    for(auto edge:edges_) {
        auto a=positions_[edge[0]],b=positions_[edge[1]];
        auto oldA=definition_.sweptCollision?previous[edge[0]]:a,oldB=definition_.sweptCollision?previous[edge[1]]:b;
        bounds.push_back({{std::min({a.x,b.x,oldA.x,oldB.x})-radius,std::min({a.y,b.y,oldA.y,oldB.y})-radius,std::min({a.z,b.z,oldA.z,oldB.z})-radius},
                          {std::max({a.x,b.x,oldA.x,oldB.x})+radius,std::max({a.y,b.y,oldA.y,oldB.y})+radius,std::max({a.z,b.z,oldA.z,oldB.z})+radius}});
    }
    std::vector<std::size_t> order(edges_.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return bounds[a].lo.x==bounds[b].lo.x?a<b:bounds[a].lo.x<bounds[b].lo.x;});
    // Sweep-and-prune bounds rebuild each iteration. Contacts created by an earlier
    // correction outside these bounds are discovered on the following iteration.
    for(std::size_t a=0;a<order.size();++a)for(std::size_t b=a+1;b<order.size();++b) {
        const auto ai=order[a],bi=order[b];const auto& ab=bounds[ai];const auto& bb=bounds[bi];
        if(bb.lo.x>ab.hi.x)break;
        if(bb.lo.y>ab.hi.y||ab.lo.y>bb.hi.y||bb.lo.z>ab.hi.z||ab.lo.z>bb.hi.z)continue;
        auto first=edges_[ai],second=edges_[bi];bool excluded=false;
        for(auto i:first)for(auto j:second)excluded|=i==j||selfExcluded_[i*positions_.size()+j];
        if(excluded)continue;
        std::array<std::uint32_t,4> indices{first[0],first[1],second[0],second[1]};
        std::array<float,4> mass{};float total=0;
        for(unsigned i=0;i<4;++i){mass[i]=weight(indices[i]);total+=mass[i];}
        if(total==0)continue;
        auto hit=cloth_detail::edgeSweep({previous[first[0]],previous[first[1]]},{positions_[first[0]],positions_[first[1]]},
            {previous[second[0]],previous[second[1]]},{positions_[second[0]],positions_[second[1]]},radius,definition_.sweptCollision);
        if(!hit)continue;
        const float s=hit->closest.s,t=hit->closest.t;
        const std::array<float,4> coefficients{1-s,s,-(1-t),-t};Vec3 difference{};float denominator=0;
        for(unsigned i=0;i<4;++i){difference=difference+positions_[indices[i]]*coefficients[i];denominator+=mass[i]*coefficients[i]*coefficients[i];}
        float depth=hit->separation-dot(difference,hit->closest.normal);
        if(depth<=0||denominator<1e-8f)continue;
        auto correction=hit->closest.normal*(depth/denominator);
        for(unsigned i=0;i<4;++i)positions_[indices[i]]=positions_[indices[i]]+correction*(mass[i]*coefficients[i]);
        ++stats_.selfContacts;++stats_.edgeContacts;if(definition_.sweptCollision&&hit->time<1)++stats_.sweptContacts;
    }
}
void ClothSimulation::advanceValid(double delta,std::span<const Vec3> targets,std::span<const ClothCapsule> capsules,Vec3 acceleration,ClothEnvironment environment) {
    for(std::size_t i=0;i<targets.size();++i)if(length(targets[i]-targets_[i])>definition_.teleportDistance){reset(targets);return;}
    const double available=remainder_+delta;
    const auto complete=static_cast<unsigned>(std::floor((available+1e-10)/step));
    const auto count=std::min(complete,8u);
    remainder_=std::max(0.0,available-complete*step);stats_.droppedSeconds+=(complete-count)*step;
    if(!count)return; // Keep the previous endpoint until an actual simulation step.
    const auto previousTargets=targets_;
    std::vector<float> lambda(lengths_.size());
    std::vector<Vec3> contact(positions_.size());
    std::vector<Vec3> contactVelocity(positions_.size());
    std::vector<Vec3> aerodynamic(positions_.size());
    for(unsigned sub=0;sub<count;++sub) {
        const auto old=positions_;std::fill(lambda.begin(),lambda.end(),0);std::fill(contact.begin(),contact.end(),Vec3{});
        std::fill(contactVelocity.begin(),contactVelocity.end(),Vec3{});
        std::fill(aerodynamic.begin(),aerodynamic.end(),Vec3{});
        if(definition_.aerodynamicDrag>0&&environment.wind.density>0)for(auto face:definition_.surfaceTriangles) {
            auto areaVector=cross(old[face[1]]-old[face[0]],old[face[2]]-old[face[0]]);
            const float twiceArea=length(areaVector);if(twiceArea<1e-6f)continue;
            auto normal=areaVector*(1/twiceArea);
            auto sampleVelocity=[&](std::size_t i){return weight(i)>0?velocities_[i]:(targets[i]-previousTargets[i])*float(1/(count*step));};
            auto fabricVelocity=(sampleVelocity(face[0])+sampleVelocity(face[1])+sampleVelocity(face[2]))*(1.f/3);
            // Symmetric two-sided pressure; reversing face winding changes no force.
            const float speed=std::clamp(dot(environment.wind.velocity-fabricVelocity,normal),-100.f,100.f);
            auto force=normal*(.25f*environment.wind.density*definition_.aerodynamicDrag*twiceArea*speed*std::abs(speed)/3);
            if(dot(force,force)>0)++stats_.aerodynamicFaces;
            for(auto index:face)aerodynamic[index]=aerodynamic[index]+force*weight(index);
        }
        const float alpha=float(sub+1)/count;
        for(std::size_t i=0;i<positions_.size();++i) {
            targets_[i]=previousTargets[i]*(1-alpha)+targets[i]*alpha;
            if(definition_.maxDistance[i]==0){positions_[i]=targets_[i];continue;}
            auto drag=aerodynamic[i];float dragLength=std::hypot(drag.x,drag.y,drag.z);if(dragLength>10000)drag=drag*(10000/dragLength);
            velocities_[i]=velocities_[i]*std::exp(-definition_.damping*float(step))+(acceleration+drag)*float(step);
            positions_[i]=positions_[i]+velocities_[i]*float(step);
        }
        for(unsigned iteration=0;iteration<definition_.iterations;++iteration) {
            for(std::size_t i=0;i<lengths_.size();++i) {
                auto l=definition_.links[i];float wa=weight(l.a),wb=weight(l.b);
                auto d=positions_[l.b]-positions_[l.a];const auto distance=length(d);
                if(wa+wb==0||distance<1e-7f)continue;
                const float compliance=l.compliance/float(step*step);
                const float correction=(-(distance-lengths_[i])-compliance*lambda[i])/(wa+wb+compliance);
                lambda[i]+=correction;d=d*(correction/distance);
                positions_[l.a]=positions_[l.a]-d*wa;positions_[l.b]=positions_[l.b]+d*wb;
            }
            for(std::size_t i=0;i<positions_.size();++i) {
                if(definition_.maxDistance[i]==0)continue;
                auto displacement=positions_[i]-targets_[i];const auto distance=length(displacement);
                if(distance>definition_.maxDistance[i])positions_[i]=targets_[i]+displacement*(definition_.maxDistance[i]/distance);
            }
            // Self-contact precedes external collision, so solid clearance wins.
            separateSelf();
            separateSurface(old);
            separateEdges(old);
            for(std::size_t i=0;i<positions_.size();++i) {
                if(definition_.maxDistance[i]==0)continue;
                // Collision is last: incompatible tethers may yield to body clearance.
                for(std::size_t collider=0;collider<capsules.size();++collider) {
                    auto c=capsules[collider];
                    const auto motion=environment.capsuleTranslations.empty()?Vec3{}:environment.capsuleTranslations[collider];
                    c.a=c.a-motion*(1-alpha);c.b=c.b-motion*(1-alpha);
                    const auto travel=motion*(1.f/count),surfaceVelocity=travel*float(1/step);
                    const bool moving=dot(motion,motion)>0;
                    if(definition_.sweptCollision) {
                        // Translate the old particle into the collider's end frame.
                        const auto from=old[i]+travel;
                        auto hit=cloth_detail::sweep(from,positions_[i],c.radius+definition_.thickness,cloth_detail::magnitude(positions_[i]-from),
                            [&](float,Vec3 p){return cloth_detail::capsule(p,c);},true);
                        if(hit) {
                            float depth=hit->separation-dot(positions_[i]-hit->closest.point,hit->normal);
                            if(depth>0){positions_[i]=positions_[i]+hit->normal*depth;contact[i]=hit->normal;contactVelocity[i]=surfaceVelocity;++stats_.sweptContacts;if(moving)++stats_.movingContacts;}
                        }
                    }
                    auto axis=c.b-c.a;float axisLength=dot(axis,axis);
                    auto center=c.a+axis*(axisLength>1e-12f?std::clamp(dot(positions_[i]-c.a,axis)/axisLength,0.f,1.f):0.f);
                    auto radial=positions_[i]-center;float radius=length(radial),safe=c.radius+definition_.thickness;
                    if(radius>=safe)continue;
                    Vec3 normal=radius>1e-7f?radial*(1/radius):normalized(cross(axis,{1,0,0}));
                    if(dot(normal,normal)<.5f)normal={0,0,1};
                    positions_[i]=center+normal*safe;contact[i]=normal;contactVelocity[i]=surfaceVelocity;if(moving)++stats_.movingContacts;
                }
                for(auto b:environment.boxes) {
                    // Inflated AABB gives conservative thickness at corners.
                    Vec3 t{definition_.thickness,definition_.thickness,definition_.thickness};
                    auto lo=b.minimum-t,hi=b.maximum+t,p=positions_[i];
                    if(definition_.sweptCollision)if(auto hit=cloth_detail::box(old[i],p,lo,hi)) {
                        float depth=-dot(p-hit->closest.point,hit->normal);
                        if(depth>0){p=p+hit->normal*depth;positions_[i]=p;contact[i]=hit->normal;contactVelocity[i]={};++stats_.sweptContacts;++stats_.environmentContacts;}
                    }
                    if(p.x<lo.x||p.x>hi.x||p.y<lo.y||p.y>hi.y||p.z<lo.z||p.z>hi.z)continue;
                    const std::array<float,6> depths{p.x-lo.x,hi.x-p.x,p.y-lo.y,hi.y-p.y,p.z-lo.z,hi.z-p.z};
                    const std::array<Vec3,6> normals{Vec3{-1,0,0},Vec3{1,0,0},Vec3{0,-1,0},Vec3{0,1,0},Vec3{0,0,-1},Vec3{0,0,1}};
                    const auto face=std::size_t(std::min_element(depths.begin(),depths.end())-depths.begin());
                    if(depths[face]<=0)continue;
                    positions_[i]=p+normals[face]*depths[face];contact[i]=normals[face];contactVelocity[i]={};++stats_.environmentContacts;
                }
                for(std::size_t collider=0;collider<environment.triangles.size();++collider) {
                    auto t=environment.triangles[collider];
                    const auto motion=environment.triangleTranslations.empty()?Vec3{}:environment.triangleTranslations[collider];
                    const auto shift=motion*(1-alpha),travel=motion*(1.f/count);
                    t.a=t.a-shift;t.b=t.b-shift;t.c=t.c-shift;
                    auto hit=cloth_detail::surface(old[i]+travel,positions_[i],t,t,definition_.thickness,definition_.sweptCollision);
                    if(!hit)continue;
                    float depth=hit->separation-dot(positions_[i]-hit->closest.point,hit->normal);
                    if(depth<=0)continue;
                    positions_[i]=positions_[i]+hit->normal*depth;contact[i]=hit->normal;contactVelocity[i]=travel*float(1/step);
                    if(dot(motion,motion)>0)++stats_.movingContacts;
                    ++stats_.environmentContacts;++stats_.surfaceContacts;if(definition_.sweptCollision&&hit->time<1)++stats_.sweptContacts;
                }
                for(auto p:environment.planes) {
                    float depth=p.offset+definition_.thickness-dot(p.normal,positions_[i]);
                    if(depth<=0)continue;
                    positions_[i]=positions_[i]+p.normal*(depth/dot(p.normal,p.normal));contact[i]=normalized(p.normal);contactVelocity[i]={};++stats_.environmentContacts;
                }
            }
        }
        for(std::size_t i=0;i<positions_.size();++i) {
            vectorValid(positions_[i]);
            velocities_[i]=definition_.maxDistance[i]>0?(positions_[i]-old[i])*float(1/step):Vec3{};
            auto n=contact[i];if(dot(n,n)>.5f){auto relative=velocities_[i]-contactVelocity[i];float vn=dot(relative,n);
                velocities_[i]=(relative-n*vn)*(1-definition_.friction)+n*std::max(0.f,vn)+contactVelocity[i];}
            float speed=length(velocities_[i]);if(speed>50)velocities_[i]=velocities_[i]*(50/speed);
        }
        ++stats_.steps;
    }
}
} // namespace engine
