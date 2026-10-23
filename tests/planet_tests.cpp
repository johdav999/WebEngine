#include "engine/planet.hpp"
#include <iostream>
#include <set>

using namespace engine;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid planet data was accepted");}
void coverage(const std::vector<PlanetDraw>& draws){
    std::array<double,6> area{};std::set<PlanetTile> unique;
    for(const auto& draw:draws){const auto t=draw.patch->tile;check(unique.insert(t).second,"Duplicate leaf");area[t.face]+=std::ldexp(1.,-2*int(t.level));
        for(auto other:unique)if(other!=t&&other.face==t.face){auto child=t,ancestor=other;if(child.level<ancestor.level)std::swap(child,ancestor);
            const auto shift=child.level-ancestor.level;check((child.x>>shift)!=ancestor.x||(child.y>>shift)!=ancestor.y,"Overlapping leaf coverage");}}
    for(auto a:area)check(std::abs(a-1)<1e-12,"Planet face has missing coverage");
}
}
int main(){try{
    PlanetTerrain terrain;
    rejects([]{PlanetSettings s;s.radius=0;PlanetTerrain invalid(s);});rejects([]{PlanetSettings s;s.cells=7;PlanetTerrain invalid(s);});
    rejects([]{PlanetSettings s;s.maxLeaves=5;PlanetTerrain invalid(s);});rejects([]{PlanetTerrain::direction({6,0,0,0},0,0);});
    rejects([]{PlanetTerrain::direction({0,24,1u<<24,0},0,0);});rejects([]{PlanetPosition{}.unit();});
    const PlanetPosition far{1e12,2e12,-3e12},close{1e12+.125,2e12-.25,-3e12+.5};
    const auto delta=close.relativeTo(far);check(delta.x==.125f&&delta.y==-.25f&&delta.z==.5f,"Double subtraction was narrowed early");
    // Every root edge sample must have an identical partner on another face.
    for(unsigned face=0;face<6;++face)for(unsigned edge=0;edge<4;++edge)for(unsigned i=0;i<=16;++i){
        const double t=double(i)/16;const double u=edge<2?double(edge):t,v=edge<2?t:double(edge-2);
        const auto d=PlanetTerrain::direction({face,0,0,0},u,v);double best=1;
        for(unsigned other=0;other<6;++other)if(other!=face)for(unsigned e=0;e<4;++e)for(unsigned j=0;j<=16;++j){
            const double s=double(j)/16;const auto b=PlanetTerrain::direction({other,0,0,0},e<2?double(e):s,e<2?s:double(e-2));best=std::min(best,(b-d).length());}
        check(best<1e-14,"Cube face source seam");
    }
    auto root=terrain.prepare({4,0,0,0}),child=terrain.prepare({4,1,0,0});
    for(auto patch:{root,child}){
        for(const auto& vertex:patch->mesh->vertices){check(std::isfinite(dot(vertex.position,vertex.position)),"Nonfinite vertex");check(std::abs(dot(vertex.normal,vertex.normal)-1)<1e-4,"Invalid normal");}
        for(auto index:patch->mesh->indices)check(index<patch->mesh->vertices.size(),"Index outside mesh");
        const auto& mesh=*patch->mesh;for(unsigned i=0;i<terrain.settings().cells*terrain.settings().cells*6;i+=3){const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
            check(dot(cross(b-a,c-a),mesh.vertices[mesh.indices[i]].normal)>0,"Planet triangle winding points inward");}
    }
    // Child morph endpoints agree with the actual parent's triangle interpolation.
    const unsigned n=terrain.settings().cells,stride=n+1;
    for(unsigned y=0;y<=n;y+=2)for(unsigned x=0;x<=n;x+=2){const auto& vertex=child->mesh->vertices[y*stride+x];const auto offset=child->mesh->morphTargets[0].positions[y*stride+x];
        const auto p=child->anchor+PlanetPosition{vertex.position.x+offset.x,vertex.position.y+offset.y,vertex.position.z+offset.z};
        const auto expected=terrain.surface(PlanetTerrain::direction({4,0,0,0},double(x)/(n*2),double(y)/(n*2)));
        check((p-expected).length()<1,"Child-to-parent morph mismatch");}
    const auto home=PlanetPosition{.28,.22,1}.unit();const auto source=terrain.surface(home);
    PlanetView view{home*(terrain.settings().radius*2), (home*-1).relativeTo({}),pi/3,900};
    auto draws=terrain.update(view,.05);coverage(draws);
    const auto before=terrain.stats().generated;auto bad=view;bad.verticalFov=0;rejects([&]{terrain.update(bad,.05);});check(terrain.stats().generated==before,"Rejected update changed state");
    // Full continuous descent, persistent planet instance, bounded preparation.
    std::uint32_t deepest=0;bool morphing=false;
    for(unsigned i=0;i<900;++i){const double t=std::min(1.,double(i)/650),altitude=std::exp(std::log(terrain.settings().radius)*(1-t)+std::log(4.)*t);
        view.position=home*(terrain.settings().radius+terrain.height(home)+altitude);const auto old=terrain.stats().generated;draws=terrain.update(view,.05);
        check(terrain.stats().generated-old<=4,"Preparation exceeded four-mesh update budget");check(draws.size()<=terrain.settings().maxLeaves,"Leaf budget exceeded");
        check(terrain.stats().resident<=draws.size()*2,"Resident tree is unbounded");
        for(const auto& d:draws){check(d.parentWeight>=0&&d.parentWeight<=1,"Morph outside range");morphing=morphing||(d.parentWeight>0&&d.parentWeight<1);
            const auto ray=d.patch->anchor-view.position;const double raySquared=ray.x*ray.x+ray.y*ray.y+ray.z*ray.z;
            if(raySquared>0){const double t=std::clamp(-(view.position.x*ray.x+view.position.y*ray.y+view.position.z*ray.z)/raySquared,0.,1.);
                if((view.position+ray*t).length()>terrain.settings().radius+.01)check(d.horizonVisible,"Horizon culled an unobstructed raised patch");}}
        if(i%90==0)coverage(draws);deepest=std::max(deepest,terrain.stats().deepest);
    }
    check(deepest>=14,"Descent did not produce ground detail");check(morphing,"Refinement never morphed");check((terrain.surface(home)-source).length()==0,"LOD changed source terrain");
    const auto fine=terrain.stats().leaves;view.position=home*(terrain.settings().radius*10);for(unsigned i=0;i<400;++i)draws=terrain.update(view,.05);
    coverage(draws);check(terrain.stats().merges>0&&terrain.stats().leaves<fine,"Ascent did not release detailed terrain");
    // Origin changes preserve the same global vertex, within float local precision.
    const auto patch=terrain.prepare({4,18,160000,110000});const auto vertex=patch->mesh->vertices[0].position;const auto global=patch->anchor+PlanetPosition{vertex.x,vertex.y,vertex.z};
    const auto a=patch->anchor.relativeTo(global);check((a+vertex).x==0&&(a+vertex).y==0&&(a+vertex).z==0,"Patch relative position drift");
    std::cout<<"Planet tests passed: cube seams, parent morph, precise origin, winding, coverage, invalid preservation, bounded descent/ascent; deepest="<<deepest<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
