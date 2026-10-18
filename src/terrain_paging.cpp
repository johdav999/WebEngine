#include "engine/terrain_paging.hpp"
#include "engine/scene.hpp"
#include "engine/scene_archive.hpp"
#include "engine/rigid_body_component.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <set>

namespace engine {
namespace {
void require(bool condition,const char* message) {if(!condition) throw std::invalid_argument(message);}
std::uint32_t crc(std::span<const std::byte> bytes) {
    std::uint32_t value=~0u;for(auto byte:bytes) {value^=std::to_integer<unsigned>(byte);for(int i=0;i<8;++i) value=(value>>1)^(0xedb88320u&-(value&1));}return ~value;
}
struct Wire {
    std::vector<std::byte> output;std::span<const std::byte> input;std::size_t at=0;
    void put(std::uint32_t value) {for(int i=0;i<4;++i) output.push_back(std::byte((value>>(i*8))&255));}
    void put(float value) {put(std::bit_cast<std::uint32_t>(value));}
    std::uint32_t get() {require(at+4<=input.size(),"Truncated terrain transport");std::uint32_t v=0;for(int i=0;i<4;++i) v|=std::to_integer<std::uint32_t>(input[at++])<<(i*8);return v;}
    float number() {return std::bit_cast<float>(get());}
    void layout(const TerrainPageLayout& d) {put(d.samplesX);put(d.samplesZ);put(d.chunkCells);put(d.spacing.x);put(d.spacing.y);put(d.textureMeters);put(d.lodLevels);for(auto p:d.lodPixels) put(p);}
    TerrainPageLayout layout() {TerrainPageLayout d;d.samplesX=get();d.samplesZ=get();d.chunkCells=get();d.spacing={number(),number()};d.textureMeters=number();d.lodLevels=get();for(auto& p:d.lodPixels) p=number();d.validate();return d;}
};
void overlaps(const TerrainAsset& a,const TerrainAsset& b) {
    const auto& p=*a.patch();const auto& q=*b.patch();const auto& d=a.data();const auto& e=b.data();
    const auto left=std::max(p.offsetX,q.offsetX),top=std::max(p.offsetZ,q.offsetZ);
    const auto right=std::min(p.offsetX+d.samplesX,q.offsetX+e.samplesX),bottom=std::min(p.offsetZ+d.samplesZ,q.offsetZ+e.samplesZ);
    auto weight=[](const TerrainData& data,std::size_t index,int k) {return data.material->layers.weights ? data.material->layers.weights->pixels[index*4+k] : std::uint8_t(k==0 ? 255 : 0);};
    for(auto z=top;z<bottom;++z) for(auto x=left;x<right;++x) {
        const auto i=std::size_t(z-p.offsetZ)*d.samplesX+x-p.offsetX,j=std::size_t(z-q.offsetZ)*e.samplesX+x-q.offsetX;
        require(d.heights[i]==e.heights[j],"Terrain page halo heights disagree with a resident neighbor");
        for(int k=0;k<4;++k) require(weight(d,i,k)==weight(e,j,k),"Terrain page halo weights disagree with a resident neighbor");
        if(x+1<right&&z+1<bottom){const auto ci=std::size_t(z-p.offsetZ)*(d.samplesX-1)+x-p.offsetX,cj=std::size_t(z-q.offsetZ)*(e.samplesX-1)+x-q.offsetX;auto ds=d.cellSurfaces.empty()?nullptr:d.cellSurfaces[ci],es=e.cellSurfaces.empty()?nullptr:e.cellSurfaces[cj];if(!ds)ds=d.physicalSurface;if(!es)es=e.physicalSurface;require((!ds&&!es)||(ds&&es&&*ds==*es),"Terrain physical surface halos disagree");}
    }
}
std::size_t geometryBytes(const Mesh& mesh) {
    std::size_t total=mesh.vertices.size()*sizeof(Vertex)+mesh.indices.size()*sizeof(std::uint32_t);
    for(const auto& lod:mesh.lods) total+=geometryBytes(*lod.mesh);
    return total;
}
}
bool TerrainPageLayout::operator==(const TerrainPageLayout& b) const {
    return samplesX==b.samplesX&&samplesZ==b.samplesZ&&chunkCells==b.chunkCells&&spacing.x==b.spacing.x&&spacing.y==b.spacing.y&&textureMeters==b.textureMeters&&lodLevels==b.lodLevels&&lodPixels==b.lodPixels;
}
void TerrainPageLayout::validate() const {
    require(samplesX>=2&&samplesZ>=2&&samplesX<=65537&&samplesZ<=65537&&chunkCells>=1&&chunkCells<=128,"Invalid terrain page layout");
    require(std::uint64_t((samplesX-2)/chunkCells+1)*((samplesZ-2)/chunkCells+1)<=1048576,"Terrain page count exceeds budget");
    TerrainData d;d.samplesX=d.samplesZ=2;d.heights.resize(4);d.spacing=spacing;d.textureMeters=textureMeters;d.lodLevels=lodLevels;d.lodPixels=lodPixels;d.validate();
    require(double(samplesX-1)*spacing.x<=1000000&&double(samplesZ-1)*spacing.y<=1000000,"Terrain page world extent exceeds limit");
}
std::uint32_t TerrainPageLayout::pageCount() const {validate();return ((samplesX-2)/chunkCells+1)*((samplesZ-2)/chunkCells+1);}
TerrainPatch TerrainPageLayout::region(std::uint32_t page) const {
    require(page<pageCount(),"Invalid terrain page ID");const auto columns=(samplesX-2)/chunkCells+1;
    const auto x=(page%columns)*chunkCells,z=(page/columns)*chunkCells,left=x>2 ? x-2 : 0,top=z>2 ? z-2 : 0;
    return {x-left,z-top,std::min(chunkCells,samplesX-1-x),std::min(chunkCells,samplesZ-1-z),left,top,page};
}
std::pair<std::uint32_t,std::uint32_t> TerrainPageLayout::dimensions(std::uint32_t page) const {
    const auto p=region(page);return {std::min(samplesX-1,p.offsetX+p.x+p.cellsX+2)-p.offsetX+1,std::min(samplesZ-1,p.offsetZ+p.z+p.cellsZ+2)-p.offsetZ+1};
}
std::size_t TerrainPageLayout::maxPageBytes() const {validate();return std::size_t(std::min(samplesX,chunkCells+5))*std::min(samplesZ,chunkCells+5)*8;}
void TerrainPage::validate() const {
    const auto [width,height]=layout.dimensions(page);const auto count=std::size_t(width)*height;
    require(revision&&heights.size()==count&&(weights.empty()||weights.size()==count*4),"Invalid terrain page sample counts or revision");
    require(cellSurfaces.empty()||cellSurfaces.size()==std::size_t(width-1)*(height-1),"Invalid page physical cell count");if(physicalSurface)physicalSurface->validate();for(const auto& s:cellSurfaces)if(s)s->validate();
    for(float h:heights) require(std::isfinite(h)&&std::abs(h)<=1000000,"Invalid terrain page height");
}
std::vector<std::byte> encodeTerrainPage(const TerrainPage& page) {
    page.validate();Wire w;w.put(0x50544557u);w.put(page.physicalSurface||!page.cellSurfaces.empty()?3u:2u);w.layout(page.layout);w.put(page.page);w.put(std::uint32_t(page.revision));w.put(std::uint32_t(page.revision>>32));w.put(std::uint32_t(page.generation));w.put(std::uint32_t(page.generation>>32));
    w.put(std::uint32_t(page.heights.size()));w.put(std::uint32_t(page.weights.size()));for(auto h:page.heights) w.put(h);for(auto b:page.weights) w.output.push_back(std::byte(b));
    if(page.physicalSurface||!page.cellSurfaces.empty()){
        AssetLibrary library;auto index=[&](const auto& s)->std::uint32_t{if(!s)return 0;auto found=std::find(library.physicalSurfaces.begin(),library.physicalSurfaces.end(),s);if(found!=library.physicalSurfaces.end())return found-library.physicalSurfaces.begin()+1;require(library.physicalSurfaces.size()<256,"Page physical palette exceeds 256 resources");library.physicalSurfaces.push_back(s);return library.physicalSurfaces.size();};
        w.put(index(page.physicalSurface));w.put(std::uint32_t(page.cellSurfaces.size()));for(const auto& s:page.cellSurfaces)w.put(index(s));const auto archive=SceneArchive::saveAssets(library);require(archive.size()<=65536,"Page physical palette exceeds 64 KiB");w.put(std::uint32_t(archive.size()));w.output.insert(w.output.end(),archive.begin(),archive.end());
    }
    w.put(crc(w.output));require(w.output.size()<=256*1024,"Terrain page exceeds 256 KiB");return std::move(w.output);
}
TerrainPage decodeTerrainPage(std::span<const std::byte> bytes) {
    require(bytes.size()>=68&&bytes.size()<=256*1024,"Terrain page response exceeds limit or is truncated");Wire w;w.input=bytes;
    require(w.get()==0x50544557u,"Unsupported terrain page format");const auto version=w.get();require(version>=1&&version<=3,"Unsupported terrain page version");TerrainPage p;p.layout=w.layout();p.page=w.get();p.revision=w.get();p.revision|=std::uint64_t(w.get())<<32;
    if(version>=2) {p.generation=w.get();p.generation|=std::uint64_t(w.get())<<32;}
    const auto count=w.get(),weights=w.get();const auto [width,height]=p.layout.dimensions(p.page);
    require(count==std::uint64_t(width)*height&&(weights==0||weights==std::uint64_t(count)*4)&&(version<3?w.at+std::uint64_t(count)*4+weights+4==bytes.size():w.at+std::uint64_t(count)*4+weights+16<=bytes.size()),"Invalid terrain page payload size");
    p.heights.reserve(count);for(std::uint32_t i=0;i<count;++i) p.heights.push_back(w.number());p.weights.reserve(weights);
    for(std::uint32_t i=0;i<weights;++i) p.weights.push_back(std::to_integer<std::uint8_t>(bytes[w.at++]));
    if(version>=3){const auto fallback=w.get(),cells=w.get();require(cells==0||cells==std::uint64_t(width-1)*(height-1),"Invalid physical page map count");require(std::uint64_t(cells)*4+8<=bytes.size()-w.at,"Truncated physical page map");std::vector<std::uint32_t> map(cells);for(auto& i:map)i=w.get();const auto size=w.get();require(size<=65536&&w.at+size+4==bytes.size(),"Invalid page palette size");ArchiveLimits limits;limits.maxBytes=65536;limits.maxAllocatedBytes=512*1024;limits.maxResources=256;limits.maxElements=65536;auto library=SceneArchive::loadAssets(bytes.subspan(w.at,size),limits);w.at+=size;require(library.physicalSurfaces.size()<=256&&library.meshes.empty()&&library.materials.empty()&&library.textures.empty()&&library.skeletons.empty()&&library.environments.empty()&&library.emitters.empty()&&library.effects.empty()&&library.collisionShapes.empty()&&library.terrains.empty(),"Physical page palette contains unrelated resources");auto resolve=[&](std::uint32_t i){require(i<=library.physicalSurfaces.size(),"Invalid physical page palette index");return i?library.physicalSurfaces[i-1]:std::shared_ptr<const PhysicalSurface>{};};p.physicalSurface=resolve(fallback);for(auto i:map)p.cellSurfaces.push_back(resolve(i));}
    require(w.get()==crc(bytes.first(bytes.size()-4)),"Terrain page checksum mismatch");p.validate();return p;
}
namespace {
TerrainData pageData(const TerrainPage& page,std::shared_ptr<const Material> material) {
    page.validate();require(material!=nullptr,"Missing terrain page material");material->validate();require(!material->layers.weights,"Paged material must not retain a global weight map");
    const auto [width,height]=page.layout.dimensions(page.page);const auto patch=page.layout.region(page.page);
    TerrainData d;d.samplesX=width;d.samplesZ=height;d.chunkCells=page.layout.chunkCells;d.spacing=page.layout.spacing;d.textureMeters=page.layout.textureMeters;d.lodLevels=page.layout.lodLevels;d.lodPixels=page.layout.lodPixels;d.heights=page.heights;d.physicalSurface=page.physicalSurface;d.cellSurfaces=page.cellSurfaces;
    auto appearance=std::make_shared<Material>(*material);
    if(!page.weights.empty()) {
        require(appearance->layers.enabled,"Terrain page weights require layered material");auto texture=std::make_shared<Texture>();texture->name="Paged terrain weights";texture->width=width;texture->height=height;
        texture->pixels=page.weights;texture->wrapU=texture->wrapV=TextureWrap::Clamp;appearance->layers.weights=texture;
        require(appearance->uvScale.x>0&&appearance->uvScale.y>0,"Paged terrain requires positive material UV scale");
        auto& layers=appearance->layers;layers.weightScale={d.textureMeters/(d.spacing.x*width*appearance->uvScale.x),d.textureMeters/(d.spacing.y*height*appearance->uvScale.y)};
        layers.weightOffset={(.5f-patch.offsetX)/width-appearance->uvOffset.x*layers.weightScale.x,(.5f-patch.offsetZ)/height-appearance->uvOffset.y*layers.weightScale.y};
    }
    d.material=appearance;return d;
}
TerrainPage sourcePage(const TerrainPageResult& result) {
    TerrainPage p;p.layout=result.layout;p.page=result.page;p.revision=result.revision;p.generation=result.generation;p.heights=result.asset->data().heights;p.physicalSurface=result.asset->data().physicalSurface;p.cellSurfaces=result.asset->data().cellSurfaces;
    if(const auto& weights=result.asset->data().material->layers.weights) p.weights=weights->pixels;
    return p;
}
}
std::shared_ptr<const TerrainPageResult> generateTerrainPage(const TerrainPage& page,std::shared_ptr<const Material> material,bool collision) {
    auto d=pageData(page,std::move(material));auto result=std::make_shared<TerrainPageResult>();result->layout=page.layout;result->page=page.page;result->revision=page.revision;result->generation=page.generation;
    result->asset=TerrainAsset::createPatch(std::move(d),page.layout.region(page.page));result->sourceBytes=page.heights.size()*sizeof(float)+page.weights.size()+physicalSurfaceBytes(page.physicalSurface,page.cellSurfaces);
    if(collision) result->collision=result->asset->chunkCollision(0);
    return result;
}
std::vector<std::byte> buildTerrainPageTransport(std::span<const std::byte> bytes,std::span<const std::byte> material,bool collision) {
    ArchiveLimits limits;limits.maxBytes=limits.maxAllocatedBytes=8*1024*1024;auto library=SceneArchive::loadAssets(material,limits);require(library.materials.size()==1,"Invalid terrain worker material");
    auto result=generateTerrainPage(decodeTerrainPage(bytes),library.materials[0],collision);AssetLibrary output;
    output.terrains={TerrainAsset::createPatch(result->asset->data(),*result->asset->patch(),false)};
    const auto mesh=result->asset->chunks()[0];output.meshes.push_back(mesh);for(const auto& lod:mesh->lods) output.meshes.push_back(lod.mesh);if(result->collision) output.collisionShapes.push_back(result->collision);
    Wire w;w.put(0x42544557u);w.put(2u);w.layout(result->layout);w.put(result->page);w.put(std::uint32_t(result->revision));w.put(std::uint32_t(result->revision>>32));w.put(std::uint32_t(result->generation));w.put(std::uint32_t(result->generation>>32));w.put(std::uint32_t(mesh->lods.size()));
    for(const auto& lod:mesh->lods) w.put(lod.maxPixels);
    auto payload=SceneArchive::saveAssets(output,limits);w.output.insert(w.output.end(),payload.begin(),payload.end());return std::move(w.output);
}
std::shared_ptr<const TerrainPageResult> hydrateTerrainPageTransport(std::span<const std::byte> bytes,std::shared_ptr<const Material> material,bool offThread) {
    require(bytes.size()<=8*1024*1024&&material,"Terrain worker output exceeds limit");Wire w;w.input=bytes;require(w.get()==0x42544557u,"Unsupported terrain build transport");const auto version=w.get();require(version>=1&&version<=2,"Unsupported terrain build version");
    auto result=std::make_shared<TerrainPageResult>();result->layout=w.layout();result->page=w.get();result->revision=w.get();result->revision|=std::uint64_t(w.get())<<32;
    if(version>=2) {result->generation=w.get();result->generation|=std::uint64_t(w.get())<<32;}
    const auto count=w.get();require(count<=3&&result->revision,"Invalid terrain build levels/revision");std::array<float,3> thresholds{};for(std::uint32_t i=0;i<count;++i) thresholds[i]=w.number();
    ArchiveLimits limits;limits.maxBytes=limits.maxAllocatedBytes=8*1024*1024;auto library=SceneArchive::loadAssets(bytes.subspan(w.at),limits);
    require(library.terrains.size()==1&&library.meshes.size()==count+1&&library.collisionShapes.size()<=1,"Invalid terrain build resources");
    const auto& source=library.terrains[0];const auto [width,height]=result->layout.dimensions(result->page);const auto& d=source->data();
    require(source->patch()&&*source->patch()==result->layout.region(result->page)&&d.samplesX==width&&d.samplesZ==height&&d.chunkCells==result->layout.chunkCells&&d.spacing.x==result->layout.spacing.x&&d.spacing.y==result->layout.spacing.y&&d.textureMeters==result->layout.textureMeters&&d.lodLevels==result->layout.lodLevels&&d.lodPixels==result->layout.lodPixels,"Terrain worker source metadata mismatch");
    auto appearance=std::make_shared<Material>(*material);appearance->layers.weights=d.material->layers.weights;appearance->layers.weightScale=d.material->layers.weightScale;appearance->layers.weightOffset=d.material->layers.weightOffset;
    auto mesh=std::make_shared<Mesh>(*library.meshes[0]);mesh->materials={appearance};mesh->lods.clear();
    for(std::uint32_t i=0;i<count;++i) {auto lod=std::make_shared<Mesh>(*library.meshes[i+1]);lod->materials={appearance};mesh->lods.push_back({std::move(lod),thresholds[i]});}
    auto data=d;data.material=appearance;result->asset=TerrainAsset::createPatch(std::move(data),*source->patch(),true,mesh);
    result->sourceBytes=d.heights.size()*sizeof(float)+(appearance->layers.weights ? appearance->layers.weights->pixels.size() : 0)+physicalSurfaceBytes(d.physicalSurface,d.cellSurfaces);result->generatedOffThread=offThread;
    if(!library.collisionShapes.empty()) {
        const auto& shape=library.collisionShapes[0];require(shape->type()==CollisionShapeType::TriangleMesh&&shape->vertices().size()==mesh->vertices.size()&&shape->indices()==mesh->indices,"Invalid terrain worker collider");
        for(std::size_t i=0;i<mesh->vertices.size();++i) {const auto a=shape->vertices()[i],b=mesh->vertices[i].position;require(a.x==b.x&&a.y==b.y&&a.z==b.z,"Terrain worker collider mismatch");}
        auto same=[](const auto& a,const auto& b){return (!a&&!b)||(a&&b&&*a==*b);};require(same(shape->surface(),d.physicalSurface),"Terrain worker physical fallback mismatch");
        const auto& mapping=shape->triangleSurfaces();const auto patch=*source->patch();require(mapping.empty()==d.cellSurfaces.empty(),"Terrain worker physical map mismatch");
        if(!mapping.empty()){require(mapping.size()==std::size_t(patch.cellsX)*patch.cellsZ*2,"Terrain worker physical triangle count mismatch");std::size_t index=0;for(std::uint32_t z=patch.z;z<patch.z+patch.cellsZ;++z)for(std::uint32_t x=patch.x;x<patch.x+patch.cellsX;++x){const auto& cell=d.cellSurfaces[std::size_t(z)*(d.samplesX-1)+x];require(same(mapping[index++],cell)&&same(mapping[index++],cell),"Terrain worker physical cell mismatch");}}
        result->collision=shape;
    }
    return result;
}
void TerrainPagingSettings::validate(const TerrainPageLayout& layout) const {
    layout.validate();residency.validate();require(maxInFlight>=1&&maxInFlight<=8&&maxSourceBytes>=layout.maxPageBytes()&&maxSourceBytes<=64*1024*1024,"Invalid terrain page IO/source budget");
}
void TerrainPageEditingSettings::validate() const {require(maxWriteBytes>=256*1024&&maxWriteBytes<=64*1024*1024,"Invalid terrain write batch budget");}
void TerrainPageWrite::validate() const {
    require(token&&!pages.empty()&&pages.size()<=256&&pages.size()==expectedGenerations.size(),"Invalid terrain write batch");std::set<std::uint32_t> seen;
    for(std::size_t i=0;i<pages.size();++i) {const auto& p=pages[i];p.validate();require(seen.insert(p.page).second&&p.layout==pages[0].layout&&p.revision==pages[0].revision&&p.generation>expectedGenerations[i],"Terrain write generation/layout mismatch");}
}
std::vector<std::byte> encodeTerrainPageWrite(const TerrainPageWrite& batch) {
    batch.validate();Wire w;w.put(0x57544557u);w.put(1u);w.put(std::uint32_t(batch.token));w.put(std::uint32_t(batch.token>>32));w.put(std::uint32_t(batch.pages.size()));
    for(std::size_t i=0;i<batch.pages.size();++i) {auto bytes=encodeTerrainPage(batch.pages[i]);require(w.output.size()+12+bytes.size()+4<=64*1024*1024,"Terrain write batch exceeds limit");w.put(std::uint32_t(batch.expectedGenerations[i]));w.put(std::uint32_t(batch.expectedGenerations[i]>>32));w.put(std::uint32_t(bytes.size()));w.output.insert(w.output.end(),bytes.begin(),bytes.end());}
    w.put(crc(w.output));return std::move(w.output);
}
TerrainPageWrite decodeTerrainPageWrite(std::span<const std::byte> bytes) {
    require(bytes.size()>=24&&bytes.size()<=64*1024*1024,"Invalid terrain write transport size");Wire w;w.input=bytes;require(w.get()==0x57544557u&&w.get()==1,"Unsupported terrain write transport");TerrainPageWrite batch;
    batch.token=w.get();batch.token|=std::uint64_t(w.get())<<32;const auto count=w.get();require(count&&count<=256,"Invalid terrain write count");
    for(std::uint32_t i=0;i<count;++i) {std::uint64_t expected=w.get();expected|=std::uint64_t(w.get())<<32;const auto size=w.get();require(size<=256*1024&&w.at+std::uint64_t(size)+4<=bytes.size(),"Invalid terrain write page size");batch.pages.push_back(decodeTerrainPage(bytes.subspan(w.at,size)));w.at+=size;batch.expectedGenerations.push_back(expected);}
    require(w.at+4==bytes.size()&&w.get()==crc(bytes.first(bytes.size()-4)),"Terrain write checksum/trailing data mismatch");batch.validate();return batch;
}
struct TerrainPagedStream::Impl {
    Scene& scene;TerrainPageLayout layout;std::uint64_t revision;TerrainPageLoader loader;TerrainPagingSettings settings;TerrainPagingStats stats;
    struct Resident {ActorId actor;std::shared_ptr<const TerrainPageResult> result;std::uint64_t savedGeneration=0;};
    struct Job {std::unique_ptr<AssetManager> manager;AssetRequest request;};
    std::map<std::uint32_t,Resident> resident;std::map<std::uint32_t,Job> jobs;std::map<std::uint32_t,std::string> failures;
    TerrainPageWriter writer;TerrainPageEditingSettings editing;
    struct Stamp {TerrainBrush brush;std::vector<std::uint32_t> required;};
    std::optional<Stamp> stamp;std::optional<Vec2> focus;
    std::optional<Job> write;std::shared_ptr<const TerrainPageWrite> batch;
    bool flushRequested=false;std::uint64_t nextWrite=0;
    Impl(Scene& scene,TerrainPageLayout layout,std::uint64_t revision,TerrainPageLoader loader,TerrainPagingSettings settings)
        :scene(scene),layout(layout),revision(revision),loader(std::move(loader)),settings(settings) {}
    bool dirty(const Resident& r) const {return r.savedGeneration!=r.result->generation;}
    void measure() {
        stats.resident=resident.size();stats.inFlight=jobs.size();stats.failed=failures.size();stats.sourceBytes=stats.geometryBytes=0;
        stats.dirty=stats.dirtyBytes=0;stats.editPending=stamp.has_value();stats.editing=bool(writer);stats.writePending=write.has_value();stats.writing=batch ? batch->pages.size() : 0;
        for(const auto& [page,r]:resident) {stats.sourceBytes+=r.result->sourceBytes;stats.geometryBytes+=geometryBytes(*r.result->asset->chunks()[0]);if(dirty(r)){++stats.dirty;stats.dirtyBytes+=r.result->sourceBytes;}}
        if(failures.empty()) stats.error.clear();else stats.error=failures.begin()->second;
    }
};
TerrainPagedStream::TerrainPagedStream(Scene& scene,TerrainPageLayout layout,std::uint64_t revision,TerrainPageLoader loader,TerrainPagingSettings settings) {
    settings.validate(layout);require(revision&&bool(loader),"Missing terrain page revision/loader");impl_=std::make_unique<Impl>(scene,layout,revision,std::move(loader),settings);
}
TerrainPagedStream::~TerrainPagedStream() {try {clear(true);} catch(...) {}}
bool TerrainPagedStream::hasUnsavedChanges() const {const auto& p=*impl_;return p.write.has_value()||std::any_of(p.resident.begin(),p.resident.end(),[&](const auto& entry){return p.dirty(entry.second);});}
void TerrainPagedStream::clear(bool discardDirty) {
    auto& p=*impl_;p.scene.requireIdle();
    require(discardDirty||!hasUnsavedChanges(),"Terrain pages have unsaved edits; finish/retry writeback or explicitly discard");
    p.stats.cancelled+=p.jobs.size();p.jobs.clear();
    p.write.reset();p.batch.reset();p.stamp.reset();p.flushRequested=false;p.stats.writeFailed=false;p.stats.writeError.clear();p.stats.editError.clear();
    for(auto& [page,r]:p.resident) {if(auto* actor=p.scene.findActor(r.actor)) actor->destroy();++p.stats.evicted;}
    p.resident.clear();p.failures.clear();p.scene.flushPendingDestroy();p.stats.queued=0;p.measure();
}
void TerrainPagedStream::retryFailed() {impl_->failures.clear();impl_->measure();}
void TerrainPagedStream::enableEditing(TerrainPageWriter writer,TerrainPageEditingSettings settings) {
    auto& p=*impl_;p.scene.requireIdle();settings.validate();require(bool(writer)&&!p.writer,"Terrain editing requires a writer and can only be enabled once");p.writer=std::move(writer);p.editing=settings;p.measure();
}
void TerrainPagedStream::cancelEdit() {auto& p=*impl_;p.scene.requireIdle();p.stamp.reset();p.measure();}
void TerrainPagedStream::flush() {auto& p=*impl_;p.scene.requireIdle();require(bool(p.writer),"Terrain page writer is not configured");p.flushRequested=true;p.stats.writeFailed=false;p.stats.writeError.clear();}
bool TerrainPagedStream::edit(const TerrainBrush& brush) {
    auto& p=*impl_;p.scene.requireIdle();require(bool(p.writer),"Enable paged terrain editing before sculpting or painting");brush.validate();require(!p.stamp,"Previous paged terrain stamp is still loading");
    const auto& d=p.layout;const double width=double(d.samplesX-1)*d.spacing.x,depth=double(d.samplesZ-1)*d.spacing.y;
    if(brush.strength==0||double(brush.center.x)+brush.radius<0||double(brush.center.y)+brush.radius<0||double(brush.center.x)-brush.radius>width||double(brush.center.y)-brush.radius>depth) return false;
    const auto left=int(std::floor(std::clamp((double(brush.center.x)-brush.radius)/d.spacing.x,0.0,double(d.samplesX-1)))),right=int(std::ceil(std::clamp((double(brush.center.x)+brush.radius)/d.spacing.x,0.0,double(d.samplesX-1))));
    const auto top=int(std::floor(std::clamp((double(brush.center.y)-brush.radius)/d.spacing.y,0.0,double(d.samplesZ-1)))),bottom=int(std::ceil(std::clamp((double(brush.center.y)+brush.radius)/d.spacing.y,0.0,double(d.samplesZ-1))));
    const int cells=int(d.chunkCells),columns=int((d.samplesX-2)/d.chunkCells+1),rows=int((d.samplesZ-2)/d.chunkCells+1);
    Impl::Stamp stamp{brush,{}};std::set<std::uint32_t> pinned;
    for(const auto& [page,r]:p.resident) if(p.dirty(r)) pinned.insert(page);
    for(int z=std::max(0,top-3)/cells;z<=std::min(rows-1,(bottom+2)/cells);++z) for(int x=std::max(0,left-3)/cells;x<=std::min(columns-1,(right+2)/cells);++x) {
        const auto id=std::uint32_t(z*columns+x);stamp.required.push_back(id);pinned.insert(id);require(pinned.size()<=p.settings.residency.maxResident,"Brush and pinned dirty pages exceed resident page limit; reduce radius or save first");
    }
    std::size_t bytes=0,writeBytes=24;for(auto id:pinned) {const auto [w,h]=d.dimensions(id);const auto physical=p.resident.contains(id)?physicalSurfaceBytes(p.resident.at(id).result->asset->data().physicalSurface,p.resident.at(id).result->asset->data().cellSurfaces):0;bytes+=std::size_t(w)*h*8+physical;writeBytes+=std::size_t(w)*h*8+92+physical;}
    require(bytes<=p.settings.maxSourceBytes,"Brush and pinned dirty pages exceed source budget; reduce radius or save first");
    require(writeBytes<=p.editing.maxWriteBytes,"Brush and dirty pages exceed write batch budget");
    p.stamp=std::move(stamp);p.stats.editError.clear();p.measure();return true;
}
void TerrainPagedStream::applyEdit() {
    auto& p=*impl_;if(!p.stamp) return;
    for(auto id:p.stamp->required) {
        if(p.failures.contains(id)) {p.stats.editError="Brush source page failed: "+p.failures.at(id);p.stamp.reset();return;}
        if(!p.resident.contains(id)) return;
    }
    try {
        const auto& brush=p.stamp->brush;const auto& layout=p.layout;
        auto sample=[&](std::uint32_t x,std::uint32_t z) -> std::pair<const TerrainData*,std::size_t> {
            for(auto id:p.stamp->required) {const auto& data=p.resident.at(id).result->asset->data();const auto region=layout.region(id);
                if(x>=region.offsetX&&z>=region.offsetZ&&x<region.offsetX+data.samplesX&&z<region.offsetZ+data.samplesZ) return {&data,std::size_t(z-region.offsetZ)*data.samplesX+x-region.offsetX};}
            throw std::invalid_argument("Brush source halo is unavailable");
        };
        std::map<std::uint64_t,float> heights;std::map<std::uint64_t,std::array<std::uint8_t,4>> weights;
        // Evaluate global samples once from the frozen pre-stamp source. Every copy uses
        // the same value, including halos on pages whose visible interior is untouched.
        const auto left=std::uint32_t(std::floor(std::clamp((double(brush.center.x)-brush.radius)/layout.spacing.x,0.0,double(layout.samplesX-1)))),right=std::uint32_t(std::ceil(std::clamp((double(brush.center.x)+brush.radius)/layout.spacing.x,0.0,double(layout.samplesX-1))));
        const auto top=std::uint32_t(std::floor(std::clamp((double(brush.center.y)-brush.radius)/layout.spacing.y,0.0,double(layout.samplesZ-1)))),bottom=std::uint32_t(std::ceil(std::clamp((double(brush.center.y)+brush.radius)/layout.spacing.y,0.0,double(layout.samplesZ-1))));
        for(auto z=top;z<=bottom;++z) for(auto x=left;x<=right;++x) {
            const double dx=double(float(x)*layout.spacing.x)-brush.center.x,dz=double(float(z)*layout.spacing.y)-brush.center.y;
            const float t=float(std::max(0.0,1-std::sqrt(dx*dx+dz*dz)/brush.radius)),alpha=t*t*(3-2*t);if(alpha==0) continue;
            const auto [data,index]=sample(x,z);const auto key=std::uint64_t(z)*layout.samplesX+x;
            if(brush.mode==TerrainBrushMode::Paint) {
                require(data->material->layers.enabled&&data->material->uvScale.x>0&&data->material->uvScale.y>0,"Paged painting requires layers and positive material UV scale");
                std::array<std::uint8_t,4> old{255,0,0,0};if(const auto& map=data->material->layers.weights) std::copy_n(map->pixels.begin()+index*4,4,old.begin());
                const float total=old[0]+old[1]+old[2]+old[3],amount=alpha*brush.strength;std::array<int,4> next{};int sum=0;
                for(std::uint32_t k=0;k<4;++k) {const float normalized=total>0 ? old[k]/total : (k==0 ? 1.f : 0.f);next[k]=int(std::round((normalized*(1-amount)+(k==brush.layer ? amount : 0))*255));sum+=next[k];}
                *std::max_element(next.begin(),next.end())+=255-sum;std::array<std::uint8_t,4> value{};for(int k=0;k<4;++k) value[k]=std::uint8_t(next[k]);if(value!=old) weights.emplace(key,value);
            } else {
                const float old=data->heights[index];float value;
                if(brush.mode==TerrainBrushMode::Raise) value=old+brush.strength*alpha;
                else {
                    float target=brush.targetHeight;
                    if(brush.mode==TerrainBrushMode::Smooth) {double sum=0;unsigned count=0;
                        for(int oz=-1;oz<=1;++oz) for(int ox=-1;ox<=1;++ox) {const auto sx=int(x)+ox,sz=int(z)+oz;if(sx>=0&&sz>=0&&sx<int(layout.samplesX)&&sz<int(layout.samplesZ)) {const auto [neighbor,at]=sample(std::uint32_t(sx),std::uint32_t(sz));sum+=neighbor->heights[at];++count;}}
                        target=float(sum/count);
                    }
                    value=old+(target-old)*brush.strength*alpha;
                }
                require(std::isfinite(value)&&std::abs(value)<=1000000,"Terrain brush exceeds height range");if(value!=old) heights.emplace(key,value);
            }
        }
        struct Next {std::uint32_t id;std::shared_ptr<const TerrainPageResult> result;TerrainComponent* component;ColliderComponent* collider;std::vector<std::optional<MaterialInstance>> overrides;};
        std::vector<Next> candidates;
        for(auto id:p.stamp->required) {
            const auto& old=p.resident.at(id).result;auto page=sourcePage(*old);const auto region=layout.region(id);const auto [width,height]=layout.dimensions(id);bool changed=false;
            if(!weights.empty()&&page.weights.empty()) {page.weights.resize(page.heights.size()*4);for(std::size_t i=0;i<page.weights.size();i+=4) page.weights[i]=255;}
            for(std::uint32_t z=0;z<height;++z) for(std::uint32_t x=0;x<width;++x) {const auto key=std::uint64_t(z+region.offsetZ)*layout.samplesX+x+region.offsetX;const auto at=std::size_t(z)*width+x;
                if(auto value=heights.find(key);value!=heights.end()) {page.heights[at]=value->second;changed=true;}
                if(auto value=weights.find(key);value!=weights.end()) {std::copy(value->second.begin(),value->second.end(),page.weights.begin()+at*4);changed=true;}
            }
            if(!changed) continue;
            require(page.generation<std::numeric_limits<std::uint64_t>::max(),"Terrain page generation exhausted");++page.generation;
            auto material=std::make_shared<Material>(*old->asset->data().material);material->layers.weights.reset();std::shared_ptr<const TerrainPageResult> result;
            if(!heights.empty()) result=generateTerrainPage(page,material,p.settings.residency.collision);
            else {
                auto data=pageData(page,material);data.validate();auto asset=std::shared_ptr<TerrainAsset>(new TerrainAsset(std::move(data),false));asset->patch_=region;asset->chunks_=old->asset->chunks();
                auto painted=std::make_shared<TerrainPageResult>(*old);painted->asset=std::move(asset);painted->generation=page.generation;painted->sourceBytes=page.heights.size()*4+page.weights.size()+physicalSurfaceBytes(page.physicalSurface,page.cellSurfaces);painted->generatedOffThread=false;result=std::move(painted);
            }
            auto* actor=p.scene.findActor(p.resident.at(id).actor);require(actor!=nullptr,"Terrain page actor was removed during editing");auto* component=actor->getComponent<TerrainComponent>();require(component&&component->asset()==old->asset,"Terrain page component was replaced during editing");component->validate();
            auto overrides=component->materialOverrides;if(overrides.empty()) overrides.resize(1);if(!overrides[0]) overrides[0]=MaterialInstance{};overrides[0]->layers=result->asset->data().material->layers;
            auto* collider=actor->getComponent<ColliderComponent>();require(!p.settings.residency.collision||collider,"Terrain page collider was removed during editing");
            candidates.push_back({id,std::move(result),component,collider,std::move(overrides)});
        }
        std::size_t totalSource=0,totalWrite=24;
        for(const auto& [id,r]:p.resident){const auto replacement=std::find_if(candidates.begin(),candidates.end(),[&](const auto& n){return n.id==id;});const auto& result=replacement==candidates.end()?r.result:replacement->result;totalSource+=result->sourceBytes;if(replacement!=candidates.end()||p.dirty(r))totalWrite+=12+encodeTerrainPage(sourcePage(*result)).size();}
        require(totalSource<=p.settings.maxSourceBytes,"Edited physical terrain exceeds source budget");require(totalWrite<=p.editing.maxWriteBytes,"Edited physical terrain exceeds write budget");
        // All allocations and validation precede a single idle publication boundary.
        for(const auto& next:candidates) for(const auto& [id,r]:p.resident) {
            const auto replacement=std::find_if(candidates.begin(),candidates.end(),[&](const auto& v){return v.id==id;});overlaps(*next.result->asset,*(replacement==candidates.end() ? r.result : replacement->result)->asset);
        }
        for(auto& next:candidates) {
            next.component->materialOverrides=std::move(next.overrides);next.component->publish(next.result->asset,next.result->asset->chunks()[0],true);
            if(next.collider&&next.result->collision) next.collider->setShape(next.result->collision);
            p.resident.at(next.id).result=std::move(next.result);
        }
        p.stats.lastAffected=candidates.size();p.stats.lastRebuilt=heights.empty() ? 0 : candidates.size();if(!candidates.empty()) ++p.stats.edits;p.stamp.reset();p.scene.synchronize();
    } catch(const std::exception& e) {p.stats.editError=std::string(e.what()).substr(0,1024);p.stamp.reset();}
}
void TerrainPagedStream::serviceWrites() {
    auto& p=*impl_;if(!p.writer) return;
    if(p.write) {
        p.write->manager->update();const auto state=p.write->request.progress();
        if(state.status==AssetStatus::Ready) {
            const auto ack=p.write->request.get<TerrainPageWriteAck>();
            if(!ack||ack->token!=p.batch->token) {p.stats.writeFailed=true;p.stats.writeError="Stale terrain write acknowledgement";}
            else {for(const auto& page:p.batch->pages) if(auto r=p.resident.find(page.page);r!=p.resident.end()) r->second.savedGeneration=page.generation;++p.stats.writes;p.stats.writtenPages+=p.batch->pages.size();}
            p.write.reset();p.batch.reset();
        } else if(state.status==AssetStatus::Failed||state.status==AssetStatus::Cancelled) {p.stats.writeFailed=true;p.stats.writeError=state.error.substr(0,1024);p.write.reset();p.batch.reset();}
    }
    if(p.write||p.stats.writeFailed||(!p.editing.autoWriteback&&!p.flushRequested)) return;
    auto batch=std::make_shared<TerrainPageWrite>();
    for(const auto& [id,r]:p.resident) if(p.dirty(r)) {batch->pages.push_back(sourcePage(*r.result));batch->expectedGenerations.push_back(r.savedGeneration);}
    p.flushRequested=false;if(batch->pages.empty()) return;
    require(p.nextWrite<std::numeric_limits<std::uint64_t>::max(),"Terrain write token exhausted");batch->token=++p.nextWrite;
    try {
        require(encodeTerrainPageWrite(*batch).size()<=p.editing.maxWriteBytes,"Terrain write batch exceeds configured budget");auto manager=std::make_unique<AssetManager>();const AssetId id("terrain-write");manager->registerAsset<TerrainPageWriteAck>(id,p.writer(batch));auto request=manager->load<TerrainPageWriteAck>(id);
        p.batch=std::move(batch);p.write=Impl::Job{std::move(manager),std::move(request)};p.write->manager->update();
    } catch(const std::exception& e) {p.stats.writeFailed=true;p.stats.writeError=std::string(e.what()).substr(0,1024);p.write.reset();p.batch.reset();}
}
const TerrainPagingStats& TerrainPagedStream::stats() const {return impl_->stats;}
const TerrainPageLayout& TerrainPagedStream::layout() const {return impl_->layout;}
const TerrainPagingSettings& TerrainPagedStream::settings() const {return impl_->settings;}
std::optional<TerrainGround> TerrainPagedStream::ground(float x,float z) const {
    require(std::isfinite(x)&&std::isfinite(z),"Invalid paged ground coordinate");
    for(const auto& [page,r]:impl_->resident) if(auto found=r.result->asset->ground(x,z)) return found;
    return {};
}
void TerrainPagedStream::update(Vec2 focus) {
    auto& p=*impl_;p.scene.requireIdle();require(std::isfinite(focus.x)&&std::isfinite(focus.y),"Invalid terrain page focus");
    if(p.stamp&&p.focus&&(p.focus->x!=focus.x||p.focus->y!=focus.y)) {p.stamp.reset();p.stats.editError="Brush cancelled because paging focus changed";}
    p.focus=focus;serviceWrites();
    const auto& d=p.layout;const auto columns=(d.samplesX-2)/d.chunkCells+1,rows=(d.samplesZ-2)/d.chunkCells+1;const auto& s=p.settings.residency;
    const int cx=int(std::clamp(std::floor(double(focus.x)/(d.chunkCells*d.spacing.x)),0.0,double(columns-1))),cz=int(std::clamp(std::floor(double(focus.y)/(d.chunkCells*d.spacing.y)),0.0,double(rows-1)));
    std::vector<std::pair<double,std::uint32_t>> candidates;
    for(int z=std::max(0,cz-int(s.radiusChunks));z<=std::min(int(rows)-1,cz+int(s.radiusChunks));++z) for(int x=std::max(0,cx-int(s.radiusChunks));x<=std::min(int(columns)-1,cx+int(s.radiusChunks));++x) {
        const double dx=(x+.5)*d.chunkCells*d.spacing.x-focus.x,dz=(z+.5)*d.chunkCells*d.spacing.y-focus.y;candidates.emplace_back(dx*dx+dz*dz,std::uint32_t(z)*columns+x);
    }
    std::sort(candidates.begin(),candidates.end());std::vector<std::uint32_t> wanted;std::set<std::uint32_t> desired;std::size_t bytes=0;
    auto pin=[&](std::uint32_t page) {if(desired.insert(page).second) {const auto [width,height]=d.dimensions(page);bytes+=p.resident.contains(page)?p.resident.at(page).result->sourceBytes:std::size_t(width)*height*8;wanted.push_back(page);}};
    for(const auto& [page,r]:p.resident) if(p.dirty(r)) pin(page);
    if(p.stamp) for(auto page:p.stamp->required) pin(page);
    for(auto [distance,page]:candidates) {
        if(wanted.size()>=s.maxResident) break;
        if(desired.contains(page)) continue;
        const auto [width,height]=d.dimensions(page);const auto cost=p.resident.contains(page)?p.resident.at(page).result->sourceBytes:std::size_t(width)*height*8;
        if(bytes+cost<=p.settings.maxSourceBytes) {bytes+=cost;wanted.push_back(page);desired.insert(page);}
    }
    for(auto it=p.jobs.begin();it!=p.jobs.end();) {if(!desired.contains(it->first)) {it=p.jobs.erase(it);++p.stats.cancelled;}else ++it;}
    for(auto it=p.failures.begin();it!=p.failures.end();) {if(!desired.contains(it->first)) it=p.failures.erase(it);else ++it;}
    for(auto it=p.resident.begin();it!=p.resident.end();) {
        if(p.dirty(it->second)&&!p.scene.findActor(it->second.actor)) {
            auto actor=makeTerrainActor("Paged terrain "+std::to_string(it->first),it->second.result->asset,false);actor->transform.position=s.origin;
            if(s.collision) {actor->addComponent<ColliderComponent>("Page collision",it->second.result->collision);RigidBodySettings body;body.motion=BodyMotion::Static;body.friction=.8f;actor->addComponent<RigidBodyComponent>("Page body",body);}
            auto* raw=actor.get();p.scene.adoptChild(std::move(actor));it->second.actor=raw->id();
        }
        if(!desired.contains(it->first)||!p.scene.findActor(it->second.actor)) {if(auto* actor=p.scene.findActor(it->second.actor)) actor->destroy();it=p.resident.erase(it);++p.stats.evicted;}else ++it;
    }
    p.scene.flushPendingDestroy();std::size_t admitted=0;
    for(auto it=p.jobs.begin();it!=p.jobs.end();) {
        auto& job=it->second;job.manager->update();const auto state=job.request.progress();
        if(state.status==AssetStatus::Failed||state.status==AssetStatus::Cancelled) {p.failures[it->first]=state.error.substr(0,1024);it=p.jobs.erase(it);continue;}
        if(state.status!=AssetStatus::Ready||admitted>=s.buildsPerUpdate) {++it;continue;}
        try {
            const auto result=job.request.get<TerrainPageResult>();require(result&&result->layout==d&&result->page==it->first&&result->revision==p.revision&&result->asset&&result->asset->patch()&&*result->asset->patch()==d.region(it->first),"Stale/mismatched terrain page result");
            const auto& data=result->asset->data();const auto [width,height]=d.dimensions(it->first);require(data.samplesX==width&&data.samplesZ==height&&data.chunkCells==d.chunkCells&&data.spacing.x==d.spacing.x&&data.spacing.y==d.spacing.y&&data.textureMeters==d.textureMeters&&data.lodLevels==d.lodLevels&&data.lodPixels==d.lodPixels&&result->asset->eager(),"Terrain page asset layout mismatch");
            if(const auto& weights=data.material->layers.weights) require(weights->width==width&&weights->height==height&&weights->pixels.size()==std::size_t(width)*height*4,"Terrain page weight dimensions mismatch");
            const auto sourceBytes=data.heights.size()*sizeof(float)+(data.material->layers.weights ? data.material->layers.weights->pixels.size() : 0)+physicalSurfaceBytes(data.physicalSurface,data.cellSurfaces);
            require(sourceBytes==result->sourceBytes&&sourceBytes<=d.maxPageBytes()+std::size_t(width-1)*(height-1)*sizeof(std::shared_ptr<const PhysicalSurface>)+65536&&(!s.collision||result->collision),"Terrain page source/collision budget mismatch");
            if(s.collision) {
                const auto& mesh=*result->asset->chunks()[0];const auto& shape=*result->collision;
                require(shape.type()==CollisionShapeType::TriangleMesh&&shape.vertices().size()==mesh.vertices.size()&&shape.indices()==mesh.indices,"Terrain page collider topology mismatch");
                for(std::size_t v=0;v<mesh.vertices.size();++v) {const auto a=shape.vertices()[v],b=mesh.vertices[v].position;require(a.x==b.x&&a.y==b.y&&a.z==b.z,"Terrain page collider position mismatch");}
            }
            std::size_t residentBytes=0;for(const auto& [page,r]:p.resident)residentBytes+=r.result->sourceBytes;require(residentBytes+sourceBytes<=p.settings.maxSourceBytes,"Physical terrain page exceeds remaining source budget");
            for(const auto& [page,r]:p.resident) overlaps(*result->asset,*r.result->asset);
            auto actor=makeTerrainActor("Paged terrain "+std::to_string(it->first),result->asset,false);actor->transform.position=s.origin;
            if(s.collision) {actor->addComponent<ColliderComponent>("Page collision",result->collision);RigidBodySettings body;body.motion=BodyMotion::Static;body.friction=.8f;actor->addComponent<RigidBodyComponent>("Page body",body);}
            auto* raw=actor.get();p.scene.adoptChild(std::move(actor));p.resident.emplace(it->first,Impl::Resident{raw->id(),result,result->generation});++admitted;++p.stats.completed;if(result->generatedOffThread) ++p.stats.workerCompleted;
        } catch(const std::exception& e) {p.failures[it->first]=std::string(e.what()).substr(0,1024);}
        it=p.jobs.erase(it);
    }
    for(auto page:wanted) {
        if(p.jobs.size()>=p.settings.maxInFlight) break;
        if(p.resident.contains(page)||p.jobs.contains(page)||p.failures.contains(page)) continue;
        try {auto manager=std::make_unique<AssetManager>();const AssetId id("terrain-page");manager->registerAsset<TerrainPageResult>(id,p.loader(page));auto request=manager->load<TerrainPageResult>(id);p.jobs.emplace(page,Impl::Job{std::move(manager),std::move(request)});++p.stats.started;p.jobs.at(page).manager->update();}
        catch(const std::exception& e) {p.failures[page]=std::string(e.what()).substr(0,1024);}
    }
    p.stats.queued=0;for(auto page:wanted) if(!p.resident.contains(page)&&!p.jobs.contains(page)&&!p.failures.contains(page)) ++p.stats.queued;
    applyEdit();serviceWrites();p.scene.synchronize();p.measure();
}
} // namespace engine
