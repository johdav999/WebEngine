#include "engine/scene.hpp"
#include "engine/camera_component.hpp"
#include <limits>
namespace engine {
void ParticleEmitterComponent::appendMeshes(SceneFrame& frame,const Mat4& world,const Frustum* frustum) const {
    const auto& d=*definition_;
    if(d.renderMode!=ParticleRenderMode::Mesh)return;
    const auto& mesh=d.particleMesh;
    const auto& bounds=mesh->localBounds();
    const auto layer=static_cast<std::size_t>(RenderLayer::World);
    frame.meshParticleCount+=particles_.size();
    for(const auto& p:particles_){
        const float age=std::clamp(p.age/p.lifetime,0.f,1.f),size=d.sizeAt(age);
        if(size<=0)continue;
        Transform pose;pose.position=p.position;pose.rotation=p.meshRotation+Vec3{0,0,p.rotation};pose.scale={size,size,size};
        Mat4 model;
        if(d.space==ParticleSpace::Local)model=world*pose.matrix();
        else {Transform rotation=pose;rotation.position={};Transform translation;translation.position=p.position;model=translation.matrix()*p.meshBasis*rotation.matrix();}
        Mat4 normal;
        try{normal=transpose(inverseAffine(model));}catch(const std::invalid_argument&){continue;}
        bool finite=true;for(float value:model.m)finite&=std::isfinite(value);for(float value:normal.m)finite&=std::isfinite(value);
        if(!finite)continue;
        ++frame.meshCount;
        const bool visible=!frustum||!bounds.valid||frustum->intersectsTransformedAabb(bounds.minimum,bounds.maximum,model);
        frame.culledMeshes+=!visible;
        const auto color=d.colorAt(age);
        const Vec3 x{model.m[0],model.m[1],model.m[2]},y{model.m[4],model.m[5],model.m[6]},z{model.m[8],model.m[9],model.m[10]};
        std::uint32_t partIndex=0;
        auto append=[&](std::size_t slot,std::uint32_t first,std::uint32_t count){
            const auto asset=slot<mesh->materials.size()&&mesh->materials[slot]?mesh->materials[slot]:Material::standard();
            auto material=asset->parameters();
            if(d.meshTextureOverride)material.baseColorTexture=d.texture;
            if(d.blend!=BlendMode::Opaque)material.alphaMode=d.blend==BlendMode::Additive?AlphaMode::Additive:AlphaMode::Blend;
            else if(color.a<1&&material.alphaMode!=AlphaMode::Additive)material.alphaMode=AlphaMode::Blend;
            const auto blend=material.alphaMode==AlphaMode::Blend?BlendMode::Alpha:material.alphaMode==AlphaMode::Additive?BlendMode::Additive:BlendMode::Opaque;
            MeshDraw draw{mesh,model,{color.r*material.baseColor.x,color.g*material.baseColor.y,color.b*material.baseColor.z},RenderLayer::World,
                material.baseColorTexture,first,count,normal,{},blend,color.a*material.opacity,material,dot(x,cross(y,z))<0,false,true,visible,{},true,mesh,bounds,id(),partIndex++,{}};
            draw.receivesDecals=false;draw.visibilityMask=visibilityMask;
            if(frame.meshes.size()>=std::numeric_limits<std::uint32_t>::max())throw std::length_error("Too many mesh particle draws");
            const auto mc=frame.meshes.capacity(),lc=frame.layerDraws[layer].capacity();
            frame.layerDraws[layer].push_back(std::uint32_t(frame.meshes.size()));frame.meshes.push_back(std::move(draw));
            frame.storageGrowths+=(mc!=frame.meshes.capacity())+(lc!=frame.layerDraws[layer].capacity());
        };
        if(mesh->parts.empty())append(0,0,std::uint32_t(mesh->indices.size()));
        else for(const auto& part:mesh->parts)append(part.materialIndex,part.firstIndex,part.indexCount);
    }
}
}
