#include "terrain_demo.hpp"
#include "engine/physics.hpp"
#include "engine/terrain_browser.hpp"

using namespace engine;
namespace {
std::shared_ptr<const TerrainAsset> hills() {
    TerrainData d;d.samplesX=d.samplesZ=97;d.spacing={.5f,.5f};d.chunkCells=24;d.textureMeters=3;
    for(unsigned z=0;z<97;++z) for(unsigned x=0;x<97;++x) {
        const float px=float(x)*.5f-24,pz=float(z)*.5f-24;
        d.heights.push_back(.01f*(px*px+pz*pz)+3*std::exp(-((px-10)*(px-10)+(pz+7)*(pz+7))/30)+
            2*std::exp(-((px+11)*(px+11)+(pz-6)*(pz-6))/24));
    }
    auto texture=std::make_shared<Texture>();texture->name="Terrain grass tile";texture->width=texture->height=64;texture->pixels.resize(64*64*4);
    for(unsigned z=0;z<64;++z) for(unsigned x=0;x<64;++x) {
        const unsigned i=(z*64+x)*4,noise=(x*13+z*29+x*z*3)%18;const bool line=x<2||z<2;
        texture->pixels[i]=std::uint8_t((line ? 68 : 88)+noise);texture->pixels[i+1]=std::uint8_t((line ? 94 : 127)+noise);texture->pixels[i+2]=std::uint8_t((line ? 34 : 48)+noise);texture->pixels[i+3]=255;
    }
    auto material=std::make_shared<Material>();material->name="Grass, sand, rock and soil";material->roughness=1;material->baseColorTexture=texture;
    material->layers.enabled=true;material->layers.layers[0].texture=texture;
    const std::array<Vec3,3> colors={Vec3{.72f,.55f,.29f},Vec3{.43f,.46f,.5f},Vec3{.27f,.12f,.055f}};
    for(std::size_t layer=1;layer<4;++layer) {
        auto tile=std::make_shared<Texture>();tile->name=layer==1 ? "Terrain sand tile" : layer==2 ? "Terrain rock tile" : "Terrain soil tile";
        tile->width=tile->height=64;tile->pixels.resize(64*64*4);
        for(unsigned z=0;z<64;++z) for(unsigned x=0;x<64;++x) {
            const auto i=(z*64+x)*4;const float variation=.8f+.2f*float((x*17+z*31+x*z*7)%23)/22;
            tile->pixels[i]=std::uint8_t(colors[layer-1].x*variation*255);tile->pixels[i+1]=std::uint8_t(colors[layer-1].y*variation*255);tile->pixels[i+2]=std::uint8_t(colors[layer-1].z*variation*255);tile->pixels[i+3]=255;
        }
        material->layers.layers[layer].texture=tile;material->layers.layers[layer].uvScale={.6f+float(layer)*.35f,.6f+float(layer)*.35f};
        material->layers.layers[layer].roughness=layer==2 ? .65f : .95f;
    }
    // Periodic procedural relief tiles: packed tangent normals + linear height.
    for(std::size_t layer=0;layer<4;++layer) {
        auto relief=std::make_shared<Texture>();relief->name="Terrain normal-height "+std::to_string(layer);
        relief->width=relief->height=64;relief->pixels.resize(64*64*4);
        for(unsigned z=0;z<64;++z) for(unsigned x=0;x<64;++x) {
            const float u=float(x)*6.2831853f/64,v=float(z)*6.2831853f/64,f=float(layer+1);
            const float height=.5f+.45f*std::sin(u*f)*std::cos(v*f);
            const auto n=normalized(Vec3{-.65f*std::cos(u*f)*std::cos(v*f),.65f*std::sin(u*f)*std::sin(v*f),1});
            const auto i=(z*64+x)*4;relief->pixels[i]=std::uint8_t((n.x*.5f+.5f)*255);relief->pixels[i+1]=std::uint8_t((n.y*.5f+.5f)*255);relief->pixels[i+2]=std::uint8_t((n.z*.5f+.5f)*255);relief->pixels[i+3]=std::uint8_t(height*255);
        }
        material->layers.layers[layer].normalHeightTexture=relief;
    }
    material->layers.heightBlend=.25f;material->layers.triplanar=true;material->layers.triplanarScale=1.f/3;
    auto weights=std::make_shared<Texture>();weights->name="Terrain blend weights (linear data)";weights->width=weights->height=97;weights->wrapU=weights->wrapV=TextureWrap::Clamp;weights->pixels.resize(97*97*4);
    for(unsigned z=0;z<97;++z) for(unsigned x=0;x<97;++x) {
        const float px=float(x)*.5f-24,pz=float(z)*.5f-24,h=d.heights[z*97+x];
        const float soil=std::clamp(1.4f-std::abs(pz-3*std::sin(px*.18f)),0.f,1.f);
        const float rock=std::clamp((h-4)/4,0.f,1.f),sand=std::clamp(1-std::abs(h-2.5f)/2,0.f,1.f)*(1-soil);
        const std::array<float,4> blend={(1-rock)*(1-soil)*(1-sand),sand*(1-rock),rock*(1-soil),soil};
        for(std::size_t k=0;k<4;++k) weights->pixels[(z*97+x)*4+k]=std::uint8_t(std::clamp(blend[k]*255,0.f,255.f));
    }
    material->layers.weights=weights;material->layers.weightScale={3.f/48,3.f/48};d.material=material;
    return TerrainAsset::create(std::move(d));
}
}
void TerrainDemo::start(Scene& scene,OrbitController& orbit,CameraComponent& camera) {
    if(active_) return;
    const auto asset=hills(); // Validate and prepare before changing the current presentation.
    camera_=camera.id();previousProjection_=camera.projectionMode();previousFov_=camera.fieldOfView;previousNear_=camera.nearPlane;previousFar_=camera.farPlane;previousHeight_=camera.orthographicHeight();
    previousNodes_.clear();previousOrbit_=orbit.snapshot();previousSky_=scene.sky();previousAmbient_=scene.ambient;previousEnvironmentIntensity_=scene.environmentIntensity;
    for(const auto& node:scene.children()) {previousNodes_.emplace_back(node->savedId(),node->enabled);node->enabled=dynamic_cast<CameraComponent*>(node.get())!=nullptr;}
    auto& decor=scene.spawn("Terrain demo lighting");decor_=decor.id();
    auto& sun=decor.addComponent<LightComponent>("Terrain sun",LightType::Directional);sun.direction={-.6f,-1,-.3f};sun.intensity=2.5f;sun.shadow.enabled=true;sun.shadow.halfExtent=30;sun.shadow.halfDepth=50;
    scene.ambient={.22f,.25f,.3f};scene.environmentIntensity=0;
    SkyBackground sky;sky.horizon={.7f,.82f,.94f};sky.zenith={.12f,.32f,.6f};scene.setSky(sky);
    active_=true;replace(scene,orbit,asset);drop(scene);
}
void TerrainDemo::replace(Scene& scene,OrbitController& orbit,std::shared_ptr<const TerrainAsset> asset) {
    if(!active_) throw std::invalid_argument("Open terrain demo before loading a heightmap");
    requireSavedPages();
    auto candidate=makeTerrainActor("Terrain ground",asset,true);
    paged_.reset();stream_.reset();asset_=asset;undo_.clear();redo_.clear();affected_=rebuilt_=0;
    const auto& d=asset->data();const float sx=(d.samplesX-1)*d.spacing.x,sz=(d.samplesZ-1)*d.spacing.y;
    candidate->transform.position={-sx*.5f,0,-sz*.5f};auto* next=candidate.get();scene.adoptChild(std::move(candidate));
    if(auto* old=scene.findActor(ground_)) old->destroy();ground_=next->id();
    for(auto id:balls_) if(auto* ball=scene.findActor(id)) ball->destroy();balls_.clear();
    if(auto* marker=scene.findActor(marker_)) marker->destroy();marker_={};scene.flushPendingDestroy();scene.synchronize();
    frame(scene,orbit);++revision_;
}
void TerrainDemo::frame(Scene& scene,OrbitController& orbit) {
    if(auto* camera=dynamic_cast<CameraComponent*>(scene.findComponent(camera_))) camera->setPerspective(pi/3,.1f,500);
    if(paged_) {const auto origin=paged_->settings().residency.origin;const Vec3 target{focus_.x+origin.x,2,focus_.y+origin.z};orbit.restore({target,target,.65f,.85f,48});orbit.orbit(0,0);return;}
    if(!asset_) return;
    const auto& d=asset_->data();const float sx=(d.samplesX-1)*d.spacing.x,sz=(d.samplesZ-1)*d.spacing.y;
    const float center=asset_->ground(sx*.5f,sz*.5f)->position.y;
    orbit.restore({{0,center,0},{0,center,0},.65f,.65f,std::max(12.f,std::max(sx,sz)*1.25f)});orbit.orbit(0,0);
}
void TerrainDemo::stop(Scene& scene,OrbitController& orbit) {
    if(!active_) return;
    requireSavedPages();
    paged_.reset();stream_.reset();asset_.reset();undo_.clear();redo_.clear();
    for(auto id:{ground_,decor_,marker_}) if(auto* actor=scene.findActor(id)) actor->destroy();
    for(auto id:balls_) if(auto* actor=scene.findActor(id)) actor->destroy();balls_.clear();scene.flushPendingDestroy();
    for(const auto& [id,enabled]:previousNodes_) if(auto* node=scene.findSavedNode(id)) node->enabled=enabled;
    previousNodes_.clear();scene.setSky(previousSky_);scene.ambient=previousAmbient_;scene.environmentIntensity=previousEnvironmentIntensity_;orbit.restore(previousOrbit_);orbit.orbit(0,0);
    if(auto* camera=dynamic_cast<CameraComponent*>(scene.findComponent(camera_))) {
        if(previousProjection_==ProjectionMode::Orthographic) camera->setOrthographic(previousHeight_,previousNear_,previousFar_);
        else camera->setPerspective(previousFov_,previousNear_,previousFar_);
    }
    active_=false;++revision_;
}
void TerrainDemo::drop(Scene& scene,float x,float z) {
    if(!active_||balls_.size()>=24) throw std::invalid_argument("Open terrain demo; maximum 24 balls");
    auto hit=terrainGround(scene,x,z);if(!hit) throw std::invalid_argument("Choose a point inside resident terrain");
    auto candidate=std::make_unique<Actor>("Terrain ball");candidate->transform.position=hit->position+Vec3{0,5,0};
    auto& visual=candidate->addComponent<MeshComponent>("Collision ball",Mesh::sphere(24,16));visual.color={1,.3f,.055f};visual.transform.scale={1.1f,1.1f,1.1f};
    candidate->addComponent<ColliderComponent>("Ball collider",CollisionShape::sphere(.55f));RigidBodySettings body;body.friction=.8f;body.restitution=.12f;
    candidate->addComponent<RigidBodyComponent>("Ball body",body);auto* ball=candidate.get();scene.adoptChild(std::move(candidate));balls_.push_back(ball->id());scene.synchronize();
}
void TerrainDemo::mark(Scene& scene,const TerrainHit& hit) {
    if(!active_) return;auto* actor=scene.findActor(marker_);
    if(!actor) {actor=&scene.spawn("Terrain query marker");marker_=actor->id();auto& mesh=actor->addComponent<MeshComponent>("Ground query point",Mesh::sphere(16,12));mesh.color={1,.85f,.05f};mesh.transform.scale={.45f,.45f,.45f};MaterialInstance m;m.shader=MaterialShader::Unlit;mesh.setMaterial(0,m);}
    actor->transform.position=hit.position+hit.normal*.25f;
}
void TerrainDemo::apply(Scene& scene,const TerrainEdit& edit) {
    if(stream_) stream_->apply(edit);
    else {auto* actor=scene.findActor(ground_);if(!actor) throw std::invalid_argument("Terrain ground is unavailable");applyTerrainEdit(*actor,edit);scene.synchronize();}
    if(edit.heightsChanged) for(auto id:balls_) if(auto* actor=scene.findActor(id))
        if(auto* body=actor->getComponent<RigidBodyComponent>();body&&body->hasBody()) body->setLinearVelocity(body->linearVelocity());
    asset_=edit.after;affected_=edit.affectedChunks.size();rebuilt_=stream_ ? stream_->stats().lastRebuilt : edit.heightsChanged ? affected_ : 0;++revision_;
}
bool TerrainDemo::edit(Scene& scene,TerrainBrush brush) {
    if(active_&&paged_) {
        if(paged_->stats().editPending) return false;
        const auto origin=paged_->settings().residency.origin;brush.center.x-=origin.x;brush.center.y-=origin.z;return paged_->edit(brush);
    }
    if(!active_||!asset_) throw std::invalid_argument("Open terrain before editing");
    const auto& d=asset_->data();brush.center.x+=(d.samplesX-1)*d.spacing.x*.5f;brush.center.y+=(d.samplesZ-1)*d.spacing.y*.5f;
    auto edit=asset_->edit(brush);if(!edit.changed()) return false;
    // Prepare history allocation before publishing an edit. Demo history is bounded.
    undo_.reserve(17);apply(scene,edit);undo_.push_back(std::move(edit));if(undo_.size()>16) undo_.erase(undo_.begin());redo_.clear();return true;
}
bool TerrainDemo::history(Scene& scene,bool redo) {
    auto& from=redo ? redo_ : undo_;auto& to=redo ? undo_ : redo_;if(from.empty()) return false;
    auto edit=from.back();to.reserve(17);if(!redo) std::swap(edit.before,edit.after);
    apply(scene,edit);to.push_back(from.back());from.pop_back();return true;
}
void TerrainDemo::streaming(Scene& scene,OrbitController& orbit,bool enabled,TerrainStreamSettings settings,Vec2 worldFocus) {
    if(!active_||!asset_) throw std::invalid_argument("Open terrain before streaming");settings.validate();
    if(!enabled) {if(stream_) replace(scene,orbit,TerrainAsset::create(asset_->data()));return;}
    auto source=TerrainAsset::create(asset_->data(),false);const auto& d=source->data();
    settings.origin={-float(d.samplesX-1)*d.spacing.x*.5f,0,-float(d.samplesZ-1)*d.spacing.y*.5f};
    const Vec2 local{worldFocus.x-settings.origin.x,worldFocus.y-settings.origin.z};
    auto next=std::make_unique<TerrainStream>(scene,source,settings);next->update(local);
    stream_.reset();if(auto* old=scene.findActor(ground_)) old->destroy();ground_={};
    for(auto id:balls_) if(auto* ball=scene.findActor(id)) ball->destroy();balls_.clear();
    stream_=std::move(next);asset_=std::move(source);focus_=local;undo_.clear();redo_.clear();++revision_;scene.flushPendingDestroy();scene.synchronize();
}
void TerrainDemo::streamFocus(Vec2 worldFocus) {
    if(!stream_&&!paged_) throw std::invalid_argument("Enable terrain streaming first");
    if(!std::isfinite(worldFocus.x)||!std::isfinite(worldFocus.y)) throw std::invalid_argument("Invalid streaming focus");
    const auto origin=paged_ ? paged_->settings().residency.origin : stream_->settings().origin;
    focus_={worldFocus.x-origin.x,worldFocus.y-origin.z};
}
bool TerrainDemo::update(Scene& scene) {
    // Editor commands publish immutable source versions through the retained actor.
    // On returning to the demo, discard its now-stale private brush history.
    if(!paged_&&!stream_)if(auto* actor=scene.findActor(ground_))if(auto* chunk=actor->getComponent<TerrainComponent>();chunk&&chunk->asset()!=asset_){
        asset_=chunk->asset();undo_.clear();redo_.clear();++revision_;return true;
    }
    if(paged_) {
        const auto completed=paged_->stats().completed,edits=paged_->stats().edits;paged_->update(focus_);
        if(paged_->stats().edits!=edits) {affected_=paged_->stats().lastAffected;rebuilt_=paged_->stats().lastRebuilt;++revision_;
            if(rebuilt_) for(auto id:balls_) if(auto* actor=scene.findActor(id)) if(auto* body=actor->getComponent<RigidBodyComponent>();body&&body->hasBody()) body->setLinearVelocity(body->linearVelocity());
        }
        return paged_->stats().completed!=completed||paged_->stats().edits!=edits;
    }
    if(!stream_) return false;const auto built=stream_->stats().built;stream_->update(focus_);return stream_->stats().built!=built;
}
void TerrainDemo::paging(Scene& scene,OrbitController& orbit,bool enabled,TerrainPagingSettings settings,Vec2 worldFocus) {
    if(!active_) throw std::invalid_argument("Open terrain before paging");
    requireSavedPages();
    if(!enabled) {if(paged_) replace(scene,orbit,hills());return;}
    if(!std::isfinite(worldFocus.x)||!std::isfinite(worldFocus.y)) throw std::invalid_argument("Invalid paging focus");
    TerrainPageLayout layout;layout.samplesX=layout.samplesZ=1025;layout.spacing={.5f,.5f};layout.textureMeters=3;
    settings.residency.origin={-256,0,-256};settings.residency.collision=true;settings.validate(layout);
    // The tiny material template owns four tiles; it retains no world-sized source or mask.
    auto templateSource=asset_ ? asset_ : hills();auto material=std::make_shared<Material>(*templateSource->data().material);material->layers.weights.reset();
    BrowserTerrainOptions options;options.collision=true;options.storageNamespace="terrain-demo-1025-v1";
    auto loader=[layout,material,options](std::uint32_t page) {return browserTerrainPageLoader("assets/terrain/"+std::to_string(page)+".weterrain",layout,page,1,material,options);};
    auto next=std::make_unique<TerrainPagedStream>(scene,layout,1,std::move(loader),settings);
    next->enableEditing(browserTerrainPageWriter(options.storageNamespace));
    const Vec2 local{worldFocus.x+256,worldFocus.y+256};next->update(local);
    paged_.reset();stream_.reset();if(auto* old=scene.findActor(ground_)) old->destroy();ground_={};
    for(auto id:balls_) if(auto* ball=scene.findActor(id)) ball->destroy();balls_.clear();
    if(auto* marker=scene.findActor(marker_)) marker->destroy();marker_={};
    asset_.reset();undo_.clear();redo_.clear();affected_=rebuilt_=0;focus_=local;paged_=std::move(next);
    scene.flushPendingDestroy();scene.synchronize();frame(scene,orbit);++revision_;
}
void TerrainDemo::requireSavedPages() const {if(paged_&&paged_->hasUnsavedChanges()) throw std::invalid_argument("Terrain page edits are not saved yet. Wait for writeback, retry it, or discard unsaved page edits.");}
