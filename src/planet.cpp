#include "engine/planet.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace engine {
namespace {
double scalar(PlanetPosition a,PlanetPosition b){return a.x*b.x+a.y*b.y+a.z*b.z;}
PlanetPosition vector(PlanetPosition a,PlanetPosition b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
void validTile(PlanetTile t){
    if(t.face>=6||t.level>24||t.x>=(1u<<t.level)||t.y>=(1u<<t.level))throw std::invalid_argument("Invalid planet tile");
}
PlanetTile parent(PlanetTile t){return {t.face,t.level-1,t.x/2,t.y/2};}
std::array<PlanetTile,4> children(PlanetTile t){return {{{t.face,t.level+1,t.x*2,t.y*2},{t.face,t.level+1,t.x*2+1,t.y*2},{t.face,t.level+1,t.x*2,t.y*2+1},{t.face,t.level+1,t.x*2+1,t.y*2+1}}};}
PlanetPosition parentSurface(const PlanetTerrain& terrain,PlanetTile tile,double u,double v){
    if(!tile.level)return terrain.surface(PlanetTerrain::direction(tile,u,v));
    const auto p=parent(tile);const auto n=terrain.settings().cells;
    const double gx=((tile.x%2+u)*.5)*n,gy=((tile.y%2+v)*.5)*n;
    const auto ix=std::min(std::uint32_t(gx),n-1),iy=std::min(std::uint32_t(gy),n-1);
    const double s=gx-ix,t=gy-iy;
    auto sample=[&](unsigned x,unsigned y){return terrain.surface(PlanetTerrain::direction(p,double(x)/n,double(y)/n));};
    const auto b=sample(ix+1,iy),c=sample(ix,iy+1);
    return s+t<=1?sample(ix,iy)*(1-s-t)+b*s+c*t:b*(1-t)+c*(1-s)+sample(ix+1,iy+1)*(s+t-1);
}
}
bool PlanetPosition::finite() const {return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);}
PlanetPosition PlanetPosition::unit() const {
    const double n=length();if(!finite()||!std::isfinite(n)||n<1e-20)throw std::invalid_argument("Invalid planet direction");return *this*(1/n);
}
Vec3 PlanetPosition::relativeTo(PlanetPosition origin) const {
    const auto d=*this-origin;
    if(!d.finite()||std::max({std::abs(d.x),std::abs(d.y),std::abs(d.z)})>1e15)throw std::invalid_argument("Invalid planet relative position");
    return {float(d.x),float(d.y),float(d.z)};
}
void PlanetSettings::validate() const {
    if(!std::isfinite(radius)||radius<1000||radius>1e10||!std::isfinite(heightScale)||heightScale<0||heightScale>radius*.05||
       cells<4||cells>32||(cells&(cells-1))||maxLevel>24||maxLeaves<6||maxLeaves>1024||
       !std::isfinite(targetPixels)||targetPixels<.5f||targetPixels>32||!std::isfinite(morphSeconds)||morphSeconds<.05f||morphSeconds>5)
        throw std::invalid_argument("Invalid planet settings");
}
void PlanetView::validate() const {
    const double d=position.length();
    if(!position.finite()||!std::isfinite(d)||d<1||d>1e14||!std::isfinite(dot(forward,forward))||dot(forward,forward)<.5f||
       !std::isfinite(verticalFov)||verticalFov<.1f||verticalFov>3||!std::isfinite(viewportHeight)||viewportHeight<1||viewportHeight>16384)
        throw std::invalid_argument("Invalid planet view");
}
PlanetPosition PlanetTerrain::direction(PlanetTile t,double u,double v){
    validTile(t);if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)throw std::invalid_argument("Invalid planet sample");
    const double scale=2.0/(1u<<t.level),a=-1+(t.x+u)*scale,b=-1+(t.y+v)*scale;
    switch(t.face){case 0:return PlanetPosition{1,b,-a}.unit();case 1:return PlanetPosition{-1,b,a}.unit();
    case 2:return PlanetPosition{a,1,-b}.unit();case 3:return PlanetPosition{a,-1,b}.unit();
    case 4:return PlanetPosition{a,b,1}.unit();default:return PlanetPosition{-a,b,-1}.unit();}
}
double PlanetTerrain::height(PlanetPosition direction) const {
    const auto d=direction.unit();
    // One continuous 3D source, independent of face, residency, origin and LOD.
    auto wave=[&](double f){return std::sin(d.x*f+std::sin(d.z*f*.73))*std::cos(d.y*f*.83-d.z*f*.31);};
    const double continent=.5+.5*wave(5);
    const double mountains=std::pow(.5+.5*wave(1200),3);
    double h=.12+.36*continent+.28*mountains;
    for(unsigned i=0;i<9;++i)h+=.055*std::pow(.43,double(i))*wave(55*std::pow(3.,double(i)));
    return settings_.heightScale*std::max(.005,h);
}
PlanetPosition PlanetTerrain::surface(PlanetPosition d) const {const auto n=d.unit();return n*(settings_.radius+height(n));}
std::shared_ptr<const PlanetPatch> PlanetTerrain::prepare(PlanetTile tile) const {
    validTile(tile);if(tile.level>settings_.maxLevel)throw std::invalid_argument("Planet tile exceeds configured level");
    auto patch=std::make_shared<PlanetPatch>();patch->tile=tile;patch->anchor=surface(direction(tile,.5,.5));
    const auto n=settings_.cells,stride=n+1;
    const double width=2*settings_.radius/(1u<<tile.level),step=width/n;
    patch->error=2*step*step/settings_.radius;
    for(unsigned i=0;i<9;++i){const double frequency=55*std::pow(3.,double(i)),a=settings_.heightScale*.055*std::pow(.43,double(i));
        patch->error+=a*std::min(1.,std::pow(step*frequency/settings_.radius,2));}
    patch->error+=settings_.heightScale*.5*std::min(1.,std::pow(step*1200/settings_.radius,2));
    const double skirt=std::min(settings_.radius*.3,std::max(2.,patch->error*5));
    auto mesh=std::make_shared<Mesh>();MorphTarget morph;morph.name="parent";
    auto texture=std::make_shared<Texture>();texture->name="Planet surface";texture->width=stride;texture->height=stride;
    texture->wrapU=texture->wrapV=TextureWrap::Clamp;
    for(unsigned y=0;y<=n;++y)for(unsigned x=0;x<=n;++x){
        const double u=double(x)/n,v=double(y)/n;const auto d=direction(tile,u,v),p=surface(d);
        const auto tangent=vector(std::abs(d.y)<.9?PlanetPosition{0,1,0}:PlanetPosition{1,0,0},d).unit();
        const auto bitangent=vector(d,tangent);const double eps=.5/settings_.radius;
        const auto dx=surface(d+tangent*eps)-surface(d-tangent*eps),dy=surface(d+bitangent*eps)-surface(d-bitangent*eps);
        const auto normal=vector(dx,dy).unit();
        mesh->vertices.push_back({p.relativeTo(patch->anchor),normal.relativeTo({}),{float(u),float(v)}});
        const auto parentPoint=parentSurface(*this,tile,u,v);
        morph.positions.push_back((parentPoint-p).relativeTo({}));
        patch->boundRadius=std::max(patch->boundRadius,(p-patch->anchor).length());
        patch->boundRadius=std::max(patch->boundRadius,(parentPoint-patch->anchor).length());
        const double h=height(d)/std::max(1.,settings_.heightScale),snow=std::clamp((h-.62)*8+std::max(0.,std::abs(d.y)-.8)*3,0.,1.);
        const double rock=std::clamp((1-scalar(normal,d))*5+(h-.45)*3,0.,1.);
        const double detail=.85+.15*std::sin(d.x*170000+d.z*110000)*std::sin(d.y*130000);
        const double base[3]{.16+.28*rock,.28+.08*rock,.10+.17*rock};
        for(double c:base)texture->pixels.push_back(std::uint8_t(std::clamp((c*(1-snow)+.86*snow)*detail*255,0.,255.)));
        texture->pixels.push_back(255);
    }
    for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){const auto a=y*stride+x,b=a+1,c=a+stride,d=c+1;mesh->indices.insert(mesh->indices.end(),{a,b,c,b,d,c});}
    auto edge=[&](std::vector<unsigned> indices){
        for(std::size_t i=0;i<indices.size();++i){const unsigned a=indices[i],b=unsigned(mesh->vertices.size());auto vertex=mesh->vertices[a];
            const auto radial=(patch->anchor+PlanetPosition{vertex.position.x,vertex.position.y,vertex.position.z}).unit();
            vertex.position=vertex.position-radial.relativeTo({})*float(skirt);mesh->vertices.push_back(vertex);morph.positions.push_back(morph.positions[a]);
            if(i){const unsigned previous=indices[i-1];mesh->indices.insert(mesh->indices.end(),{previous,b,a,previous,b-1,b});}
        }
    };
    std::vector<unsigned> border;for(unsigned x=0;x<=n;++x)border.push_back(x);edge(border);
    border.clear();for(unsigned y=0;y<=n;++y)border.push_back(y*stride+n);edge(border);
    border.clear();for(unsigned x=n+1;x-->0;)border.push_back(n*stride+x);edge(border);
    border.clear();for(unsigned y=n+1;y-->0;)border.push_back(y*stride);edge(border);
    auto material=std::make_shared<Material>();material->name="Procedural planet rock and grass";material->baseColorTexture=texture;material->roughness=.95f;
    mesh->materials.push_back(material);mesh->morphTargets.push_back(std::move(morph));mesh->generateTangents();mesh->validateMorphTargets();
    patch->boundRadius+=skirt+settings_.radius*1e-7;patch->mesh=std::move(mesh);return patch;
}
PlanetTerrain::PlanetTerrain(PlanetSettings settings):settings_(settings){
    settings_.validate();for(unsigned face=0;face<6;++face){PlanetTile t{face,0,0,0};resident_.emplace(t,Entry{prepare(t),settings_.morphSeconds});leaves_.push_back(t);}stats_.generated=6;
}
bool PlanetTerrain::visible(const PlanetPatch& patch,const PlanetView& view) const {
    const double distance=view.position.length(),r=settings_.radius;
    if(distance<=r)return true;
    // Cull only a complete patch bounding sphere inside the occlusion cone and
    // beyond its tangent distance. Raised mountains can remain visible beyond
    // the zero-height horizon; a tangent-plane-only test would hide them early.
    const auto to=patch.anchor-view.position;const double length=to.length(),bound=patch.boundRadius;
    const double horizon=std::sqrt(std::max(0.,distance*distance-r*r));
    if(length<=bound||length-bound<=horizon)return true;
    const double alpha=std::asin(r/distance),beta=std::asin(std::min(1.,bound/length));
    if(beta>=alpha)return true;
    const double cosine=scalar(to,view.position.unit()*-1)/length;
    return cosine<=std::cos(alpha-beta);
}
double PlanetTerrain::projectedError(const PlanetPatch& patch,const PlanetView& view) const {
    if(!visible(patch,view))return 0;
    const auto offset=patch.anchor-view.position;const double distance=std::max(1.,offset.length()-patch.boundRadius);
    const auto f=normalized(view.forward);const PlanetPosition forward{f.x,f.y,f.z};
    if(scalar(offset,forward)<-patch.boundRadius)return 0;
    return patch.error*view.viewportHeight/(2*std::tan(view.verticalFov*.5)*distance);
}
std::vector<PlanetDraw> PlanetTerrain::update(const PlanetView& view,double deltaSeconds){
    view.validate();if(!std::isfinite(deltaSeconds)||deltaSeconds<0||deltaSeconds>1)throw std::invalid_argument("Invalid planet delta");
    for(auto& [tile,entry]:resident_){(void)tile;entry.age=std::min(double(settings_.morphSeconds),entry.age+deltaSeconds);}
    // Merge only complete stable sibling sets. Hysteresis keeps selection stationary.
    std::set<PlanetTile> leafSet(leaves_.begin(),leaves_.end());
    for(auto tile:leaves_)if(tile.level){const auto p=parent(tile);const auto siblings=children(p);
        bool complete=true;for(auto s:siblings)complete=complete&&leafSet.contains(s)&&resident_.at(s).age>=settings_.morphSeconds;
        if(complete&&projectedError(*resident_.at(p).patch,view)<settings_.targetPixels*.45){
            for(auto s:siblings){leafSet.erase(s);resident_.erase(s);}leafSet.insert(p);++stats_.merges;
        }
    }
    leaves_.assign(leafSet.begin(),leafSet.end());
    PlanetTile selected{};double best=settings_.targetPixels;
    for(auto tile:leaves_){const auto& entry=resident_.at(tile);if(tile.level>=settings_.maxLevel||entry.age<settings_.morphSeconds)continue;
        const auto error=projectedError(*entry.patch,view);if(error>best){best=error;selected=tile;}}
    // A full budget must still follow the observer: trade a less important sibling
    // family for the most important split, rather than freezing the orbital LOD.
    if(best>settings_.targetPixels&&leaves_.size()+3>settings_.maxLeaves){
        double least=best*.6;std::optional<PlanetTile> merge;
        const std::set<PlanetTile> current(leaves_.begin(),leaves_.end());
        for(auto tile:leaves_)if(tile.level){const auto p=parent(tile);bool complete=true;
            for(auto s:children(p))complete=complete&&s!=selected&&current.contains(s)&&resident_.at(s).age>=settings_.morphSeconds;
            if(complete){const double error=projectedError(*resident_.at(p).patch,view);if(error<least){least=error;merge=p;}}}
        if(merge){for(auto s:children(*merge)){resident_.erase(s);leaves_.erase(std::find(leaves_.begin(),leaves_.end(),s));}leaves_.push_back(*merge);++stats_.merges;}
        else best=0;
    }
    if(best>settings_.targetPixels){
        // Prepare the complete replacement before touching the accepted topology.
        const auto next=children(selected);std::array<Entry,4> staged;
        for(unsigned i=0;i<4;++i)staged[i]={prepare(next[i]),0};
        for(unsigned i=0;i<4;++i)resident_.emplace(next[i],std::move(staged[i]));
        leaves_.erase(std::find(leaves_.begin(),leaves_.end(),selected));leaves_.insert(leaves_.end(),next.begin(),next.end());stats_.generated+=4;++stats_.splits;
    }
    stats_.leaves=leaves_.size();stats_.resident=resident_.size();stats_.deepest=0;stats_.triangles=0;
    std::vector<PlanetDraw> result;result.reserve(leaves_.size());
    for(auto tile:leaves_){const auto& entry=resident_.at(tile);const bool horizon=visible(*entry.patch,view);
        result.push_back({entry.patch,tile.level?float(1-entry.age/settings_.morphSeconds):0,horizon});
        stats_.deepest=std::max(stats_.deepest,tile.level);if(horizon)stats_.triangles+=entry.patch->mesh->indices.size()/3;
    }
    return result;
}
} // namespace engine
