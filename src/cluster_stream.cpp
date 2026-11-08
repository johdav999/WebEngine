#include "engine/cluster_stream.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <tuple>

namespace engine {
namespace {
constexpr std::uint32_t manifestMagic=0x4c434557; // WECL
constexpr std::size_t maxGroups=64*511,maxPages=maxGroups+1;
constexpr std::size_t vertexBytes=12*sizeof(float);
constexpr auto invalid=std::numeric_limits<std::uint32_t>::max();
using Bytes=std::vector<std::byte>;
void put(Bytes& out,std::uint32_t value) {
    for(unsigned shift=0;shift<32;shift+=8)out.push_back(std::byte((value>>shift)&255));
}
void putFloat(Bytes& out,float value) { put(out,std::bit_cast<std::uint32_t>(value)); }
struct Reader {
    std::span<const std::byte> bytes;
    std::span<const std::byte> take(std::size_t size) {
        if(size>bytes.size())throw std::invalid_argument("Truncated cluster package");
        const auto result=bytes.first(size);
        bytes=bytes.subspan(size);
        return result;
    }
    std::uint32_t u32() {
        const auto data=take(4);
        std::uint32_t value=0;
        for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(data[i])<<(8*i);
        return value;
    }
    float number() {
        const auto value=std::bit_cast<float>(u32());
        if(!std::isfinite(value))throw std::invalid_argument("Non-finite cluster value");
        return value;
    }
    Vec3 vec() { return {number(),number(),number()}; }
};
std::uint64_t fingerprint(std::span<const std::byte> bytes) {
    std::uint64_t value=14695981039346656037ull;
    for(auto byte:bytes) { value^=std::to_integer<unsigned>(byte);value*=1099511628211ull; }
    return value;
}
Bytes encode(const Mesh& mesh) {
    Bytes bytes;
    bytes.reserve(mesh.vertices.size()*vertexBytes+mesh.indices.size()*4);
    for(const auto& v:mesh.vertices)for(float value:{v.position.x,v.position.y,v.position.z,
        v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y,v.tangent[0],v.tangent[1],v.tangent[2],v.tangent[3]})
        putFloat(bytes,value);
    for(auto index:mesh.indices)put(bytes,index);
    if(bytes.empty()||bytes.size()>ClusterPageAsset::maxPageBytes)throw std::length_error("Cluster page size exceeded");
    return bytes;
}
bool encloses(const MeshBounds& a,const MeshBounds& b) {
    return a.minimum.x<=b.minimum.x&&a.minimum.y<=b.minimum.y&&a.minimum.z<=b.minimum.z
        &&a.maximum.x>=b.maximum.x&&a.maximum.y>=b.maximum.y&&a.maximum.z>=b.maximum.z;
}
}

std::vector<std::byte> ClusterPageAsset::cook(const ClusterLod& lod,const PageWriter& write,std::size_t targetPageBytes) {
    if(!write||!lod.geometry()||lod.groups().empty()||targetPageBytes<16*1024||targetPageBytes>maxPageBytes)
        throw std::invalid_argument("Invalid cluster cook input or page target");
    std::vector<ClusterGroup> groups(lod.groups().begin(),lod.groups().end());
    std::array<std::vector<std::uint32_t>,9> levels;
    const auto visit=[&](auto&& self,std::uint32_t id,unsigned depth)->void {
        auto& group=groups.at(id);
        if(group.range.page)throw std::invalid_argument("Cook requires a resident hierarchy");
        levels.at(depth).push_back(id);
        if(!group.leaf())for(auto child:group.children)self(self,child,depth+1);
    };
    for(auto root:lod.roots())visit(visit,root,0);
    const auto& source=*lod.geometry();
    std::vector<std::uint32_t> remap(source.vertices.size(),invalid),used;
    Mesh page;
    Bytes root;
    std::vector<ClusterPageInfo> pages;
    const auto flush=[&] {
        auto bytes=encode(page);
        const auto id=std::uint32_t(pages.size());
        pages.push_back({std::uint32_t(bytes.size()),std::uint32_t(page.vertices.size()),
                         std::uint32_t(page.indices.size()),fingerprint(bytes)});
        if(id)write(id,bytes);
        else root=std::move(bytes);
        for(auto index:used)remap[index]=invalid;
        used.clear();page.vertices.clear();page.indices.clear();
    };
    for(unsigned depth=0;depth<levels.size();++depth) {
        for(auto id:levels[depth]) {
            auto& range=groups[id].range;
            const auto projected=page.vertices.size()*vertexBytes+page.indices.size()*4
                +std::size_t(range.indexCount)*(vertexBytes+4);
            if(depth&&!page.indices.empty()&&projected>targetPageBytes)flush();
            const auto indices=std::span(source.indices).subspan(range.firstIndex,range.indexCount);
            range.firstIndex=std::uint32_t(page.indices.size());
            range.page=std::uint32_t(pages.size());
            for(auto index:indices) {
                auto& local=remap[index];
                if(local==invalid) {
                    local=std::uint32_t(page.vertices.size());
                    page.vertices.push_back(source.vertices[index]);used.push_back(index);
                }
                page.indices.push_back(local);
            }
        }
        if(!page.indices.empty())flush();
    }
    Bytes bytes;
    for(auto value:{manifestMagic,1u,std::uint32_t(lod.roots().size()),std::uint32_t(groups.size()),std::uint32_t(pages.size())})
        put(bytes,value);
    for(auto id:lod.roots())put(bytes,id);
    for(const auto& g:groups) {
        const auto& a=g.bounds.minimum;const auto& b=g.bounds.maximum;
        for(float value:{a.x,a.y,a.z,b.x,b.y,b.z,g.error})putFloat(bytes,value);
        for(auto value:{g.range.firstIndex,g.range.indexCount,g.range.page,g.children[0],g.children[1]})put(bytes,value);
    }
    for(const auto& p:pages)for(auto value:{p.bytes,p.vertices,p.indices,std::uint32_t(p.fingerprint),std::uint32_t(p.fingerprint>>32)})put(bytes,value);
    bytes.insert(bytes.end(),root.begin(),root.end());
    if(bytes.size()>maxManifestBytes)throw std::length_error("Cluster manifest size exceeded");
    return bytes;
}

std::shared_ptr<const ClusterPageAsset> ClusterPageAsset::open(std::span<const std::byte> bytes) {
    Reader in{bytes};
    if(bytes.size()>maxManifestBytes||in.u32()!=manifestMagic||in.u32()!=1)throw std::invalid_argument("Unsupported cluster manifest");
    const auto roots=in.u32(),groups=in.u32(),pages=in.u32();
    if(!roots||roots>64||groups<roots||groups>maxGroups||!pages||pages>maxPages||pages>groups)
        throw std::invalid_argument("Invalid cluster manifest counts");
    auto result=std::make_shared<ClusterPageAsset>();
    result->lod_.reset(new ClusterLod);
    auto& lod=*result->lod_;
    for(unsigned i=0;i<roots;++i)lod.roots_.push_back(in.u32());
    for(unsigned i=0;i<groups;++i) {
        ClusterGroup g;
        g.bounds={in.vec(),in.vec(),true};
        g.error=in.number();g.range={in.u32(),in.u32(),in.u32()};g.children={in.u32(),in.u32()};
        const MeshBounds ends{g.bounds.maximum,g.bounds.minimum,true};
        if(!encloses(g.bounds,ends)||g.error<0||g.range.page>=pages||!g.range.indexCount
            ||g.range.indexCount%3||g.range.firstIndex%3||(bool(g.children[0])!=bool(g.children[1])))
            throw std::invalid_argument("Invalid cluster group");
        lod.groups_.push_back(g);
    }
    for(unsigned i=0;i<pages;++i) {
        ClusterPageInfo p{in.u32(),in.u32(),in.u32(),in.u32()};
        p.fingerprint|=std::uint64_t(in.u32())<<32;
        if(!p.vertices||!p.indices||p.bytes>maxPageBytes||p.bytes!=std::uint64_t(p.vertices)*vertexBytes+std::uint64_t(p.indices)*4)
            throw std::invalid_argument("Invalid cluster page descriptor");
        result->pages_.push_back(p);
    }
    std::vector<bool> seen(groups);
    const auto visit=[&](auto&& self,std::uint32_t id,unsigned depth)->void {
        if(id>=groups||seen[id]||depth>8)throw std::invalid_argument("Invalid cluster hierarchy");
        seen[id]=true;
        const auto& g=lod.groups_[id];
        if(bool(g.range.page)!=bool(depth))throw std::invalid_argument("Invalid cluster root residency");
        if(!g.leaf())for(auto child:g.children) {
            self(self,child,depth+1);
            const auto& finer=lod.groups_[child];
            if(finer.error>g.error||!encloses(g.bounds,finer.bounds))throw std::invalid_argument("Invalid child bounds or error");
        }
    };
    for(auto root:lod.roots_)visit(visit,root,0);
    if(std::find(seen.begin(),seen.end(),false)!=seen.end())throw std::invalid_argument("Unreachable cluster group");
    std::vector<ClusterRange> ranges;
    for(const auto& group:lod.groups_)ranges.push_back(group.range);
    std::sort(ranges.begin(),ranges.end(),[](auto a,auto b){return std::tie(a.page,a.firstIndex)<std::tie(b.page,b.firstIndex);});
    auto range=ranges.begin();
    for(unsigned page=0;page<pages;++page) {
        std::uint64_t end=0;
        for(;range!=ranges.end()&&range->page==page;++range) {
            if(range->firstIndex!=end)throw std::invalid_argument("Invalid cluster page ranges");
            end+=range->indexCount;
        }
        if(end!=result->pages_[page].indices)throw std::invalid_argument("Incomplete cluster page ranges");
    }
    lod.geometry_=result->decode(0,in.bytes);
    return result;
}

std::shared_ptr<const Mesh> ClusterPageAsset::decode(std::uint32_t page,std::span<const std::byte> bytes) const {
    const auto& info=pages_.at(page);
    if(bytes.size()!=info.bytes||fingerprint(bytes)!=info.fingerprint)throw std::invalid_argument("Cluster page size or fingerprint mismatch");
    Reader in{bytes};
    auto mesh=std::make_shared<Mesh>();
    mesh->vertices.reserve(info.vertices);mesh->indices.reserve(info.indices);
    for(unsigned i=0;i<info.vertices;++i) {
        Vertex v{in.vec(),in.vec(),{in.number(),in.number()}};
        for(auto& value:v.tangent)value=in.number();
        mesh->vertices.push_back(v);
    }
    for(unsigned i=0;i<info.indices;++i) {
        const auto index=in.u32();
        if(index>=info.vertices)throw std::invalid_argument("Cluster page index outside vertex stream");
        mesh->indices.push_back(index);
    }
    return mesh;
}

ClusterPageStream::ClusterPageStream(std::shared_ptr<const ClusterPageAsset> asset,const LoaderFactory& factory,ClusterStreamSettings settings)
    :asset_(std::move(asset)),settings_(settings) {
    if(!asset_||!factory||!settings.inFlight||settings.inFlight>32||!settings.idleUpdates||settings.fineBytes>512*1024*1024)
        throw std::invalid_argument("Invalid cluster stream configuration");
    pages_.reserve(asset_->pages().size());
    for(std::uint32_t i=0;i<asset_->pages().size();++i) {
        AssetId key{"cluster-page/"+std::to_string(i)};
        if(i)manager_.registerAsset<Mesh>(key,factory(asset_,i));
        pages_.push_back({std::move(key),{},{}});
    }
    pages_.front().mesh=asset_->hierarchy().geometry();
}
void ClusterPageStream::demand(std::uint32_t id,double priority) {
    demand(id,priority,generation_);
}
bool ClusterPageStream::demand(std::uint32_t id,double priority,std::uint64_t expectedGeneration) {
    if(expectedGeneration!=generation_||id>=pages_.size()||!(priority>0))return false;
    auto& page=pages_[id];
    page.priority=std::max(page.priority,priority);
    return true;
}
const std::shared_ptr<const Mesh>& ClusterPageStream::mesh(std::uint32_t id) const { return pages_.at(id).mesh; }
void ClusterPageStream::evict(Page& page) {
    evictions_+=bool(page.mesh);
    page.mesh.reset();
    page.request.cancel();page.request={};
    manager_.unload(page.id);
    page.pending=false;
}
void ClusterPageStream::clear() {
    if(generation_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Cluster stream generation exhausted");
    ++generation_;
    for(std::size_t i=1;i<pages_.size();++i) {
        auto& page=pages_[i];
        if(page.mesh||page.pending||page.failed)evict(page);
        page.failed=false;page.priority=0;page.idle=0;
    }
}
void ClusterPageStream::update() {
    manager_.update();
    std::vector<std::uint32_t> wanted,cold;
    std::size_t reserved=0,pending=0;
    for(std::uint32_t i=1;i<pages_.size();++i) {
        auto& page=pages_[i];
        if(page.pending) {
            const auto status=page.request.progress().status;
            if(status==AssetStatus::Ready) { page.mesh=page.request.get<Mesh>();page.pending=false; }
            else if(status==AssetStatus::Failed||status==AssetStatus::Cancelled) { evict(page);page.failed=true; }
        }
        if(page.priority>0) {
            page.idle=0;
            if(!page.mesh&&!page.pending&&!page.failed)wanted.push_back(i);
        } else if(page.mesh||page.pending) {
            if(++page.idle>=settings_.idleUpdates)evict(page);
            else cold.push_back(i);
        }
        if(page.mesh||page.pending)reserved+=asset_->pages()[i].bytes;
        pending+=page.pending;
    }
    std::sort(wanted.begin(),wanted.end(),[&](auto a,auto b){
        return pages_[a].priority==pages_[b].priority?a<b:pages_[a].priority>pages_[b].priority;
    });
    std::sort(cold.begin(),cold.end(),[&](auto a,auto b){
        return std::tie(pages_[a].idle,a)<std::tie(pages_[b].idle,b);
    });
    for(auto id:wanted) {
        const auto bytes=asset_->pages()[id].bytes;
        if(bytes>settings_.fineBytes)continue;
        while(reserved>settings_.fineBytes-bytes&&!cold.empty()) {
            const auto old=cold.back();cold.pop_back();
            reserved-=asset_->pages()[old].bytes;
            pending-=pages_[old].pending;
            evict(pages_[old]);
        }
        if(pending>=settings_.inFlight||reserved>settings_.fineBytes-bytes)continue;
        auto& page=pages_[id];
        page.request=manager_.load<Mesh>(page.id);page.pending=true;
        ++reads_;++pending;reserved+=bytes;
    }
    for(auto& page:pages_)page.priority=0;
}
ClusterStreamStats ClusterPageStream::stats() const {
    ClusterStreamStats result{0,0,0,0,reads_,evictions_};
    for(std::size_t i=1;i<pages_.size();++i) {
        const auto& page=pages_[i];
        if(page.mesh) { ++result.residentPages;result.residentBytes+=asset_->pages()[i].bytes; }
        result.pendingPages+=page.pending;result.failedPages+=page.failed;
    }
    return result;
}
} // namespace engine
