#include "engine/glb_importer.hpp"
#include "engine/asset_decode.hpp"
#include "../src/material_delivery.hpp"
#include <bit>
#include <cstring>
#include <iostream>
#ifndef __EMSCRIPTEN__
#include <fstream>
#endif
using namespace engine;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,message);}
void put16(std::vector<std::byte>& out,std::uint16_t v){out.push_back(std::byte(v&255));out.push_back(std::byte(v>>8));}
void put(std::vector<std::byte>& out,std::uint32_t v){put16(out,std::uint16_t(v));put16(out,std::uint16_t(v>>16));}
void text(std::vector<std::byte>& out,std::string_view s){for(char c:s)out.push_back(std::byte(c));}
void floats(std::vector<std::byte>& out,std::initializer_list<float> f){for(float v:f)put(out,std::bit_cast<std::uint32_t>(v));}
std::vector<std::byte> glb(std::string manifest,bool atlas=false,bool skin=false,std::string advanced={},std::string extras={}){
    std::vector<std::byte> binary;floats(binary,{0,0,0,1,0,0,0,1,0});floats(binary,{0,0,1,0,0,1});floats(binary,{0,0,0,1,1,0});
    if(skin){for(int i=0;i<12;++i)put16(binary,0);floats(binary,{1,0,0,0,1,0,0,0,1,0,0,0});floats(binary,{0,0,0,0,0,0,0,0,.2f});}
    std::string json=R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0)";
    json+=skin?",1":"";json+=R"(]}],"nodes":[{"mesh":0)";json+=skin?R"(,"skin":0},{"name":"RootJoint")":"";json+=R"(}],"buffers":[{"byteLength":)"+std::to_string(binary.size())+R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":24})";
    if(skin)json+=R"(,{"buffer":0,"byteOffset":84,"byteLength":24},{"buffer":0,"byteOffset":108,"byteLength":48},{"buffer":0,"byteOffset":156,"byteLength":36})";
    json+=R"(],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"})";
    if(skin)json+=R"(,{"bufferView":3,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":5,"componentType":5126,"count":3,"type":"VEC3"})";
    json+=R"(],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1,"TEXCOORD_1":2)";if(skin)json+=R"(,"JOINTS_0":3,"WEIGHTS_0":4)";json+=R"(},"material":0)";if(skin)json+=R"(,"targets":[{"POSITION":5}])";json+=R"(}])";if(skin)json+=R"(,"weights":[0.25])";json+=R"(}],"materials":[{"name":"Duplicate-safe slot")";
    if(atlas)json+=R"(,"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":0}},"normalTexture":{"index":0,"texCoord":1})";
    if(!advanced.empty())json+=","+advanced;
    json+=R"(}])";
    if(atlas||advanced.find("Texture")!=std::string::npos)json+=R"(,"textures":[{"source":0}],"images":[{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAGklEQVR4nGNgcGhQ+H/A4QGLgkOCwsMEhwcAOo0HBeB4e6MAAAAASUVORK5CYII="}])";
    if(skin)json+=R"(,"skins":[{"joints":[1]}])";
    if(!manifest.empty())json+=R"(,"extras":{"WEBENGINE_materials":)"+manifest+"}";else if(!extras.empty())json+=",\"extras\":"+extras;
    json+='}';while(json.size()%4)json+=' ';while(binary.size()%4)binary.push_back(std::byte(0));
    std::vector<std::byte> result;text(result,"glTF");put(result,2);put(result,std::uint32_t(28+json.size()+binary.size()));put(result,std::uint32_t(json.size()));text(result,"JSON");text(result,json);put(result,std::uint32_t(binary.size()));text(result,std::string_view("BIN\0",4));result.insert(result.end(),binary.begin(),binary.end());return result;
}
std::vector<std::byte> bundle(const std::vector<std::byte>& mesh,std::string_view manifest,std::string firstName="asset.glb"){
    struct Member{std::string name;std::vector<std::byte> data;std::uint32_t crc=0,offset=0;};
    std::vector<std::byte> metadata,report;text(metadata,manifest);const auto parsed=material_delivery::Parser(manifest).parse();const auto materialStart=manifest.find("["),materialEnd=manifest.rfind("]");check(materialStart!=std::string_view::npos&&materialEnd!=std::string_view::npos,"Test manifest array");text(report,std::string("{\"version\":1,\"glbSha256\":\"")+material_delivery::sha256(mesh)+"\",\"materialManifestSha256\":\""+material_delivery::sha256(metadata)+"\",\"materials\":"+std::string(manifest.substr(materialStart,materialEnd-materialStart+1))+"}");
    std::array<Member,3> members={Member{firstName,mesh},Member{"materials.webengine.json",metadata},Member{"export-report.json",report}};std::vector<std::byte> out;
    for(auto& m:members){m.offset=std::uint32_t(out.size());m.crc=material_delivery::crc32(m.data);put(out,0x04034b50);put16(out,20);put16(out,0);put16(out,0);put16(out,0);put16(out,0);put(out,m.crc);put(out,std::uint32_t(m.data.size()));put(out,std::uint32_t(m.data.size()));put16(out,std::uint16_t(m.name.size()));put16(out,0);text(out,m.name);out.insert(out.end(),m.data.begin(),m.data.end());}
    const auto central=out.size();for(auto& m:members){put(out,0x02014b50);put16(out,20);put16(out,20);put16(out,0);put16(out,0);put16(out,0);put16(out,0);put(out,m.crc);put(out,std::uint32_t(m.data.size()));put(out,std::uint32_t(m.data.size()));put16(out,std::uint16_t(m.name.size()));put16(out,0);put16(out,0);put16(out,0);put16(out,0);put(out,0);put(out,m.offset);text(out,m.name);}const auto centralSize=out.size()-central;put(out,0x06054b50);put16(out,0);put16(out,0);put16(out,3);put16(out,3);put(out,std::uint32_t(centralSize));put(out,std::uint32_t(central));put16(out,0);return out;
}
}
int main(int argc,char** argv){try{
    check(material_delivery::sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA-256 empty vector");
    const std::string abc="abc";check(material_delivery::sha256(std::as_bytes(std::span(abc)))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 known vector");
    check(GlbImporter::loadMemory(glb({},false,false,{},R"(["ordinary metadata",2])"))->indices.size()==3,"Legal unrelated non-object glTF extras remain supported");
    const std::string manifest=R"({"version":1,"materials":[{"index":0,"name":"Glass","shader":"glass","parameters":{"clearcoat":0.8,"transmission":0.7,"ior":1.6,"thickness":0.3,"attenuationColor":[0.5,0.8,1],"attenuationDistance":2,"subsurface":0.2,"subsurfaceColor":[1,0.2,0.1],"sheen":0.4,"sheenColor":[0.2,0.3,0.7],"sheenRoughness":0.6,"anisotropy":-0.3,"anisotropyRotation":0.2,"specular":0.4},"uvAnimation":{"speed":[0.1,-0.2]},"wind":{"direction":[1,0,0],"amplitude":0.4,"frequency":2,"speed":-1},"diagnostics":{"status":"approximated","reasons":["Portable glass"]}}]})";
    auto additive=GlbImporter::loadMemory(glb(R"({"version":1,"materials":[{"index":0,"shader":"unlit","alphaMode":"additive","alphaCutoff":0.3,"doubleSided":true}]})"));check(additive->materials.at(0)->alphaMode==AlphaMode::Additive&&additive->materials.at(0)->doubleSided,"Unreal additive and sidedness settings preserved");
    rejects([&]{GlbImporter::loadMemory(glb(R"({"version":1,"materials":[{"index":0,"shader":"pbr","alphaMode":"other"}]})"));},"Unknown alpha mode rejected");
    rejects([&]{GlbImporter::loadMemory(glb(R"({"version":1,"materials":[{"index":0,"shader":"pbr","doubleSided":1}]})"));},"Non-Boolean sidedness rejected");
    auto bytes=glb(manifest);auto mesh=GlbImporter::loadMemory(bytes);auto& material=*mesh->materials.at(mesh->parts.at(0).materialIndex);
    check(material.name=="Glass"&&material.advanced.clearcoat==.8f&&material.advanced.transmission==.7f,"Index-bound manifest applies complete advanced factors");
    check(material.advanced.uvSpeed.y==-.2f&&material.advanced.windAmplitude==.4f&&material.advanced.sheenColor.z==.7f,"Dynamic and color parameters preserved");
    check(material.alphaMode==AlphaMode::Opaque,"Transmission preserves alpha coverage semantics");
    check(!mesh->warnings.empty(),"Material fidelity reasons retained");
    auto zipped=bundle(bytes,manifest);auto loaded=GlbImporter::loadMemory(zipped);check(loaded->materials.at(0)->advanced.ior==1.6f,"ZIP_STORED bundle imports through ordinary GLB entry");
    const auto transport=decodeAssetBytes(zipped,AssetDecodeKind::Glb);auto hydrated=std::get<std::shared_ptr<const Mesh>>(hydrateDecodedAsset(transport,AssetDecodeKind::Glb));check(hydrated->materials.at(0)->advanced.windAmplitude==.4f,"Worker/native decode transport preserves extended materials");
    auto corrupt=zipped;corrupt[40]^=std::byte{1};rejects([&]{GlbImporter::loadMemory(corrupt);},"Corrupted ZIP payload rejected");
    rejects([&]{GlbImporter::loadMemory(bundle(bytes,manifest,"../asset.glb"));},"ZIP traversal path rejected");
    rejects([&]{GlbImporter::loadMemory(bundle(bytes,R"({"version":1,"materials":[]})"));},"Manifest copies must agree");
    auto deflated=zipped;deflated[8]=std::byte{8};rejects([&]{GlbImporter::loadMemory(deflated);},"Unsupported compression rejected before allocation");
    for(const auto& bad:{std::string(R"({"version":2,"materials":[]})"),std::string(R"({"version":1,"materials":[{"index":1,"shader":"pbr"}]})"),std::string(R"({"version":1,"materials":[{"index":0,"shader":"pbr"},{"index":0,"shader":"unlit"}]})"),std::string(R"({"version":1,"materials":[{"index":0,"shader":"glass","parameters":{"ior":0.5}}]})"),std::string(R"({"version":1,"materials":[{"index":0,"shader":"pbr","wind":{"direction":[0,0,0],"amplitude":1,"frequency":1,"speed":1}}]})")})rejects([&]{GlbImporter::loadMemory(glb(bad));},"Invalid material contract rejected");
    for(auto bad:{R"({"x":1,"x":2})",R"({"x":1e999})",R"({"x":"\ud800"})",R"({"x":01})",R"([0,])"})rejects([&]{material_delivery::Parser(bad).parse();},"Malformed JSON rejected");
    auto standard=GlbImporter::loadMemory(glb({},false,false,R"("extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":0.6},"KHR_materials_transmission":{"transmissionFactor":0.4},"KHR_materials_ior":{"ior":1.7},"KHR_materials_sheen":{"sheenColorFactor":[0.2,0.4,0.8],"sheenRoughnessFactor":0.3},"KHR_materials_anisotropy":{"anisotropyStrength":0.5}})"));check(standard->materials.at(0)->advanced.clearcoat==.6f&&standard->materials.at(0)->advanced.sheenColor.z==.8f&&standard->materials.at(0)->advanced.anisotropy==.5f,"Standard glTF advanced extensions map without sidecar");
    const std::string advancedMaps=R"("extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":0.8,"clearcoatRoughnessFactor":0.4,"clearcoatTexture":{"index":0},"clearcoatRoughnessTexture":{"index":0},"clearcoatNormalTexture":{"index":0,"scale":0.6}},"KHR_materials_transmission":{"transmissionFactor":0.7,"transmissionTexture":{"index":0}},"KHR_materials_volume":{"thicknessFactor":0.3,"thicknessTexture":{"index":0}},"KHR_materials_sheen":{"sheenColorFactor":[0.2,0.4,0.8],"sheenRoughnessFactor":0.5,"sheenColorTexture":{"index":0},"sheenRoughnessTexture":{"index":0}},"KHR_materials_anisotropy":{"anisotropyStrength":0.6,"anisotropyTexture":{"index":0}},"KHR_materials_specular":{"specularFactor":0.8,"specularTexture":{"index":0}},"KHR_materials_diffuse_transmission":{"diffuseTransmissionFactor":0.4,"diffuseTransmissionColorFactor":[1,0.4,0.2],"diffuseTransmissionTexture":{"index":0},"diffuseTransmissionColorTexture":{"index":0}}})";
    auto mappedBytes=glb({},false,false,advancedMaps);auto mapped=GlbImporter::loadMemory(mappedBytes);const auto& maps=mapped->materials.at(0)->advanced;
    check(maps.surfaceTexture&&maps.coatNormalTexture&&maps.colorTexture&&maps.anisotropyTexture,"All four spatial advanced map groups imported");
    const auto& mask=maps.surfaceTexture->pixels;check(mask[0]==0&&mask[4]==255&&mask[1]==64&&mask[5]==192&&mask[2]==0&&mask[6]==255&&mask[3]==64&&mask[7]==192,"Packed coat/roughness/transmission/thickness retain spatial channels");
    check(maps.clearcoatNormalScale==.6f&&maps.coatNormalTexture->pixels[1]==64,"Clearcoat second normal map and scale preserved");
    check(maps.colorTexture->height==4&&maps.colorTexture->pixels[3]==32&&maps.colorTexture->pixels[7]==224&&maps.colorTexture->pixels[19]==0,"Sheen alpha roughness and subsurface alpha strength occupy independent bands");
    check(maps.anisotropyTexture->pixels[2]==128&&maps.anisotropyTexture->pixels[3]==32&&maps.anisotropyTexture->pixels[7]==224,"Anisotropy strength and specular alpha retain source data");
    auto mapWorker=std::get<std::shared_ptr<const Mesh>>(hydrateDecodedAsset(decodeAssetBytes(mappedBytes,AssetDecodeKind::Glb),AssetDecodeKind::Glb));check(mapWorker->materials.at(0)->advanced.surfaceTexture->pixels==mask&&mapWorker->materials.at(0)->advanced.colorTexture->pixels==maps.colorTexture->pixels,"Spatial maps survive browser/native decode transport");
    auto different=advancedMaps;const std::string directCoat="\"clearcoatTexture\":{\"index\":0}";different.replace(different.find(directCoat),directCoat.size(),"\"clearcoatTexture\":{\"index\":0,\"texCoord\":1}");auto mappedAtlas=GlbImporter::loadMemory(glb({},false,true,different));const auto& rebaked=mappedAtlas->materials.at(mappedAtlas->parts.at(0).materialIndex)->advanced;
    check(rebaked.surfaceTexture&&rebaked.coatNormalTexture&&rebaked.colorTexture&&rebaked.anisotropyTexture&&mappedAtlas->skin.size()==mappedAtlas->vertices.size(),"Differently mapped advanced masks rebake with skeletal streams");
    auto atlas=GlbImporter::loadMemory(glb(manifest,true,true));const auto& a=*atlas->materials.at(atlas->parts.at(0).materialIndex);
    check(a.baseColorTexture&&a.normalTexture&&a.baseColorTexture!=a.normalTexture,"Independent maps both retained in separate atlases");
    check(a.baseColorTexture->wrapU==TextureWrap::Clamp&&a.advanced.uvSpeed.x==0,"Atlas sampling and frozen incompatible UV animation are explicit");
    check(atlas->vertices.size()==3&&atlas->skin.size()==3&&atlas->skeleton&&atlas->morphTargets.size()==1&&atlas->morphTargets[0].positions.size()==3,"Atlas conversion preserves skeletal and morph streams");
    bool foundMorph=false;for(auto p:atlas->morphTargets[0].positions)if(std::abs(p.z-.2f)<.0001f)foundMorph=true;check(foundMorph,"Atlas vertex duplication preserves morph offsets");
    GlbImportOptions limited;limited.maxTextureBytes=100;rejects([&]{GlbImporter::loadMemory(glb({},true),limited);},"Atlas respects aggregate texture budget");
#ifndef __EMSCRIPTEN__
    for(int i=1;i<argc;++i){auto real=GlbImporter::loadFile(std::filesystem::path(argv[i]));check(!real->parts.empty(),"Exported bundle has mesh parts");std::size_t mapCount=0;for(const auto& m:real->materials)mapCount+=bool(m->advanced.surfaceTexture)+bool(m->advanced.coatNormalTexture)+bool(m->advanced.colorTexture)+bool(m->advanced.anisotropyTexture);std::ifstream file(argv[i],std::ios::binary|std::ios::ate);check(bool(file),"Real artifact can be reopened");std::vector<std::byte> source(std::size_t(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(source.data()),std::streamsize(source.size()));auto transported=std::get<std::shared_ptr<const Mesh>>(hydrateDecodedAsset(decodeAssetBytes(source,AssetDecodeKind::Glb),AssetDecodeKind::Glb));check(transported->materials.size()==real->materials.size(),"Real artifact material table survives worker transport");for(std::size_t k=0;k<real->materials.size();++k)check(materialAdvancedParameters(real->materials[k]->advanced)==materialAdvancedParameters(transported->materials[k]->advanced),"Real artifact advanced factors/maps survive worker transport");std::cout<<"Imported artifact "<<i<<": "<<real->vertices.size()<<" vertices, "<<real->materials.size()<<" materials, "<<real->skin.size()<<" skin entries, "<<mapCount<<" advanced maps, "<<real->warnings.size()<<" warnings\n";}
#else
    (void)argc;(void)argv;
#endif
    std::cout<<"Material delivery checks: "<<checks<<"\n";return 0;
}catch(const std::exception& error){std::cerr<<"Material delivery failure after "<<checks<<": "<<error.what()<<"\n";return 1;}}
