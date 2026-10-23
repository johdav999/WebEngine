#include "../examples/deformation_gallery.hpp"
#include "engine/desktop.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
using namespace engine;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv){try{
    check(argc==3,"Usage: windows_animation_smoke dawn|d3d12 evidence-directory");const std::string backend=argv[1];check(backend=="dawn"||backend=="d3d12","Unknown backend");
    const auto directory=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(directory);
    DesktopSettings settings;settings.hidden=true;settings.width=960;settings.height=640;settings.backend=backend=="dawn"?DesktopBackend::Dawn:DesktopBackend::Direct3D12;
    DesktopHost host(settings);auto g=deformation_example::makeGallery(FbxImporter::loadFile(std::filesystem::path(DESKTOP_FIXTURE_DIR)/"skeletal_column_binary.fbx"));
    const auto id=host.application().loadScene(std::move(g.scene));const DesktopView view{g.camera->id()};host.setViews(id,{&view,1});host.application().start(id);host.setPaused(true);
    auto& gui=host.gui().root().add<Button>("");gui.setPosition({12,12});gui.setSize({140,44});gui.style.background={.16f,.32f,.55f,1};
    double time=0;auto step=[&]{check(host.step({},time+=1./60),"Unexpected host close");check(host.diagnostics().error.empty(),host.diagnostics().error.c_str());check(host.captureError().empty(),host.captureError().c_str());};
    auto wait=[&](auto predicate){const auto deadline=host.nowSeconds()+120;while(!predicate()){check(host.nowSeconds()<deadline,"Animation acceptance timeout");step();}};
    wait([&]{return host.diagnostics().ready;});std::ostringstream phases;bool first=true;
    auto capture=[&](const char* name){g.world->synchronize();step();const auto changed=host.diagnostics().performance;for(int i=0;i<2;++i)step();
        host.captureFrame(directory/(std::string(name)+".ppm"));wait([&]{return !host.capturePending();});step();
        if(!first)phases<<',';first=false;phases<<'"'<<name<<"\":{\"firstPerformance\":"<<changed<<",\"renderState\":"<<host.diagnostics().renderState<<",\"performance\":"<<host.diagnostics().performance<<'}';};
    auto reset=[&]{g.reset();host.setQuality(RenderQuality{});host.setShadowsEnabled(true);};
    capture("bind");const auto independent=g.b->skinPose()->revision;
    deformation_example::pose(*g.a,0,.5);capture("left");check(g.b->skinPose()->revision==independent,"Shared mesh changed the other player's pose");
    deformation_example::pose(*g.a,1,.5);capture("right");
    g.a->animation().blend(0,1,.5f,false);g.a->animation().seek(g.asset->skeleton->clips[0].duration*.5);g.a->animation().pause();capture("blend");
    reset();g.a->setMorphWeight(0,.8f);capture("morph-bind");
    deformation_example::pose(*g.a,0,.5);g.a->setMorphWeight(1,.6f);capture("morph-skin");
    g.a->setMorphWeight(0,-.3f);g.a->setMorphWeight(1,1.3f);capture("negative-morph");
    reset();g.morph->setMorphWeight(0,.8f);g.morph->setMorphWeight(1,.6f);capture("morph-only");
    // Identity palette must draw the same morphed stream as an ordinary mesh.
    reset();g.b->enabled=g.morph->enabled=false;g.a->setMorphWeight(0,.8f);g.a->setMorphWeight(1,.6f);capture("identity-skin");
    auto& equivalent=g.world->spawn("Static equivalent").addComponent<MeshComponent>("Static equivalent",g.staticAsset);equivalent.color={1,1,1};
    equivalent.transform=g.a->transform;equivalent.setMaterial(0,MaterialInstance{g.surface});equivalent.setMorphWeight(0,.8f);equivalent.setMorphWeight(1,.6f);g.a->enabled=false;capture("identity-static");
    equivalent.destroy();reset();g.world->synchronize();
    deformation_example::pose(*g.a,0,.5);g.a->setMorphWeight(0,.7f);g.a->setMorphWeight(1,.4f);
    deformation_example::pose(*g.b,1,.75);g.b->setMorphWeight(1,-.4f);capture("independent");capture("unchanged");
    g.a->transform.scale={-g.scale.x*.8f,g.scale.y*1.15f,g.scale.z*.6f};capture("mirrored-scale");
    MaterialInstance normal{g.surface};normal.normalScale=0.f;g.a->setMaterial(0,normal);capture("normal-off");
    MaterialInstance alpha{g.surface};alpha.alphaMode=AlphaMode::Blend;alpha.opacity=.4f;alpha.doubleSided=true;g.a->setMaterial(0,alpha);capture("transparent");
    alpha.alphaMode=AlphaMode::Additive;g.a->setMaterial(0,alpha);g.world->setFog(FogSettings{});capture("additive-fog");
    alpha.alphaMode=AlphaMode::Mask;alpha.baseColorTexture=lighting_example::checker("Animated cutout",0);alpha.opacity=1.f;g.a->setMaterial(0,alpha);capture("masked");
    g.a->castsShadow=false;capture("caster-off");
    reset();deformation_example::pose(*g.a,0,.5);g.a->setMorphWeight(0,.8f);g.a->setMorphWeight(1,.6f);capture("shadowed");host.setShadowsEnabled(false);capture("no-shadows");
    host.setShadowsEnabled(true);auto q=host.quality();q.directionalCascades=3;q.shadowDistance=45;host.setQuality(q);capture("cascades");
    g.sun->intensity=0;capture("spot-shadow");host.setShadowsEnabled(false);capture("spot-no-shadow");g.sun->intensity=2;host.setShadowsEnabled(true);
    // Both kinds of deformation bring an offscreen bind mesh back into the view.
    reset();const auto& cameraMatrix=g.camera->worldMatrix().m;const Vec3 right{cameraMatrix[0],cameraMatrix[1],cameraMatrix[2]};
    const Vec3 shift=right*30;g.a->transform.position=g.aPosition+shift;
    std::vector<JointPose> shifted;for(const auto& joint:g.asset->skeleton->joints){auto local=joint.bindPose;if(joint.parent<0)local.translation=local.translation-shift*(1/g.scale.x);shifted.push_back(local);}
    g.a->skeleton().setPose(shifted);capture("skin-bounds");g.a->frustumCulling=false;capture("skin-bounds-unclipped");g.a->frustumCulling=true;
    reset();auto entering=std::make_shared<Mesh>(*g.asset);MorphTarget enter{"Enter"};enter.positions.resize(entering->vertices.size(),shift*(-1/g.scale.x));entering->morphTargets.push_back(std::move(enter));
    g.a->mesh=entering;g.a->transform.position=g.aPosition+shift;g.a->setMorphWeight(2,1);capture("morph-bounds");g.a->frustumCulling=false;capture("morph-bounds-unclipped");g.a->frustumCulling=true;
    // Rendering must accept arbitrary composed palettes from the shared controller.
    reset();auto controller=std::make_shared<AnimationControllerDefinition>();controller->parameters={{"Speed",AnimationParameterType::Number,0.0},{"Layer",AnimationParameterType::Number,0.0}};
    controller->states={{"Motion",0,true,1,{},{}}};controller->states[0].tree=AnimationBlendTree{0,{},{{0,0,0},{1,1,0}},true};
    AnimationLayer layer;layer.name="Overlay";layer.motion={"Opposite",1,true,1,{},{}};layer.mask.assign(g.asset->skeleton->joints.size(),.75f);layer.weightParameter=1;controller->layers.push_back(layer);
    g.a->animation().setController(controller);g.a->animation().seek(g.asset->skeleton->clips[0].duration*.5);g.a->animation().pause();capture("tree-left");
    g.a->animation().setParameter("Speed",1.0);g.a->animation().seek(g.asset->skeleton->clips[0].duration*.5);capture("tree-right");g.a->animation().setParameter("Speed",0.0);g.a->animation().setParameter("Layer",1.0);g.a->animation().seek(g.asset->skeleton->clips[0].duration*.5);capture("layer");
    reset();deformation_example::pose(*g.a,0,.5);g.a->setMorphWeight(0,.8f);g.a->setMorphWeight(1,.6f);
    q=host.quality();q.post.enabled=true;q.post.bloomStrength=.4f;q.post.exposure=.5f;q.msaaSamples=4;host.setQuality(q);g.world->setSky(SkyBackground{});g.world->setFog(FogSettings{});capture("hdr-msaa-fog");
    host.setWindowSize(803,603);capture("resized");host.setWindowSize(960,640);
    auto& second=g.world->spawn("Second camera").addComponent<CameraComponent>();second.transform.position={-10,8,16};second.lookAt({0,2,0});second.setViewport({.5f,0,.5f,1});g.camera->setViewport({0,0,.5f,1});
    const DesktopView split[]{view,DesktopView{second.id()}};host.setViews(id,split);capture("split");g.camera->setViewport({});host.setViews(id,{&view,1});second.destroy();
    {
        reset();g.b->enabled=g.morph->enabled=false;
        auto rig=std::make_shared<Skeleton>();for(int i=0;i<4;++i)rig->joints.push_back({"Root "+std::to_string(i),-1,{}});
        rig->bindings.resize(1024);for(std::uint32_t i=0;i<4;++i)rig->bindings[1020+i].joint=i;
        auto weighted=std::make_shared<Mesh>(*Mesh::cube());weighted->skeleton=rig;weighted->skin.resize(weighted->vertices.size());
        for(auto& skin:weighted->skin){skin.joints={1020,1021,1022,1023};skin.weights={.1f,.2f,.3f,.4f};}
        g.a->mesh=weighted;g.a->transform.position={-3,2,1};g.a->transform.scale={2,2,2};
        std::vector<JointPose> poses(4);poses[0].rotation={0,std::sin(.15f),0,std::cos(.15f)};poses[0].scale={1.2f,.7f,1.1f};
        poses[1].translation={.5f,0,0};poses[1].rotation={0,0,std::sin(.1f),std::cos(.1f)};poses[2].translation={0,.8f,0};poses[3].rotation={std::sin(-.125f),0,0,std::cos(-.125f)};
        g.a->skeleton().setPose(poses);capture("large-palette");
        auto reference=std::make_shared<Mesh>(*weighted);reference->skeleton.reset();reference->skin.clear();const auto palette=g.a->skinPose();
        for(std::size_t i=0;i<reference->vertices.size();++i){const auto v=weighted->vertices[i];const Vec3 tangent{v.tangent[0],v.tangent[1],v.tangent[2]},bitangent=cross(v.normal,tangent)*v.tangent[3];Vec3 p{},n{},t{},b{};
            for(std::size_t j=0;j<4;++j){const auto& bone=palette->bones[weighted->skin[i].joints[j]];const float w=weighted->skin[i].weights[j];
                p=p+transformPoint(bone.transform,v.position)*w;n=n+transformDirection(bone.normal,v.normal)*w;t=t+transformDirection(bone.transform,tangent)*w;b=b+transformDirection(bone.transform,bitangent)*w;}
            reference->vertices[i].position=p;reference->vertices[i].normal=n;reference->vertices[i].tangent={t.x,t.y,t.z,dot(cross(n,t),b)<0?-1.f:1.f};
        }
        reference->invalidateBounds();auto& baked=g.world->spawn("Baked reference").addComponent<MeshComponent>("Baked reference",reference);baked.color={1,1,1};baked.transform=g.a->transform;baked.setMaterial(0,MaterialInstance{g.surface});g.a->enabled=false;capture("weighted-reference");baked.destroy();
    }
    reset();
    if(backend=="d3d12"){
        auto bad=std::make_shared<Mesh>(*g.asset);bad->skin[0].joints[0]=1024;g.a->mesh=bad;g.a->frustumCulling=false;bool rejected=false;
        try{step();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid skin binding accepted");g.a->mesh=g.asset;g.a->frustumCulling=true;
    }
    reset();capture("restored");
    // Mesh replacement and removal must retire pose buffers without retaining CPU assets.
    g.a->mesh=Mesh::cube();g.b->mesh=Mesh::cube();g.morph->mesh=Mesh::cube();capture("static-replaced");
    std::ofstream out(directory/"report.json");out<<"{\"passed\":true,\"requested\":\""<<backend<<"\",\"cases\":{"<<phases.str()<<"}}\n";check(bool(out),"Cannot write animation evidence");
    host.requestClose();host.step({},time+=1./60);std::cout<<backend<<" skin/morph rendering acceptance passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
