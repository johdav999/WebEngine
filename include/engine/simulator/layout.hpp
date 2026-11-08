#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace engine::simulator {
struct GridCell {int x=0,z=0;bool operator==(const GridCell&) const=default;};
struct GridRect {int x=0,z=0,width=1,depth=1;};
struct Placement {
    std::uint64_t id=0;
    int x=0,z=0,width=1,depth=1,rotation=0;
    bool operator==(const Placement&) const=default;
};
enum class LayoutError {None,Invalid,Duplicate,Missing,Bounds,Overlap,Clearance,Unreachable,Limit};
inline const char* describe(LayoutError e) {
    switch(e) {
    case LayoutError::None:return "Placement accepted.";
    case LayoutError::Invalid:return "Invalid footprint or rotation.";
    case LayoutError::Duplicate:return "Furniture identity already exists.";
    case LayoutError::Missing:return "Furniture is not placed.";
    case LayoutError::Bounds:return "Keep the footprint and access space inside the room.";
    case LayoutError::Overlap:return "Furniture overlaps another object.";
    case LayoutError::Clearance:return "Leave a clear cell around furniture and interaction points.";
    case LayoutError::Unreachable:return "This blocks a required route from the entrance.";
    case LayoutError::Limit:return "The furniture limit is reached.";
    }
    return "Invalid layout.";
}
// Bounded discrete placement validation. No scene, navigation follower or money ownership.
// Coordinates are lower-left anchors; quarter turns also rotate the front access cell.
class GridLayout {
    int width_,depth_;
    std::vector<GridRect> fixed_;
    std::vector<GridCell> required_;
    std::vector<Placement> placed_;
    bool inside(GridCell c) const {return c.x>=0 && c.z>=0 && c.x<width_ && c.z<depth_;}
    static bool contains(GridRect r,GridCell c) {return c.x>=r.x && c.z>=r.z && c.x<r.x+r.width && c.z<r.z+r.depth;}
    static bool overlaps(GridRect a,GridRect b) {return a.x<b.x+b.width && b.x<a.x+a.width && a.z<b.z+b.depth && b.z<a.z+a.depth;}
public:
    GridLayout(int width,int depth,std::vector<GridRect> fixed,std::vector<GridCell> required)
        :width_(width),depth_(depth),fixed_(std::move(fixed)),required_(std::move(required)) {
        if(width<1 || depth<1 || width>64 || depth>64 || fixed_.size()>256 || required_.empty() || required_.size()>256)
            throw std::invalid_argument("Invalid layout limits");
        for(auto r:fixed_)if(r.width<1 || r.depth<1 || r.width>width || r.depth>depth || !inside({r.x,r.z}) || r.x>width-r.width || r.z>depth-r.depth)
            throw std::invalid_argument("Invalid fixed footprint");
        if(validate({})!=LayoutError::None)throw std::invalid_argument("Unreachable fixed layout");
    }
    int width() const {return width_;} int depth() const {return depth_;}
    const std::vector<Placement>& placements() const {return placed_;}
    std::optional<Placement> find(std::uint64_t id) const {for(auto p:placed_)if(p.id==id)return p;return {};}
    static GridRect footprint(Placement p) {return {p.x,p.z,p.rotation%2?p.depth:p.width,p.rotation%2?p.width:p.depth};}
    static GridCell access(Placement p) {
        switch(p.rotation) {
        case 0:return {p.x+p.width/2,p.z+p.depth};
        case 1:return {p.x-1,p.z+p.width/2};
        case 2:return {p.x+(p.width-1)/2,p.z-1};
        default:return {p.x+p.depth,p.z+(p.width-1)/2};
        }
    }
    static std::optional<int> snap(double world,double origin,double cellSize) {
        if(!std::isfinite(world) || !std::isfinite(origin) || !std::isfinite(cellSize) || cellSize<=0)return {};
        double n=std::round((world-origin)/cellSize);
        if(!std::isfinite(n) || n < -1000000 || n > 1000000)return {};
        return int(n);
    }
    bool blocked(GridCell c) const {
        if(!inside(c))return true;
        for(auto r:fixed_)if(contains(r,c))return true;
        for(auto p:placed_)if(contains(footprint(p),c))return true;
        return false;
    }
    LayoutError validate(std::span<const Placement> next) const {
        if(next.size()>256)return LayoutError::Limit;
        std::vector<std::uint8_t> mask(std::size_t(width_*depth_),0);
        auto fill=[&](GridRect r){for(int z=r.z;z<r.z+r.depth;++z)for(int x=r.x;x<r.x+r.width;++x)mask[std::size_t(z*width_+x)]=1;};
        for(auto r:fixed_)fill(r);
        for(std::size_t i=0;i<next.size();++i) {
            auto p=next[i];
            if(!p.id || p.width<1 || p.depth<1 || p.width>64 || p.depth>64 || p.rotation<0 || p.rotation>3)return LayoutError::Invalid;
            auto r=footprint(p);
            if(!inside({r.x,r.z}) || r.x>width_-r.width || r.z>depth_-r.depth || !inside(access(p)))return LayoutError::Bounds;
            for(std::size_t j=0;j<i;++j)if(next[j].id==p.id)return LayoutError::Duplicate;
            for(auto f:fixed_)if(overlaps(r,f))return LayoutError::Overlap;
            for(std::size_t j=0;j<i;++j) {
                auto other=footprint(next[j]);
                if(overlaps(r,other))return LayoutError::Overlap;
                auto padded=other;--padded.x;--padded.z;padded.width+=2;padded.depth+=2;
                if(overlaps(r,padded))return LayoutError::Clearance;
            }
            fill(r);
        }
        auto targets=required_;
        for(auto p:next)targets.push_back(access(p));
        for(auto c:targets)if(!inside(c) || mask[std::size_t(c.z*width_+c.x)])return LayoutError::Clearance;
        std::vector<GridCell> frontier{targets.front()};mask[std::size_t(targets.front().z*width_+targets.front().x)]=2;
        for(std::size_t i=0;i<frontier.size();++i)for(auto d:std::array{GridCell{1,0},GridCell{-1,0},GridCell{0,1},GridCell{0,-1}}) {
            GridCell c{frontier[i].x+d.x,frontier[i].z+d.z};
            if(inside(c) && mask[std::size_t(c.z*width_+c.x)]==0){mask[std::size_t(c.z*width_+c.x)]=2;frontier.push_back(c);}
        }
        for(auto c:targets)if(mask[std::size_t(c.z*width_+c.x)]!=2)return LayoutError::Unreachable;
        return LayoutError::None;
    }
    LayoutError restore(std::span<const Placement> next) {
        auto error=validate(next);if(error!=LayoutError::None)return error;
        std::vector<Placement> candidate(next.begin(),next.end());
        std::sort(candidate.begin(),candidate.end(),[](auto a,auto b){return a.id<b.id;});
        placed_.swap(candidate);return LayoutError::None;
    }
    LayoutError place(Placement p) {
        if(find(p.id))return LayoutError::Duplicate;
        auto next=placed_;next.push_back(p);return restore(next);
    }
    LayoutError move(std::uint64_t id,int x,int z,int rotation) {
        auto next=placed_;auto i=std::find_if(next.begin(),next.end(),[&](auto p){return p.id==id;});
        if(i==next.end())return LayoutError::Missing;
        i->x=x;i->z=z;i->rotation=rotation;return restore(next);
    }
    LayoutError erase(std::uint64_t id) {
        if(!find(id))return LayoutError::Missing;
        auto next=placed_;std::erase_if(next,[&](auto p){return p.id==id;});return restore(next);
    }
};
}
