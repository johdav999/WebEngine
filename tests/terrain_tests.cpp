#include "engine/terrain.hpp"
#include "engine/scene_archive.hpp"
#include "engine/physics.hpp"
#include "engine/render_frame.hpp"
#include "engine/camera_component.hpp"
#include <iostream>
#include <limits>
#include <map>

using namespace engine;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void near(float a,float b,float tolerance=.0002f) { check(std::abs(a-b)<tolerance,"Terrain numeric mismatch"); }
template<class F> void rejects(F&& f) { bool threw=false;try {f();} catch(const std::invalid_argument&) {threw=true;}check(threw,"Invalid terrain input accepted"); }
TerrainData grid(std::uint32_t nx=5,std::uint32_t nz=4) {
    TerrainData d;d.samplesX=nx;d.samplesZ=nz;d.chunkCells=2;d.spacing={2,3};d.heights.resize(nx*nz);
    for(std::uint32_t z=0;z<nz;++z) for(std::uint32_t x=0;x<nx;++x) d.heights[z*nx+x]=float(x)*.2f+float(z)*.3f;
    return d;
}
void editingAndStreaming() {
    // Appearance edits share immutable geometry and work for both resident and source-only owners.
    auto appearanceSource=TerrainAsset::create(grid());auto layers=appearanceSource->data().material->layers;
    check(!appearanceSource->withLayers(layers).changed(),"Identical layers produced an edit");
    layers.enabled=true;layers.layers[2].texture=Texture::white();auto appearance=appearanceSource->withLayers(layers);
    check(appearance.changed()&&appearance.paintChanged&&!appearance.heightsChanged&&appearance.affectedChunks.empty(),"Layer replacement flags are invalid");
    check(appearance.after->chunks()==appearanceSource->chunks()&&appearance.after->data().heights==appearanceSource->data().heights,"Appearance edit rebuilt geometry or changed heights");
    auto sourceOnly=TerrainAsset::create(grid(),false);check(!sourceOnly->withLayers(layers).after->eager(),"Appearance edit eagerly built a source-only asset");
    auto invalidLayers=layers;invalidLayers.layers[0].texture=std::make_shared<Texture>();rejects([&]{appearanceSource->withLayers(invalidLayers);});
    // Surface appearance stays independent of authoritative geometry and survives codecs.
    auto detailed=appearanceSource->data().material->layers;detailed.enabled=true;
    detailed.layers[0].normalHeightTexture=Texture::flatNormal();detailed.layers[0].normalScale=2;
    detailed.layers[0].heightBias=-.2f;detailed.heightBlend=.25f;detailed.triplanar=true;detailed.triplanarScale=.3f;
    auto surfaceEdit=appearanceSource->withLayers(detailed);
    check(surfaceEdit.changed()&&surfaceEdit.after->chunks()[0]==appearanceSource->chunks()[0],"Surface replacement rebuilt geometry");
    auto inactive=detailed;inactive.triplanar=false;
    auto configured=appearanceSource->withLayers(inactive).after;inactive.triplanarScale=.7f;
    check(configured->withLayers(inactive).changed(),"Inactive projection configuration was lost");
    auto replacement=detailed;replacement.layers[0].normalHeightTexture=Texture::white();
    check(surfaceEdit.after->withLayers(replacement).changed(),"Normal map-only replacement ignored");
    auto invalid=detailed;invalid.heightBlend=-1;rejects([&]{appearanceSource->withLayers(invalid);});
    invalid=detailed;invalid.triplanarSharpness=17;rejects([&]{appearanceSource->withLayers(invalid);});
    invalid=detailed;invalid.layers[1].normalScale=std::numeric_limits<float>::quiet_NaN();rejects([&]{appearanceSource->withLayers(invalid);});
    Scene detailedScene;detailedScene.adoptChild(makeTerrainActor("Detailed",surfaceEdit.after));
    const auto detailedCopy=SceneArchive::load(SceneArchive::save(detailedScene,{},SceneArchive::collectAssets(detailedScene)));
    const auto restoredLayers=detailedCopy.assets.terrains[0]->data().material->layers;
    check(restoredLayers.triplanar&&restoredLayers.heightBlend==.25f&&restoredLayers.layers[0].normalHeightTexture&&restoredLayers.layers[0].normalScale==2,"Surface archive lost parameters/maps");
    const auto& surfaceMaterial=*surfaceEdit.after->data().material;
    check(!sameMaterialBinding(surfaceMaterial,*appearanceSource->data().material),"Surface parameters ignored by binding identity");
    check(materialTextureSemantics(surfaceMaterial)[5]==TextureSemantic::Normal,"Packed relief map needs normal mips and linear alpha");
    auto packed=std::make_shared<Texture>();packed->width=2;packed->height=1;packed->pixels={128,128,255,0,128,128,255,255};
    check(packed->mipChain(TextureSemantic::Normal).back().pixels[3]==128,"Normal mip filtering must retain linear height");
    auto d=grid(65,65);d.chunkCells=16;d.spacing={1,1};d.textureMeters=4;
    std::fill(d.heights.begin(),d.heights.end(),0);
    auto material=std::make_shared<Material>();material->layers.enabled=true;d.material=material;
    auto source=TerrainAsset::create(d);TerrainBrush brush;brush.center={8,8};brush.radius=2;brush.strength=2;
    auto edit=source->edit(brush);check(edit.changed()&&edit.affectedChunks==std::vector<std::uint32_t>{0},"Localized sculpt rebuilt unrelated chunks");
    near(source->ground(8,8)->position.y,0);near(edit.after->ground(8,8)->position.y,2);
    for(unsigned i=1;i<source->chunkCount();++i) check(source->chunks()[i]==edit.after->chunks()[i],"Unchanged geometry lost sharing");
    auto full=TerrainAsset::create(edit.after->data());
    for(unsigned i=0;i<source->chunkCount();++i) {
        const auto& a=edit.after->chunks()[i];const auto& b=full->chunks()[i];check(a->vertices.size()==b->vertices.size(),"Partial rebuild vertex count");
        for(std::size_t v=0;v<a->vertices.size();++v) {near(a->vertices[v].position.y,b->vertices[v].position.y);near(dot(a->vertices[v].normal,b->vertices[v].normal),1);}
    }
    Scene scene;auto actor=makeTerrainActor("Editable ground",source,true);auto* instance=actor.get();scene.adoptChild(std::move(actor));scene.synchronize();
    const auto id=instance->getComponent<TerrainComponent>()->id();auto shape=instance->getComponent<ColliderComponent>()->shape();
    applyTerrainEdit(*instance,edit);scene.synchronize();check(instance->getComponent<TerrainComponent>()->id()==id,"Editing invalidated component handles");
    near(terrainGround(scene,8,8)->position.y,2);near(scene.physics().raycast({8,20,8},{0,-1,0},30)->point.y,2);
    check(instance->getComponent<ColliderComponent>()->shape()!=shape,"Sculpt left stale collision");rejects([&]{applyTerrainEdit(*instance,edit);});
    brush.center={16,16};brush.radius=1.5f;brush.strength=1;auto seam=edit.after->edit(brush);check(seam.affectedChunks==std::vector<std::uint32_t>({0,1,4,5}),"Shared-corner normal halo missing");
    auto regenerated=TerrainAsset::create(seam.after->data());
    for(auto i:seam.affectedChunks) {
        const auto& a=seam.after->chunks()[i];const auto& b=regenerated->chunks()[i];
        for(std::size_t v=0;v<a->vertices.size();++v) {near(a->vertices[v].position.y,b->vertices[v].position.y);near(dot(a->vertices[v].normal,b->vertices[v].normal),1);}
        for(std::size_t l=0;l<a->lods.size();++l) for(std::size_t v=0;v<a->lods[l].mesh->vertices.size();++v) {
            const auto& p=a->lods[l].mesh->vertices[v];const auto& q=b->lods[l].mesh->vertices[v];near(p.position.y,q.position.y);near(dot(p.normal,q.normal),1);
        }
    }
    brush.center={8,8};brush.radius=3;brush.mode=TerrainBrushMode::Flatten;brush.targetHeight=5;brush.strength=.5f;
    auto flat=edit.after->edit(brush);near(flat.after->ground(8,8)->position.y,3.5f);
    brush.mode=TerrainBrushMode::Smooth;brush.strength=1;auto smooth=edit.after->edit(brush);check(smooth.after->ground(8,8)->position.y<2,"Smooth did not average frozen source neighbors");
    brush.mode=TerrainBrushMode::Paint;brush.layer=3;brush.strength=1;auto paint=edit.after->edit(brush);check(paint.paintChanged&&!paint.heightsChanged,"Paint modified height data");
    check(!edit.after->data().material->layers.weights,"Painting mutated published material");
    const auto& weights=*paint.after->data().material->layers.weights;check(weights.pixels[(8*65+8)*4+3]==255,"Paint did not select requested layer");
    for(std::size_t i=0;i<weights.pixels.size();i+=4) check(int(weights.pixels[i])+weights.pixels[i+1]+weights.pixels[i+2]+weights.pixels[i+3]==255,"Paint weights not normalized");
    for(unsigned i=0;i<source->chunkCount();++i) check(paint.after->chunks()[i]==edit.after->chunks()[i],"Paint rebuilt geometry");
    auto mappedMaterial=std::make_shared<Material>(*material);mappedMaterial->uvScale={2,.5f};mappedMaterial->uvOffset={.1f,.2f};auto mappedData=d;mappedData.material=mappedMaterial;
    const auto mappedPaint=TerrainAsset::create(mappedData)->edit(brush);check(mappedPaint.after->data().material->layers.weights->pixels[(8*65+8)*4+3]==255,"Painting ignored global material UV transform");
    auto wide=grid(4097,2);wide.spacing={1,1};wide.chunkCells=16;wide.material=material;
    auto edgeBrush=brush;edgeBrush.center={8,0};auto edgePaint=TerrainAsset::create(wide,false)->edit(edgeBrush);
    check(edgePaint.changed()&&edgePaint.after->data().material->layers.weights->width==4096,"Valid 4097-sample terrain exceeded the weight texture limit");
    auto beforePaint=instance->getComponent<ColliderComponent>()->shape();applyTerrainEdit(*instance,paint);scene.synchronize();check(instance->getComponent<ColliderComponent>()->shape()==beforePaint,"Paint rebuilt collision");
    for(auto* part:instance->getComponents<TerrainComponent>()) check(part->resolvedMaterial(0).layers.weights==paint.after->data().material->layers.weights,"Paint binding stale on shared geometry");
    auto restored=SceneArchive::load(SceneArchive::save(scene));near(terrainGround(*restored.scene,8,8)->position.y,2);
    brush.radius=std::numeric_limits<float>::quiet_NaN();rejects([&]{source->edit(brush);});brush.radius=2;brush.layer=4;rejects([&]{source->edit(brush);});brush.layer=0;brush.strength=-1;rejects([&]{source->edit(brush);});
    brush.strength=0;check(!source->edit(brush).changed(),"Zero strength publishes a source");brush.strength=1;brush.center={-100,-100};check(!source->edit(brush).changed(),"Outside brush publishes a source");
    auto streamed=TerrainAsset::create(d,false);check(streamed->chunks().empty()&&streamed->chunkCount()==16,"Source-only terrain retained generated chunks");
    AssetLibrary sourceLibrary;sourceLibrary.terrains={streamed};ArchiveLimits sourceBudget;sourceBudget.maxAllocatedBytes=100000;
    auto sourceCopy=SceneArchive::loadAssets(SceneArchive::saveAssets(sourceLibrary),sourceBudget);check(sourceCopy.terrains[0]->chunks().empty(),"Library source-only load generated geometry");
    sourceLibrary.terrains={source};rejects([&]{SceneArchive::loadAssets(SceneArchive::saveAssets(sourceLibrary),sourceBudget);});
    Scene streamScene;TerrainStreamSettings settings;settings.radiusChunks=1;settings.maxResident=4;settings.buildsPerUpdate=1;settings.collision=true;
    TerrainStream stream(streamScene,streamed,settings);stream.update({8,8});check(stream.stats().resident==1&&stream.stats().pending==3&&stream.stats().built==1,"Streaming admission budget ignored");
    for(int i=0;i<4;++i) stream.update({8,8});check(stream.stats().resident==4&&stream.stats().pending==0,"Streaming residency did not settle");
    check(!terrainGround(streamScene,56,56)&&bool(streamed->ground(56,56)),"Unloaded terrain still resident or source query missing");
    std::weak_ptr<const Mesh> released;ComponentId evicted;
    streamScene.visit([&](const Node& n,const Mat4&){if(auto* t=dynamic_cast<const TerrainComponent*>(&n);t&&t->chunk()==0) {released=t->mesh;evicted=t->id();}});
    auto cachedBytes=stream.stats().geometryBytes;
    for(int i=0;i<4;++i) stream.update({56,56});check(stream.stats().resident==4&&stream.stats().geometryBytes==cachedBytes,"Streaming memory grew with traversal");
    check(released.expired()&&!streamScene.findComponent(evicted),"Eviction retained CPU geometry or live handles");
    check(!streamScene.physics().raycast({8,20,8},{0,-1,0},30),"Eviction retained solid ground");
    brush.mode=TerrainBrushMode::Raise;brush.center={8,8};brush.strength=3;brush.radius=2;
    auto unloaded=stream.asset()->edit(brush);stream.apply(unloaded);check(stream.stats().lastRebuilt==0,"Editing unloaded chunks generated geometry");
    for(int i=0;i<4;++i) stream.update({8,8});near(terrainGround(streamScene,8,8)->position.y,3);near(streamScene.physics().raycast({8,20,8},{0,-1,0},30)->point.y,3);
    auto saved=SceneArchive::save(streamScene);auto loaded=SceneArchive::load(saved);auto resources=SceneArchive::collectAssets(*loaded.scene);
    rejects([&]{SceneArchive::load(saved,sourceBudget);});
    check(resources.terrains.size()==1&&!resources.terrains[0]->eager()&&resources.terrains[0]->chunks().empty(),"Stream archive eagerly hydrated unloaded chunks");
    near(terrainGround(*loaded.scene,8,8)->position.y,3);check(!terrainGround(*loaded.scene,56,56),"Stream archive changed resident region");
    brush.center={8,8};brush.strength=1;auto residentEdit=stream.asset()->edit(brush);stream.apply(residentEdit);check(stream.stats().lastRebuilt==1,"Resident partial rebuild ignored chunk bounds");near(streamScene.physics().raycast({8,20,8},{0,-1,0},30)->point.y,4);
    rejects([&]{stream.update({std::numeric_limits<float>::infinity(),0});});auto state=stream.stats();check(state.resident==4,"Invalid focus evicted valid terrain");
    for(int i=0;i<12;++i) {Vec2 focus=i%2 ? Vec2{8,8} : Vec2{56,56};for(int j=0;j<4;++j) stream.update(focus);check(stream.stats().resident<=4&&stream.stats().geometryBytes==cachedBytes,"Repeated streaming leaked generated geometry");}
    stream.clear();check(stream.stats().resident==0&&!terrainGround(streamScene,8,8),"Stream clear retained actors");
    rejects([&]{TerrainStream invalid(streamScene,source,settings);});settings.maxResident=0;rejects([&]{TerrainStream invalid(streamScene,streamed,settings);});
}
void detailAndLayers() {
    auto d=grid(51,42);d.chunkCells=16;d.spacing={.3f,.7f};
    for(std::uint32_t z=0;z<d.samplesZ;++z) for(std::uint32_t x=0;x<d.samplesX;++x) d.heights[z*d.samplesX+x]=std::sin(float(x)*.4f)*std::cos(float(z)*.3f);
    auto source=TerrainAsset::create(d);check(source->chunks().front()->lods.size()==3,"Terrain LOD chain missing");
    std::map<std::pair<float,float>,Vertex> borders;
    for(const auto& fine:source->chunks()) {
        const auto bounds=fine->localBounds();std::size_t previous=fine->indices.size();
        auto border=[&](const Vertex& v) {return v.position.x==bounds.minimum.x||v.position.x==bounds.maximum.x||v.position.z==bounds.minimum.z||v.position.z==bounds.maximum.z;};
        std::map<std::pair<float,float>,Vertex> expected;
        for(const auto& v:fine->vertices) if(border(v)) {expected[{v.position.x,v.position.z}]=v;borders[{v.position.x,v.position.z}]=v;}
        for(const auto& level:fine->lods) {
            const auto& coarse=*level.mesh;check(coarse.indices.size()<previous,"Terrain LOD failed to reduce triangles");previous=coarse.indices.size();
            std::map<std::pair<float,float>,Vertex> actual;
            for(const auto& v:coarse.vertices) if(border(v)) actual[{v.position.x,v.position.z}]=v;
            check(expected.size()==actual.size(),"Terrain LOD dropped perimeter vertices");
            for(const auto& [key,v]:expected) {check(actual.contains(key),"Terrain LOD edge moved");const auto& other=actual.at(key);near(v.position.y,other.position.y);near(dot(v.normal,other.normal),1);near(v.uv.x,other.uv.x);near(v.uv.y,other.uv.y);}
            double area=0;
            for(std::size_t i=0;i<coarse.indices.size();i+=3) {
                const auto a=coarse.vertices.at(coarse.indices[i]).position,b=coarse.vertices.at(coarse.indices[i+1]).position,c=coarse.vertices.at(coarse.indices[i+2]).position;
                const auto triangle=cross(b-a,c-a).y*.5f;check(triangle>0,"Degenerate/reversed terrain LOD fan");area+=triangle;
            }
            check(std::abs(area-double(bounds.maximum.x-bounds.minimum.x)*(bounds.maximum.z-bounds.minimum.z))<.0005,"Terrain LOD has incomplete coverage");
        }
    }
    Scene scene;auto owned=makeTerrainActor("LOD terrain",source,true);auto* actor=owned.get();scene.adoptChild(std::move(owned));scene.synchronize();
    const auto shape=actor->getComponent<ColliderComponent>()->shape();const auto before=terrainGround(scene,3,4)->position.y;
    auto& camera=scene.addChild<CameraComponent>();camera.transform.position={12,30,35};camera.lookAt({7,0,12});
    const DetailView view{camera.viewProjection(1),1500,1500};RenderFrame frame;
    auto policy=[&](int a,int b) {for(auto* chunk:actor->getComponents<TerrainComponent>()) {chunk->lodPolicy.cameraLevel=a;chunk->lodPolicy.shadowLevel=b;}};
    auto colorTriangles=[&] {std::size_t count=0;for(const auto& draw:frame.sceneFrame().meshes) count+=draw.indexCount/3;return count;};
    auto shadowTriangles=[&] {std::size_t count=0;for(const auto& draw:frame.shadowBatches(0)) count+=draw.indexCount/3*draw.instances.instanceCount;return count;};
    policy(0,3);frame.collect(scene,false,0xffffffffu,nullptr,&view);const auto full=colorTriangles();frame.prepareShadow(0,view,true,false);
    check(shadowTriangles()<full&&frame.cameraLodParts()==0&&frame.shadowLodParts()>0,"Independent coarse shadow LOD failed");
    policy(3,0);frame.collect(scene,false,0xffffffffu,nullptr,&view);frame.prepareShadow(0,view,true,false);
    check(colorTriangles()<full&&shadowTriangles()==full,"Camera LOD contaminated shadow detail");
    frame.collect(scene,false);check(colorTriangles()==full,"Disabling global LOD did not preserve full geometry");
    policy(-1,-1);const DetailView small{view.viewProjection,32,32};frame.collect(scene,false,0xffffffffu,nullptr,&small);check(colorTriangles()<full,"Automatic terrain LOD missing");
    check(shape==actor->getComponent<ColliderComponent>()->shape(),"LOD replaced terrain collision");near(terrainGround(scene,3,4)->position.y,before);
    auto invalid=d;invalid.lodLevels=4;rejects([&]{TerrainAsset::create(invalid);});invalid=d;invalid.lodPixels[1]=invalid.lodPixels[0];rejects([&]{TerrainAsset::create(invalid);});
    auto weight=std::make_shared<Texture>();weight->width=weight->height=1;weight->pixels={17,33,51,99};weight->wrapU=weight->wrapV=TextureWrap::Clamp;
    auto material=std::make_shared<Material>();material->layers.enabled=true;material->layers.weights=weight;
    for(auto& layer:material->layers.layers) layer.texture=Texture::white();material->layers.layers[1].uvScale={3,2};material->layers.layers[2].roughness=.35f;
    material->validate();check(materialTextures(*material)[4]==weight&&materialTextureSemantics(*material)[4]==TextureSemantic::Data,"Weights decoded as color");
    check(materialTextureSemantics(*material)[2]==TextureSemantic::Color,"Third layer decoded as a normal map");
    check(material->parameters().layers.weights==weight,"Metadata-free parameters lost layers");
    auto& first=*actor->getComponents<TerrainComponent>()[0];first.setMaterial(0,MaterialInstance(material));
    auto candidate=material->layers;candidate.layers[0].roughness=-1;MaterialInstance bad;bad.layers=candidate;
    rejects([&]{first.setMaterial(0,bad);});check(first.resolvedMaterial(0).layers.weights==weight,"Failed layer edit replaced valid state");
    auto changed=*material;changed.layers.layers[1].color={.2f,.3f,.4f};check(!sameMaterialBinding(*material,changed),"Different layer colors batched");
    changed=*material;changed.layers.layers[1].uvScale.x=7;check(!sameMaterialBinding(*material,changed),"Different layer UVs batched");
    changed=*material;changed.shader=MaterialShader::Water;rejects([&]{changed.validate();});
    policy(3,0);const auto bytes=SceneArchive::save(scene);auto restored=SceneArchive::load(bytes);
    auto* copy=dynamic_cast<TerrainComponent*>(restored.scene->findSavedNode(first.savedId()));check(copy&&copy->lodPolicy.cameraLevel==3&&copy->lodPolicy.shadowLevel==0,"Terrain detail policy did not persist");
    const auto layers=copy->resolvedMaterial(0).layers;check(layers.enabled&&layers.weights->pixels==weight->pixels&&layers.layers[0].texture==layers.layers[3].texture,"Layer source/texture sharing did not restore");
    check(copy->asset()->chunks()[0]->lods.size()==3,"Terrain LODs did not regenerate");near(layers.layers[2].roughness,.35f);
    ArchiveLimits limits;limits.maxAllocatedBytes=d.derivedBytes()/2;rejects([&]{SceneArchive::load(bytes,limits);});
    auto old=SceneArchive::loadFile(std::filesystem::path(TERRAIN_FIXTURE_DIR)/"terrain_v10.wescene");
    const auto* oldChunk=dynamic_cast<const TerrainComponent*>(old.scene->findSavedNode(first.savedId()));(void)oldChunk;
    near(terrainGround(*old.scene,0,0)->position.y,2+.8f*std::cos(.4f));
    old.scene->visit([&](const Node& node,const Mat4&) {if(const auto* t=dynamic_cast<const TerrainComponent*>(&node)) check(t->asset()->data().lodLevels==0&&!t->resolvedMaterial(0).layers.enabled,"Format-10 defaults changed");});
}
}
int main(int argc,char** argv) {try {
    editingAndStreaming();
    detailAndLayers();
    auto d=grid();auto asset=TerrainAsset::create(d);check(asset->chunks().size()==4,"Partial edge chunks missing");
    near(asset->ground(1,1.5f)->position.y,.25f);near(asset->ground(8,9)->position.y,1.7f);
    check(!asset->ground(-.1f,0)&&!asset->ground(8.1f,0),"Out-of-bounds query hit");
    auto saddle=grid(2,2);saddle.spacing={1,1};saddle.heights={0,0,0,4};auto s=TerrainAsset::create(saddle);
    near(s->ground(.25f,.25f)->position.y,0);near(s->ground(.75f,.75f)->position.y,2); // Triangles, not bilinear interpolation.
    std::map<std::pair<float,float>,Vertex> seams;std::size_t triangles=0;
    for(const auto& chunk:asset->chunks()) {
        triangles+=chunk->indices.size()/3;
        for(const auto& v:chunk->vertices) {
            const auto [it,fresh]=seams.emplace(std::make_pair(v.position.x,v.position.z),v);
            if(!fresh) {near(v.position.y,it->second.position.y);near(dot(v.normal,it->second.normal),1);near(v.uv.x,it->second.uv.x);near(v.uv.y,it->second.uv.y);}
        }
        for(std::size_t i=0;i<chunk->indices.size();i+=3) {
            auto a=chunk->vertices[chunk->indices[i]].position,b=chunk->vertices[chunk->indices[i+1]].position,c=chunk->vertices[chunk->indices[i+2]].position;
            check(cross(b-a,c-a).y>0,"Terrain triangle winding incorrect");
        }
    }
    check(triangles==24,"Terrain cells duplicated or missing");
    Texture image;image.width=image.height=2;image.pixels={0,0,0,255,255,0,0,255,128,0,0,255,64,0,0,255};
    auto imported=TerrainAsset::fromHeightmap(image,-2,6);near(imported->data().heights[0],-2);near(imported->data().heights[1],6);near(imported->data().heights[2],-2+8*128.f/255);
    auto tiny=grid(2,2);tiny.spacing={.0001f,.0001f};tiny.heights={0,0,0,0};near(TerrainAsset::create(tiny)->ground(0,0)->normal.y,1);
    Scene nested;auto& parent=nested.spawn("Parent");parent.transform.position={10,3,4};parent.transform.rotation.y=.3f;
    auto visual=makeTerrainActor("Nested terrain",asset);auto* nestedActor=visual.get();parent.adoptChild(std::move(visual));
    const auto nestedPoint=transformPoint(nestedActor->worldMatrix(),{1,.25f,1.5f});
    near(terrainGround(*nestedActor,nestedPoint.x,nestedPoint.z)->position.y,nestedPoint.y);
    nestedActor->transform.scale={2,3,.5f};nestedActor->transform.rotation.x=.15f;
    const auto scaledPoint=transformPoint(nestedActor->worldMatrix(),{1,.25f,1.5f});
    near(terrainGround(*nestedActor,scaledPoint.x,scaledPoint.z)->position.y,scaledPoint.y);
    check(!terrainRaycast(nested,{0,100,0},{0,1,0},1),"Ray moving away hit terrain");
    auto fractional=grid(8,8);fractional.spacing={.3f,.7f};auto edge=TerrainAsset::create(fractional);
    check(bool(edge->ground(7*.3f,7*.7f)),"Exact fractional-spacing edge rejected");
    rejects([&]{auto bad=d;bad.heights.pop_back();TerrainAsset::create(bad);});
    rejects([&]{auto bad=d;bad.spacing.x=0;TerrainAsset::create(bad);});
    rejects([&]{auto bad=d;bad.chunkCells=0;TerrainAsset::create(bad);});
    rejects([&]{auto bad=d;bad.heights[0]=std::numeric_limits<float>::quiet_NaN();TerrainAsset::create(bad);});
    rejects([&]{auto bad=d;bad.material.reset();TerrainAsset::create(bad);});
    rejects([&]{TerrainAsset::fromHeightmap(image,2,-2);});
    Scene scene;auto instance=makeTerrainActor("Terrain",asset,true);auto* actor=instance.get();actor->transform.position={-4,2,-4};scene.adoptChild(std::move(instance));scene.synchronize();
    auto ground=terrainGround(scene,-3,-2.5f);check(bool(ground),"World ground missing");near(ground->position.y,2.25f);
    auto ray=terrainRaycast(scene,{-3,8,-2.5f},{0,-2,0},10);near(ray->position.y,ground->position.y);near(ray->distance,5.75f);
    auto physics=scene.physics().raycast({-3,8,-2.5f},{0,-1,0},10);check(bool(physics),"Terrain collision ray missing");near(physics->point.y,ground->position.y);near(dot(physics->normal,ground->normal),1);
    actor->transform.rotation.y=.4f;scene.synchronize();scene.fixedUpdate(1.f/60);
    const auto p=transformPoint(actor->worldMatrix(),{1,.25f,1.5f});ground=terrainGround(scene,p.x,p.z);check(bool(ground),"Rotated world query missing");near(ground->position.y,p.y);
    physics=scene.physics().raycast({p.x,8,p.z},{0,-1,0},10);near(physics->point.y,p.y);
    rejects([&]{terrainRaycast(scene,{},{},1);});rejects([&]{terrainGround(scene,std::numeric_limits<float>::infinity(),0);});
    actor->enabled=false;check(!terrainGround(scene,p.x,p.z),"Disabled terrain remains queryable");actor->enabled=true;
    auto components=actor->getComponents<TerrainComponent>();check(components.size()==4,"Terrain component count");
    MaterialInstance override;override.tint={.6f,.8f,.2f};components[0]->setMaterial(0,override);
    auto library=SceneArchive::collectAssets(scene);check(library.terrains.size()==1,"Terrain asset collection loses sharing");
    const auto bytes=SceneArchive::save(scene,{},library);check(bytes[8]==std::byte{34},"Terrain archive version");
    auto restored=SceneArchive::load(bytes);check(SceneArchive::save(*restored.scene,{},restored.assets)==bytes,"Terrain archive not stable");
    auto* copy=dynamic_cast<Actor*>(restored.scene->findSavedNode(actor->savedId()));auto chunks=copy->getComponents<TerrainComponent>();
    check(chunks.size()==4&&chunks[0]->asset()==chunks[3]->asset()&&chunks[0]->asset()==restored.assets.terrains[0],"Restored terrain source sharing lost");
    check(chunks[0]->mesh==chunks[0]->asset()->chunks()[chunks[0]->chunk()],"Terrain mesh not regenerated from source");
    near(terrainGround(*restored.scene,p.x,p.z)->position.y,p.y);near(restored.scene->physics().raycast({p.x,8,p.z},{0,-1,0},10)->point.y,p.y);
    auto assets=SceneArchive::loadAssets(SceneArchive::saveAssets(library));check(assets.terrains.size()==1,"Terrain library missing");near(assets.terrains[0]->ground(1,1.5f)->position.y,.25f);
    ArchiveLimits limits;limits.maxAllocatedBytes=100;rejects([&]{SceneArchive::load(bytes,limits);});limits={};limits.maxElements=5;rejects([&]{SceneArchive::load(bytes,limits);});
    auto corrupt=bytes;corrupt.back()^=std::byte{1};rejects([&]{SceneArchive::load(corrupt);});near(terrainGround(scene,p.x,p.z)->position.y,p.y);
    auto original=components[0]->mesh;components[0]->mesh=Mesh::cube();rejects([&]{SceneArchive::save(scene);});components[0]->mesh=original;
    // Real fixed-step contact, then restoration and continued simulation.
    Scene dynamics;auto flat=grid(9,9);flat.spacing={1,1};std::fill(flat.heights.begin(),flat.heights.end(),0);
    dynamics.adoptChild(makeTerrainActor("Flat terrain",TerrainAsset::create(flat),true));auto& ball=dynamics.spawn("Ball");ball.transform.position={2,4,2};
    ball.addComponent<ColliderComponent>("Sphere",CollisionShape::sphere(.5f));ball.addComponent<RigidBodyComponent>();dynamics.synchronize();
    for(int i=0;i<240;++i) dynamics.fixedUpdate(1.f/60);near(ball.transform.position.y,.5f,.03f);
    auto dynamicCopy=SceneArchive::load(SceneArchive::save(dynamics));auto* ballCopy=dynamicCopy.scene->findSavedNode(ball.savedId());
    for(int i=0;i<60;++i) dynamicCopy.scene->fixedUpdate(1.f/60);near(ballCopy->transform.position.y,.5f,.03f);
    if(argc==2) SceneArchive::saveFile(argv[1],scene);
    actor->destroy();check(!terrainGround(scene,p.x,p.z),"Pending-destroy terrain remains queryable");scene.flushPendingDestroy();
    std::cout<<"Terrain: geometry, seams, import, queries, collision, lifecycle, archive and validation passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}

