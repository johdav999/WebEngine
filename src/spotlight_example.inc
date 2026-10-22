// Application-only example and test fixtures. Lights remain ordinary scene components.
EM_JS(int,spotlight_requested,(),{const q=new URLSearchParams(location.search);return ['spotlight','lighting'].includes(q.get('scene'))||q.get('spotlightTest')==='1';});
extern "C" EMSCRIPTEN_KEEPALIVE int engine_spotlight_fixture(int mode){
    try{
        if(!spotlight_requested()||mode<0||mode>10||openedScene||browserEditorPlaying()||browserEditorFxActive())return 0;
        const bool showcase=mode>=8;
        auto candidate=std::make_unique<Actor>("Spotlight fixture");candidate->setSavedId("spotlight.fixture");
        auto material=std::make_shared<Material>();material->roughness=1;material->metallic=0;
        auto mesh=std::make_shared<Mesh>(*Mesh::plane(20));mesh->materials={material};
        auto& receiver=candidate->addComponent<MeshComponent>("Light receiver",mesh);receiver.color={.6f,.6f,.6f};
        if(!showcase)receiver.transform.rotation.x=pi/2;
        const int count=mode==8?2:mode==5?2:mode==6||mode==7?9:1;
        for(int i=0;i<count;++i){
            auto& light=candidate->addComponent<LightComponent>(i==0?"Spotlight":"Additional light",mode==1?LightType::Point:LightType::Spot);
            light.transform.position=mode==8?Vec3{i==0?-.8f:.8f,1.1f,3}:Vec3{0,0,4};
            light.direction=mode==8?Vec3{0,-.22f,-1}:mode==3?Vec3{0,0,1}:Vec3{0,0,-1};
            light.innerAngle=15*pi/180;light.outerAngle=30*pi/180;light.range=mode==2?3:12;light.intensity=8;
            if(mode==5){light.visibilityMask=i==0?1:2;light.color=i==0?Vec3{1,0,0}:Vec3{0,0,1};}
            if(mode==6||mode==7)light.intensity=(mode==6?i==8:i==7)?8:0;
            if(mode==8){light.color={1,.9f,.7f};light.range=18;light.intensity=15;}
            if(showcase)light.shadow.enabled=true;
            if(mode==9){light.transform.position={-2,2.5f,3};light.direction={.3f,-.4f,-1};light.color={1,.55f,.2f};light.range=20;light.intensity=25;light.innerAngle=.3f;light.outerAngle=.65f;}
            if(mode==10){light.type=LightType::Directional;light.direction={-.4f,-1,-.35f};light.color={1,.93f,.8f};light.intensity=3;light.shadow.halfExtent=12;light.shadow.halfDepth=20;}
        }
        if(mode==4){candidate->transform.rotation.y=.3f;candidate->transform.position.x=1;}
        if(mode==8){
            auto& body=candidate->addComponent<MeshComponent>("Example chassis",Mesh::cube());body.transform.position={0,.6f,4.4f};body.transform.scale={2,.8f,3};body.color={.05f,.2f,.5f};
            auto& cabin=candidate->addComponent<MeshComponent>("Example cabin",Mesh::cube());cabin.transform.position={0,1.2f,4.7f};cabin.transform.scale={1.6f,.6f,1.4f};cabin.color={.15f,.25f,.35f};
        }
        if(showcase){
            for(int i=0;i<3;++i){auto& blocker=candidate->addComponent<MeshComponent>("Shadow pillar",Mesh::cube());blocker.transform.position={float(i-1)*2, .8f, float(-i)};blocker.transform.scale={.65f,1.6f,.65f};blocker.color={.3f,.4f,.55f};}
            auto glowMaterial=std::make_shared<Material>();glowMaterial->shader=MaterialShader::Unlit;glowMaterial->emissive={16,5,1};
            auto glowMesh=std::make_shared<Mesh>(*Mesh::cube());glowMesh->materials={glowMaterial};
            auto& glow=candidate->addComponent<MeshComponent>("HDR light source",glowMesh);glow.transform.position=mode==9?Vec3{-2,2.5f,3}:Vec3{3,1,-3};glow.transform.scale={.18f,.18f,.18f};glow.castsShadow=false;
        }
        candidate->visit([](const Node& n,const Mat4&){if(const auto* c=dynamic_cast<const Component*>(&n))c->validate();});
        if(auto* previous=dynamic_cast<Actor*>(scene.findSavedNode("spotlight.fixture"))){previous->destroy();scene.flushPendingDestroy();}
        for(const auto& child:scene.children())child->enabled=false;
        auto* root=candidate.get();scene.adoptChild(std::move(candidate));camera->enabled=true;camera->transform={};camera->setViewport({});camera->setVisibilityMask(0xffffffffu);
        camera->transform.position=showcase?Vec3{7,7,11}:Vec3{0,0,8};camera->lookAt(showcase?Vec3{0,0,-1}:Vec3{});camera->setOrthographic(showcase?15:8,.1f,60);
        scene.ambient=showcase?Vec3{.025f,.025f,.025f}:Vec3{};scene.setFog(std::nullopt);scene.setSky(std::nullopt);scene.environment.reset();setPaused(true);
        renderer->setViews(std::array{RenderView{camera}});
        if(mode==5){camera->setVisibilityMask(1);camera->setViewport({0,0,.5f,1});auto& second=root->addComponent<CameraComponent>("Masked second view");second.transform=camera->transform;second.setOrthographic(8,.1f,60);second.setVisibilityMask(2);second.setViewport({.5f,0,.5f,1});receiver.visibilityMask=3;renderer->setViews(std::array{RenderView{camera},RenderView{&second}});}
        orbit->adoptCamera(showcase?15:8);scene.synchronize();if(browserEditorActive())bindBrowserEditor(scene,*camera,*orbit,displayedLibrary(),renderer->views());return 1;
    }catch(const std::exception& error){archive_message(error.what(),1);return 0;}
}

extern "C" EMSCRIPTEN_KEEPALIVE int engine_lighting_fixture(int directional) {
    if(!engine_spotlight_fixture(0))return 0;
    try {
        auto* root=dynamic_cast<Actor*>(scene.findSavedNode("spotlight.fixture"));
        auto* light=root->getComponent<LightComponent>();light->type=directional?LightType::Directional:LightType::Spot;
        light->transform.position={2,2,5};light->direction={-.35f,-.35f,-1};light->outerAngle=.7f;light->innerAngle=.45f;
        light->range=20;light->intensity=15;light->shadow.enabled=true;light->shadow.halfExtent=7;light->shadow.halfDepth=15;
        auto& box=root->addComponent<MeshComponent>("Shadow blocker",Mesh::cube());box.transform.position={0,0,2};box.transform.scale={.8f,.8f,.8f};box.color={.1f,.2f,.5f};
        auto m=std::make_shared<Material>();m->shader=MaterialShader::Unlit;m->emissive={20,5,1};
        auto mesh=std::make_shared<Mesh>(*Mesh::cube());mesh->materials={m};
        auto& emitter=root->addComponent<MeshComponent>("HDR emitter",mesh);emitter.transform.position={-2,1,1};emitter.transform.scale={.25f,.25f,.25f};emitter.castsShadow=false;
        scene.ambient={.015f,.015f,.015f};scene.synchronize();return 1;
    }catch(const std::exception&){return 0;}
}
