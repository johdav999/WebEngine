#include "engine/physical_surface.hpp"
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace engine {
void PhysicalSurface::validate() const {
    if(id.empty()||id.size()>128)throw std::invalid_argument("Surface ID must contain 1..128 ASCII identifier characters");
    for(unsigned char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'||c==':'))throw std::invalid_argument("Invalid surface ID");
    auto range=[](float n,float hi){if(!std::isfinite(n)||n<0||n>hi)throw std::invalid_argument("Invalid physical surface coefficient");};
    range(friction,10);range(restitution,1);range(tyreGrip,10);range(rollingResistance,1);
}
std::size_t physicalSurfaceBytes(const std::shared_ptr<const PhysicalSurface>& fallback,const std::vector<std::shared_ptr<const PhysicalSurface>>& cells){
    std::size_t bytes=cells.size()*sizeof(std::shared_ptr<const PhysicalSurface>);std::unordered_set<const PhysicalSurface*> seen;
    auto count=[&](const auto& s){if(s&&seen.insert(s.get()).second)bytes+=sizeof(PhysicalSurface)+s->id.size();};count(fallback);for(const auto& s:cells)count(s);return bytes;
}
}
