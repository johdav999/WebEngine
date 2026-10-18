#include "water_demo.hpp"
#include "engine/mesh_component.hpp"

using namespace engine;
namespace {
std::shared_ptr<const Mesh> surface() {
    auto mesh=std::make_shared<Mesh>();constexpr int divisions=128;constexpr float size=18;
    for(int z=0;z<=divisions;++z) for(int x=0;x<=divisions;++x)
        mesh->vertices.push_back({{size*(float(x)/divisions-.5f),0,size*(float(z)/divisions-.5f)},
            {0,1,0},{float(x)/divisions,float(z)/divisions},{1,0,0,-1}});
    for(int z=0;z<divisions;++z) for(int x=0;x<divisions;++x) {
        const auto a=std::uint32_t(z*(divisions+1)+x),b=a+1,c=a+divisions+1,d=c+1;
        mesh->indices.insert(mesh->indices.end(),{a,c,b,b,c,d});
    }
    return mesh;
}
std::shared_ptr<const Texture> ripples() {
    auto t=std::make_shared<Texture>();t->name="Procedural seamless water normals";t->width=t->height=128;t->pixels.resize(128*128*4);
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
        const float u=float(x)/128*2*pi,v=float(y)/128*2*pi;
        const auto n=normalized(Vec3{.35f*std::cos(u*3+v*2)+.2f*std::sin(u*7-v*4),.25f*std::sin(u*2-v*5),1});
        const auto i=(y*128+x)*4;
        t->pixels[i]=std::uint8_t((n.x*.5f+.5f)*255);t->pixels[i+1]=std::uint8_t((n.y*.5f+.5f)*255);
        t->pixels[i+2]=std::uint8_t((n.z*.5f+.5f)*255);t->pixels[i+3]=255;
    }
    return t;
}
std::shared_ptr<const Mesh> lakebed() {
    auto mesh=std::make_shared<Mesh>();constexpr int divisions=32;
    for(int z=0;z<=divisions;++z) for(int x=0;x<=divisions;++x) {
        const float px=22*(float(x)/divisions-.5f),pz=22*(float(z)/divisions-.5f);
        mesh->vertices.push_back({{px,.6f-(pz+9)*.23f,pz},normalized(Vec3{0,1,.23f}),{float(x)/divisions,float(z)/divisions},{1,0,0,-1}});
    }
    for(int z=0;z<divisions;++z) for(int x=0;x<divisions;++x) {
        const auto a=std::uint32_t(z*(divisions+1)+x),b=a+1,c=a+divisions+1,d=c+1;
        mesh->indices.insert(mesh->indices.end(),{a,c,b,b,c,d});
    }
    auto texture=std::make_shared<Texture>();texture->name="Lakebed sand and stone pattern";texture->width=texture->height=128;texture->pixels.resize(128*128*4);
    for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
        const bool stone=(x/8+y/8)%2;const auto i=(y*128+x)*4;
        texture->pixels[i]=stone ? 80 : 195;texture->pixels[i+1]=stone ? 120 : 164;texture->pixels[i+2]=stone ? 125 : 104;texture->pixels[i+3]=255;
    }
    auto material=std::make_shared<Material>();material->name="Sloping lakebed";material->baseColorTexture=texture;material->roughness=.9f;
    mesh->materials={material};mesh->parts={{0,std::uint32_t(mesh->indices.size()),0}};return mesh;
}
}
void WaterDemo::start(Scene& scene,OrbitController& orbit) {
    if(active_&&scene.findActor(root_)) return;
    active_=false;previousNodes_.clear();
    previousOrbit_=orbit.snapshot();previousSky_=scene.sky();previousAmbient_=scene.ambient;
    previousEnvironmentIntensity_=scene.environmentIntensity;
    auto* root=scene.findActor(root_);
    if(!root) {
        root=&scene.spawn<Actor>("Water demo");root_=root->id();
        auto& water=root->addChild<MeshComponent>("Animated reflective water",surface());water_=water.id();
        water.color={1,1,1};water.castsShadow=false;
        auto m=std::make_shared<Material>();m->name="Lake water";m->shader=MaterialShader::Water;
        m->baseColor={0.018f,0.11f,0.16f};m->roughness=.16f;m->normalTexture=ripples();m->water.amplitude=.16f;m->water.wavelength=4;
        m->water.planarReflectionStrength=1;m->water.underwaterStrength=1;
        m->water.refractionStrength=.85f;m->water.foamStrength=.9f;m->water.foamWidth=.4f;
        water.setMaterial(0,MaterialInstance(m));
        auto& boat=root->addChild<MeshComponent>("Moving wake source",Mesh::cube());boat_=boat.id();
        boat.transform.scale={.55f,.3f,1.1f};boat.color={1,.65f,.06f};boat.castsShadow=false;
        auto& impact=root->addChild<MeshComponent>("Ripple contact source",Mesh::sphere(16,12));impact_=impact.id();
        impact.transform.scale={.25f,.25f,.25f};impact.color={1,.2f,.05f};impact.castsShadow=false;
        auto& bed=root->addChild<MeshComponent>("Sloping shallow and deep lakebed",lakebed());bed.color={1,1,1};bed.castsShadow=false;
        auto& foreground=root->addChild<MeshComponent>("Refraction foreground rejection fixture",Mesh::cube());foreground_=foreground.id();
        foreground.transform.position={-2,1.5f,0};foreground.transform.scale={1,2,1};foreground.color={1,0,0};foreground.castsShadow=false;foreground.enabled=false;
        MaterialInstance unlit;unlit.shader=MaterialShader::Unlit;foreground.setMaterial(0,unlit);
        for(int i=0;i<9;++i) {
            const float z=-4+float(i/3)*3,x=-3+float(i%3)*2;
            auto& stone=root->addChild<MeshComponent>("Submerged depth marker",Mesh::cube());
            stone.transform.position={x,.75f-(z+9)*.23f,z};stone.transform.scale={.7f,.18f,.5f};
            stone.color=i%2 ? Vec3{.8f,.38f,.1f} : Vec3{.65f,.8f,.9f};stone.castsShadow=false;
        }
        for(int i=0;i<2;++i) {
            auto& marker=root->addChild<MeshComponent>(i ? "Far submerged marker" : "Near submerged marker",Mesh::cube());
            marker.transform.position=i ? Vec3{1.6f,-.8f,-1} : Vec3{-1,-.8f,3};marker.transform.scale={.4f,.4f,.4f};marker.color={.8f,.8f,.8f};marker.castsShadow=false;
            MaterialInstance material;material.shader=MaterialShader::Unlit;marker.setMaterial(0,material);
        }
        auto& light=root->addChild<LightComponent>("Water sun",LightType::Directional);
        light.direction={-.5f,-.6f,-.3f};light.color={1,.88f,.66f};light.intensity=3;
        for(int i=0;i<2;++i) {
            auto& marker=root->addChild<MeshComponent>("Reflection landmark",Mesh::cube());
            marker.transform.position={-3+float(i)*4,1.7f,-3};marker.transform.scale={.8f,3.4f,.8f};
            marker.color=i ? Vec3{.04f,.3f,1} : Vec3{1,.35f,.03f};
            MaterialInstance color;color.shader=MaterialShader::Unlit;marker.setMaterial(0,color);
        }
        for(int i=0;i<7;i++) {
            auto& rock=root->addChild<MeshComponent>("Shore rock",Mesh::sphere(12,8));
            rock.color={.2f,.24f,.25f};rock.transform.position={-6+float(i)*2,.15f,-4};
            rock.transform.scale={.8f,.5f+float(i%3)*.3f,.65f};
        }
        auto& pier=root->addChild<MeshComponent>("Timber pier",Mesh::cube());
        pier.color={.32f,.16f,.065f};pier.transform.position={5,.35f,1};pier.transform.scale={2,.3f,7};
        for(int i=0;i<4;i++) {
            auto& post=root->addChild<MeshComponent>("Pier post",Mesh::cube());post.color={.17f,.08f,.035f};
            post.transform.position={4.3f+float(i%2)*1.4f,.25f,-1+float(i/2)*4};post.transform.scale={.2f,1.8f,.2f};
        }
    }
    for(const auto& node:scene.children()) if(node.get()!=root) {
        previousNodes_.emplace_back(node->savedId(),node->enabled);
        node->enabled=dynamic_cast<CameraComponent*>(node.get())!=nullptr;
    }
    root->enabled=true;
    SkyBackground sky;sky.horizon={.65f,.8f,1};sky.zenith={.06f,.2f,.45f};scene.setSky(sky);
    scene.ambient={.16f,.19f,.22f};scene.environmentIntensity=0; // Shared analytic reflection on both backends.
    orbit.restore({{0,0,0},{0,0,0},.65f,.28f,16});active_=true;interactionMode_=1;still_=false;
}
void WaterDemo::stop(Scene& scene,OrbitController& orbit) {
    if(!active_) return;
    if(auto* root=scene.findActor(root_)) root->enabled=false;
    for(const auto& [id,enabled]:previousNodes_) if(auto* node=scene.findSavedNode(id)) node->enabled=enabled;
    scene.setSky(previousSky_);scene.ambient=previousAmbient_;scene.environmentIntensity=previousEnvironmentIntensity_;
    orbit.restore(previousOrbit_);
    if(auto* camera=dynamic_cast<CameraComponent*>(scene.findComponent(camera_))) {
        camera->transform=previousCamera_;
        if(previousProjection_==ProjectionMode::Orthographic) camera->setOrthographic(previousOrthoHeight_,previousNear_,previousFar_);
        else camera->setPerspective(previousFov_,previousNear_,previousFar_);
    }
    camera_={};previousNodes_.clear();active_=false;
}
void WaterDemo::appearance(Scene& scene,int preset) {
    if(preset<0||preset>29) throw std::invalid_argument("Unknown water preset");
    still_=preset==1;
    auto* mesh=dynamic_cast<MeshComponent*>(scene.findComponent(water_));
    if(!active_||!mesh) throw std::invalid_argument("Water demo is inactive");
    auto m=std::make_shared<Material>(mesh->resolvedMaterial(0));
    m->water.amplitude=preset==1||preset==5 ? 0 : .16f;m->water.rippleStrength=preset==1||preset==4 ? 0 : .18f;
    m->alphaMode=preset==6 ? AlphaMode::Blend : AlphaMode::Opaque;m->opacity=preset==6 ? .6f : 1;
    m->doubleSided=true; // The submerged view can see the underside.
    mesh->transform.scale.x=preset==7 ? -1 : 1;
    m->water.reflectionStrength=preset==2 ? 0 : 1;
    m->water.planarReflectionStrength=preset==14||preset==16||preset==18||(preset==23||preset==24||preset==28) ? 0 : 1;
    m->water.speed=preset==1 ? 0 : 1;
    m->water.underwaterStrength=preset==19||preset==24 ? 0 : 1;m->water.underwaterFogDensity=preset==21 ? 0 : .12f;
    m->water.underwaterMaxDistance=preset==22 ? .5f : 100;
    m->water.causticsStrength=preset==25||(preset>=19&&preset<=24) ? 0 : 1.6f;
    m->water.causticsScale=2;m->water.causticsSpeed=preset==1||preset==26 ? 0 : 1;
    m->water.causticsDepth=preset==27 ? .4f : 8;
    if(preset==28) m->water.underwaterFogDensity=0;
    m->water.refractionStrength=preset==8||preset==10||(preset==23||preset==24||preset==28) ? 0 : .85f;
    m->water.foamStrength=preset==9||preset==10||preset==4||preset==5||(preset==23||preset==24||preset==28) ? 0 : .9f;
    m->water.refractionDistortion=preset==13 ? 0 : .015f;
    m->water.absorption=preset==11||preset==20||preset==28 ? Vec3{} : Vec3{.7f,.24f,.12f};
    if(auto* foreground=scene.findComponent(foreground_)) {foreground->enabled=preset>=15&&preset<=18;foreground->transform.position.y=preset>=17 ? -1.5f : 1.5f;}
    if(preset>=15&&preset<=18) {m->water.amplitude=0;m->water.rippleStrength=0;m->water.refractionStrength=0;m->water.foamStrength=0;}
    if(preset==12) {m->water.refractionDistortion=.08f;m->water.foamStrength=0;m->water.reflectionStrength=0;}
    if(preset==29) {m->water.amplitude=0;m->water.rippleStrength=0;m->water.foamStrength=0;m->water.refractionStrength=0;m->water.planarReflectionStrength=0;}
    scene.environmentIntensity=preset==3 ? 1 : 0;
    mesh->setMaterial(0,MaterialInstance(m));
}
void WaterDemo::depthFixture(Scene& scene,int mode) {
    if(mode<0||mode>4) throw std::invalid_argument("Unknown water depth fixture");
    appearance(scene,0);
    auto* mesh=dynamic_cast<MeshComponent*>(scene.findComponent(water_));
    if(auto* foreground=scene.findComponent(foreground_)) {foreground->enabled=mode==1||mode==2;foreground->transform.position.y=1.5f;}
    auto m=std::make_shared<Material>(mesh->resolvedMaterial(0));m->water.reflectionStrength=0;
    m->water.refractionStrength=mode==1||mode==2 ? 1 : 0;
    m->water.refractionDistortion=mode==2 ? .1f : 0;m->water.absorption={};
    m->water.foamStrength=mode==3 ? 1 : 0;m->water.foamWidth=.6f;
    mesh->setMaterial(0,MaterialInstance(m));
}

