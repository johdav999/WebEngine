#include "engine/water_region.hpp"
#include <bit>
#include <charconv>
#include <algorithm>

namespace engine {
namespace {
constexpr std::string_view prefix="WebEngine.WaterRegion.",version="WebEngine.WaterRegion.v1:";
void require(bool ok){if(!ok)throw std::invalid_argument("Invalid finite water region");}
std::string word(std::uint32_t value){char buffer[8];std::fill(std::begin(buffer),std::end(buffer),'0');char digits[8];const auto end=std::to_chars(digits,digits+8,value,16).ptr;std::copy(digits,end,buffer+8-(end-digits));return {buffer,8};}
}
void WaterRegion::validate()const{
    require(outline.size()>=3&&outline.size()<=16&&std::isfinite(bottom)&&bottom<0&&bottom>=-10000);
    for(auto p:outline)require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=10000&&std::abs(p.y)<=10000);
    for(unsigned i=0;i<outline.size();++i){const auto a=outline[i],b=outline[(i+1)%outline.size()];const float x=b.x-a.x,z=b.y-a.y,l=std::sqrt(x*x+z*z);require(l>.001f);
        // Strict convexity, counterclockwise in XZ, also rejects repeated points,
        // self-intersections and collinear edges, independent of vertex ordering.
        for(unsigned j=0;j<outline.size();++j)if(j!=i&&j!=(i+1)%outline.size())require((x*(outline[j].y-a.y)-z*(outline[j].x-a.x))/l>1e-5f);}
}
std::array<float,4> WaterRegion::plane(std::size_t i)const{
    const auto a=outline[i],b=outline[(i+1)%outline.size()];const float x=b.x-a.x,z=b.y-a.y,inv=1/std::sqrt(x*x+z*z);
    return {-z*inv,x*inv,(z*a.x-x*a.y)*inv,0};
}
bool WaterRegion::contains(Vec3 p)const{
    validate();
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||p.y>0||p.y<bottom)return false;
    for(unsigned i=0;i<outline.size();++i){const auto e=plane(i);if(e[0]*p.x+e[1]*p.z+e[2]<-1e-6f)return false;}return true;
}
void setWaterRegion(Component& component,const std::optional<WaterRegion>& region){
    std::string tag;if(region){region->validate();tag=std::string(version)+word(unsigned(region->outline.size()))+word(std::bit_cast<std::uint32_t>(region->bottom));
        for(auto p:region->outline)tag+=word(std::bit_cast<std::uint32_t>(p.x))+word(std::bit_cast<std::uint32_t>(p.y));}
    auto snapshot=component.serialize();std::erase_if(snapshot.tags,[](const auto& t){return t.starts_with(prefix);});if(region)snapshot.tags.push_back(std::move(tag));component.deserialize(snapshot);
}
std::optional<WaterRegion> waterRegion(const Component& component){
    std::optional<WaterRegion> out;
    for(const auto& tag:component.tags())if(tag.starts_with(prefix)){
        require(!out&&tag.starts_with(version)&&tag.size()>=version.size()+16&&tag.size()<=version.size()+16+16*16);
        std::size_t cursor=version.size();auto read=[&](){require(cursor+8<=tag.size());std::uint32_t value=0;const auto r=std::from_chars(tag.data()+cursor,tag.data()+cursor+8,value,16);require(r.ec==std::errc{}&&r.ptr==tag.data()+cursor+8);cursor+=8;return value;};
        const auto n=read();require(n>=3&&n<=16&&tag.size()==version.size()+16+n*16);WaterRegion region;region.bottom=std::bit_cast<float>(read());
        for(unsigned i=0;i<n;++i){const float x=std::bit_cast<float>(read()),z=std::bit_cast<float>(read());region.outline.push_back({x,z});}region.validate();out=std::move(region);
    }return out;
}
} // namespace engine
