#include "engine/material.hpp"
#include "engine/material_showcase.hpp"
#include "engine/mesh_component.hpp"
#include "engine/render_frame.hpp"
#include "engine/fx_frame.hpp"
#include "engine/fbx_importer.hpp"
#include <iostream>
#include <cstring>
#include <fstream>

using namespace engine;
namespace {
void check(bool v,const char* m) { if(!v) throw std::runtime_error(m); }
template<class F> void rejects(F f) { bool rejected=false;try { f(); } catch(const std::exception&) { rejected=true; }check(rejected,"Invalid material data was accepted"); }
bool close(float a,float b) { return std::abs(a-b)<0.002f; }
}
int main() {
    try {
        auto material=std::make_shared<Material>();material->name="Oak";material->roughness=0.75f;material->baseColorTexture=Texture::white();
        auto mesh=std::make_shared<Mesh>(*Mesh::cube());mesh->materials={material,material};mesh->parts={{0,18,0},{18,18,1}};
        Scene scene;auto& a=scene.addChild<MeshComponent>("A",mesh);auto& b=scene.addChild<MeshComponent>("B",mesh);a.color=b.color={1,1,1};
        MaterialInstance instance;instance.tint={0.5f,1,1};instance.roughness=0.2f;instance.alphaMode=AlphaMode::Blend;instance.opacity=0.4f;
        a.setMaterial(1,instance);auto frame=scene.collect();
        check(frame.meshes.size()==4&&frame.meshes[0].blend==BlendMode::Opaque&&frame.meshes[1].blend==BlendMode::Alpha,"Alpha mode must route individual slots");
        check(close(frame.meshes[1].opacity,0.4f)&&close(frame.meshes[1].color.x,0.5f)&&close(frame.meshes[3].material.roughness,0.75f),"Slot edits must not mutate shared assets or other objects");
        FxFrame fx;fx.prepare(frame,Mat4::identity());check(fx.batches(RenderLayer::World).size()==1,"Only the blended slot enters the sorted FX queue");
        auto invalid=instance;invalid.roughness=-1;rejects([&]{a.setMaterial(1,invalid);});check(close(a.resolvedMaterial(1).roughness,0.2f),"Failed edits preserve existing overrides");
        rejects([&]{a.setMaterial(2,instance);});a.clearMaterial(1);check(close(a.resolvedMaterial(1).roughness,0.75f),"Reset restores imported material");
        a.texture=Texture::flatNormal();instance={};instance.baseColorTexture=std::shared_ptr<const Texture>{};a.setMaterial(0,instance);
        check(!a.resolvedMaterial(0).baseColorTexture&&a.resolvedMaterial(1).baseColorTexture==a.texture,"Per-slot map removal overrides legacy whole-mesh texture only for that slot");
        auto primitive=Mesh::cube();Scene batching;auto& first=batching.addChild<MeshComponent>("First",primitive);auto& second=batching.addChild<MeshComponent>("Second",primitive);
        RenderFrame render;render.collect(batching);check(render.batches(RenderLayer::World).size()==1,"Matching materials instance together");
        MaterialInstance rough;rough.roughness=0.1f;second.setMaterial(0,rough);render.collect(batching);check(render.batches(RenderLayer::World).size()==2,"Roughness differences must split material bindings");
        second.clearMaterial(0);second.transform.scale.x=-1;render.collect(batching);check(render.batches(RenderLayer::World).size()==2&&render.sceneFrame().meshes[1].mirrored,"Mirrored winding must select a separate pipeline");
        (void)first;
        Texture image;image.width=2;image.height=1;image.pixels={0,0,0,0,255,255,255,255};
        check(image.mipChain(TextureSemantic::Color).back().pixels[0]==188,"Color mip averaging must happen in linear space");
        check(image.mipChain(TextureSemantic::Data).back().pixels[0]==128,"Data maps must not receive sRGB conversion");
        image.width=3;image.pixels={0,0,0,255,0,0,0,255,255,255,255,255};check(image.mipChain(TextureSemantic::Data).back().pixels[0]==85,"Odd texture dimensions must retain their edge texels");
        image.width=2;image.pixels={255,128,128,255,128,255,128,255};const auto pixel=image.mipChain(TextureSemantic::Normal).back().pixels;
        Vec3 n{pixel[0]/127.5f-1,pixel[1]/127.5f-1,pixel[2]/127.5f-1};check(std::abs(dot(n,n)-1)<0.02f,"Normal map mip vectors must remain normalized");
        for(const auto& vertex:Mesh::cube()->vertices) {
            const Vec3 t{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]};check(close(dot(t,t),1)&&close(dot(t,vertex.normal),0),"Cube tangent basis must be orthonormal");
        }
        Mesh seam;seam.vertices={{{0,0,0},{0,0,1},{0,0}},{{1,0,0},{0,0,1},{1,0}},{{0,1,0},{0,0,1},{0,1}},{{-1,0,0},{0,0,1},{1,0}}};seam.indices={0,1,2,0,2,3};seam.skin.resize(4);seam.generateTangents();
        check(seam.vertices.size()>4&&seam.skin.size()==seam.vertices.size(),"Mirrored UV seams must split tangents and preserve skin weights");
        check(seam.vertices[seam.indices[0]].tangent[3]!=seam.vertices[seam.indices[3]].tangent[3],"Mirrored UV islands require opposite tangent handedness");
        check(floatToHalf(1)==0x3c00&&floatToHalf(0.5f)==0x3800&&floatToHalf(0)==0,"HDR upload float16 encoding");
        const auto env=Environment::bake("Constant",[](Vec3){return Vec3{2,1,0.5f};},4);
        for(const auto& level:env->specular) for(const auto& p:level.pixels) check(close(p[0],2)&&close(p[1],1),"Specular prefilter must preserve constant HDR radiance at every roughness");
        for(const auto& p:env->diffuse.pixels) check(close(p[0],2)&&close(p[2],0.5f),"Diffuse convolution must preserve constant irradiance / pi");
        rejects([]{Environment::bake("Invalid",[](Vec3){return Vec3{-1,0,0};},4);});
        auto& showcase=buildMaterialShowcase(scene);scene.update(0.1);check(showcase.children().size()==8,"Showcase must cover all material surfaces");
        auto imported=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"Knarr.fbx");
        std::size_t packed=0;for(const auto& m:imported->materials) { m->validate();if(m->metallicRoughnessTexture) ++packed; }
        check(packed==7,"Knarr separate roughness and metalness maps must become packed PBR maps");
        std::ifstream fixture(std::filesystem::path(FBX_FIXTURE_DIR)/"textured_embedded_ascii.fbx");
        std::string fbx((std::istreambuf_iterator<char>(fixture)),std::istreambuf_iterator<char>());
        const std::string property="P: \"DiffuseColor\", \"Color\", \"\", \"A\",1,1,1";
        auto pos=fbx.find(property);check(pos!=std::string::npos,"Authored material fixture property");
        fbx.insert(pos,"P: \"EmissiveColor\", \"Color\", \"\", \"A\",0.25,0.5,1\n P: \"EmissiveFactor\", \"Number\", \"\", \"A\",2\n P: \"TransparencyFactor\", \"Number\", \"\", \"A\",0.6\n ");
        pos=fbx.rfind('}');fbx.insert(pos," C: \"OP\",4000,3000,\"NormalMap\"\n");
        const auto mapped=FbxImporter::loadMemory(std::as_bytes(std::span(fbx)));
        const auto& m=*mapped->materials[0];
        check(m.normalTexture==m.baseColorTexture&&m.normalTexture,"FBX normal and base maps must reuse one decoded source image");
        check(close(m.emissive.x,0.5f)&&close(m.emissive.y,1)&&close(m.emissive.z,2),"FBX emission color and factor must map into linear HDR emission");
        check(close(m.opacity,0.4f)&&m.alphaMode==AlphaMode::Blend,"Legacy FBX transparency must map into per-slot alpha blending");
        const auto old=a.texture;
        rejects([&]{a.loadTexture(std::as_bytes(std::span(m.baseColorTexture->pixels)));});
        check(a.texture==old&&!a.resolvedMaterial(0).baseColorTexture,"Malformed whole-mesh texture edits preserve all slot bindings");
        std::ifstream png(std::filesystem::path(FBX_FIXTURE_DIR)/"uv-colors.png",std::ios::binary);
        std::string imageBytes((std::istreambuf_iterator<char>(png)),std::istreambuf_iterator<char>());
        a.loadTexture(std::as_bytes(std::span(imageBytes)));
        check(a.resolvedMaterial(0).baseColorTexture==a.texture&&a.resolvedMaterial(1).baseColorTexture==a.texture,"Whole-mesh texture convenience updates overridden and inherited slots");
        std::cout<<"Material, texture, tangent, environment and showcase checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