void WaterDemo::view(Scene& scene,OrbitController& orbit,CameraComponent& camera,int mode) {
    if(!active_||camera.scene()!=&scene||mode<0||mode>7) throw std::invalid_argument("Invalid water camera view");
    if(!camera_) {
        camera_=camera.id();previousCamera_=camera.transform;previousProjection_=camera.projectionMode();
        previousFov_=camera.fieldOfView;previousOrthoHeight_=camera.orthographicHeight();previousNear_=camera.nearPlane;previousFar_=camera.farPlane;
    }
    if(mode==6&&!scene.findComponent(underwaterOverlay_)) {
        auto* root=scene.findActor(root_);auto& marker=root->addChild<MeshComponent>("Underwater overlay fixture",Mesh::cube());underwaterOverlay_=marker.id();
        marker.layer=RenderLayer::Overlay;marker.color={1,0,1};marker.transform.position={0,-.8f,4};marker.transform.scale={.3f,.3f,.1f};marker.castsShadow=false;
        MaterialInstance material;material.shader=MaterialShader::Unlit;marker.setMaterial(0,material);
    }
    if(auto* overlay=scene.findComponent(underwaterOverlay_)) overlay->enabled=mode==6;
    camera.setPerspective(pi/3,.1f,100);
    if(mode==0) orbit.restore({{0,0,0},{0,0,0},.65f,.28f,16});
    else {const Vec3 target{mode==4 ? 25.0f : 0.0f,mode==2 ? -1.9f : mode==3 ? -.6f : -1.2f,-2};
        orbit.restore({target,target,0,mode==3 ? .12f : .05f,8});}
    orbit.orbit(0,0);
    if(mode==5) camera.setOrthographic(6,.1f,100);
    if(mode==7) {camera.transform.position={0,14,0};camera.lookAt({0,0,0},{0,0,-1});camera.setOrthographic(18,.1f,100);}
}

void WaterDemo::interactions(int mode) {
    if(mode<0||mode>7) throw std::invalid_argument("Unknown water interaction mode");
    interactionMode_=mode;
}
void WaterDemo::update(Scene& scene,Renderer& renderer,double time) {
    if(!active_) {renderer.setWaterInteractions({});return;}
    const double t=still_ ? 0 : time;
    const auto position=[](double t) {return Vec3{float(3*std::cos(t*.35)),.12f,float(2*std::sin(t*.35))};};
    const auto heading=normalized(Vec3{float(-1.05*std::sin(t*.35)),0,float(.7*std::cos(t*.35))});
    if(auto* boat=scene.findComponent(boat_)) {boat->enabled=interactionMode_==1||interactionMode_==3;boat->transform.position=position(t);boat->transform.rotation.y=std::atan2(heading.x,heading.z);}
    if(auto* impact=scene.findComponent(impact_)) {impact->enabled=interactionMode_==1||interactionMode_==2;
        impact->transform.position={-4,.2f+float(std::abs(std::sin(t*pi/4))*1.4),2};}
    std::array<WaterInteraction,5> inputs{};std::size_t count=0;
    if(interactionMode_==1||interactionMode_==3) {
        auto& wake=inputs[count++];wake.position=position(t);wake.position.y=0;wake.direction=heading;
        wake.startTime=t;wake.wakeLength=4;wake.radius=.28f;wake.strength=.9f;wake.wavelength=.55f;
    }
    if(interactionMode_==1||interactionMode_==2) {
        auto& ring=inputs[count++];ring.position={-4,0,2};ring.startTime=std::floor(t/4)*4;
        ring.radius=.55f;ring.speed=1.5f;ring.strength=.85f;
        for(int i=0;i<3&&interactionMode_==1;++i) {
            const double stamp=std::floor(t/.5)*.5-i*.5;if(stamp<0) continue;
            auto& trail=inputs[count++];trail.position=position(stamp);trail.position.y=0;
            trail.startTime=stamp;trail.radius=.25f;trail.strength=.45f;trail.speed=1.3f;trail.lifetime=2;
        }
    }
    if(interactionMode_>=4) {
        auto& fixture=inputs[count++];fixture.position={0,interactionMode_==7 ? 10.f : 0.f,0};fixture.startTime=0;
        fixture.strength=1;fixture.radius=.65f;fixture.wavelength=.7f;fixture.speed=2;
        fixture.wakeLength=interactionMode_==5 ? 5 : 0;fixture.direction={1,0,0};
        if(interactionMode_==6) fixture.visibilityMask=0;
    }
    // Preserve the visible still-water preset without advancing interaction age.
    if(still_) for(std::size_t i=0;i<count;++i) inputs[i].startTime+=time;
    renderer.setWaterInteractions({inputs.data(),count});
}
