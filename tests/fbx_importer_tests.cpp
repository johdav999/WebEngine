#include "engine/fbx_importer.hpp"
#include "engine/mesh_component.hpp"
#include "engine/scene.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace engine;
namespace {
int failures=0;
void check(bool condition,const char* description) {
    if(!condition) { ++failures; std::cerr<<"FAIL: "<<description<<'\n'; }
}
bool close(float a,float b) { return std::abs(a-b)<0.0001f; }
bool close(Vec3 a,Vec3 b) { return close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z); }
std::vector<std::byte> read(const char* name) {
    std::ifstream file(std::filesystem::path(FBX_FIXTURE_DIR)/name,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("Missing FBX fixture");
    std::vector<std::byte> data(static_cast<std::size_t>(file.tellg())); file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));
    return data;
}
template<class F> void rejects(F&& operation,const char* description) {
    bool rejected=false;
    try { operation(); } catch(const std::exception&) { rejected=true; }
    check(rejected,description);
}
void checkGeometry(const Mesh& mesh) {
    check(!mesh.indices.empty() && mesh.indices.size()%3==0,"Imported mesh has indexed triangles");
    for(const auto& v:mesh.vertices) check(close(dot(v.normal,v.normal),1),"Imported normals are unit vectors");
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto& a=mesh.vertices.at(mesh.indices[i]);
        const auto& b=mesh.vertices.at(mesh.indices[i+1]);
        const auto& c=mesh.vertices.at(mesh.indices[i+2]);
        check(dot(cross(b.position-a.position,c.position-a.position),a.normal)>0,"Winding matches transformed normals, including mirrored instances");
    }
}
}
int main() {
    try {
        const auto ascii=read("transformed_instances_ascii.fbx");
        const auto mesh=FbxImporter::loadMemory(ascii);
        check(mesh->vertices.size()==11 && mesh->indices.size()==15,"Quad instances and triangle merge into one triangulated mesh");
        for(Vec3 position: {Vec3{-0.8f,2,3.5f},Vec3{3.2f,2,2.5f},Vec3{5,2,2},Vec3{7,2,4},Vec3{0,0,0},Vec3{0,1,0}}) {
            bool found=false; for(const auto& v:mesh->vertices) found |= close(v.position,position);
            check(found,"Centimeter units, parent transforms, geometry offsets, and instance scales are baked correctly");
        }
        checkGeometry(*mesh);
        const auto binary=read("blender_cube_binary.fbx");
        const auto binaryMesh=FbxImporter::loadMemory(binary);
        check(binaryMesh->indices.size()==36,"Binary Blender cube imports as twelve triangles");
        checkGeometry(*binaryMesh);
        MeshComponent component{"Imported",Mesh::cube()};
        component.transform.position={4,5,6}; component.color={0.2f,0.3f,0.4f}; component.layer=RenderLayer::Overlay;
        component.loadFbx(ascii);
        check(component.mesh->indices.size()==15,"Memory importer populates the mesh component");
        check(close(component.transform.position,{4,5,6}) && close(component.color,{0.2f,0.3f,0.4f}) && component.layer==RenderLayer::Overlay,
            "Import preserves component transform, color, and render layer");
        const auto previous=component.mesh;
        const std::string invalid="not an FBX file";
        const auto invalidBytes=std::as_bytes(std::span(invalid));
        rejects([&]{component.loadFbx(invalidBytes);},"Malformed input is rejected");
        check(component.mesh==previous,"Failed import preserves previous mesh");
        rejects([&]{FbxImporter::loadMemory({});},"Empty input is rejected");
        rejects([&]{FbxImporter::loadMemory(std::span(binary).first(30));},"Truncated binary input is rejected");
        const std::string obj="v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        rejects([&]{FbxImporter::loadMemory(std::as_bytes(std::span(obj)));},"Non-FBX geometry formats are rejected");
        std::string brokenIndex(reinterpret_cast<const char*>(ascii.data()),ascii.size());
        brokenIndex.replace(brokenIndex.find("a: 0,1,2,-4"),std::string("a: 0,1,2,-4").size(),"a: 0,9999,2,-4");
        rejects([&]{FbxImporter::loadMemory(std::as_bytes(std::span(brokenIndex)));},"Broken polygon indices are rejected");
        std::string singular(reinterpret_cast<const char*>(ascii.data()),ascii.size());
        singular.replace(singular.find("\"A\",-1,2,1"),std::string("\"A\",-1,2,1").size(),"\"A\",0,2,1");
        rejects([&]{FbxImporter::loadMemory(std::as_bytes(std::span(singular)));},"Singular mesh instance transforms are rejected");
        FbxImportOptions options;
        options.maxFileBytes=ascii.size()-1;
        rejects([&]{FbxImporter::loadMemory(ascii,options);},"File byte limit is enforced");
        options={}; options.maxVertices=10;
        rejects([&]{FbxImporter::loadMemory(ascii,options);},"Merged vertex limit is enforced");
        options={}; options.maxIndices=14;
        rejects([&]{FbxImporter::loadMemory(ascii,options);},"Merged triangle limit is enforced");
        options={}; options.parserMemoryBytes=64;
        rejects([&]{FbxImporter::loadMemory(ascii,options);},"Parser memory budget is enforced");
        options={}; options.maxIndices=0;
        rejects([&]{FbxImporter::loadMemory(ascii,options);},"Zero import limits are rejected");
        std::string emptyScene="; FBX 7.4.0 project file\nFBXHeaderExtension: {\nFBXVersion: 7400\n}\nObjects: {\n}\nConnections: {\n}\n";
        rejects([&]{FbxImporter::loadMemory(std::as_bytes(std::span(emptyScene)));},"Mesh-free FBX is rejected");
        // Verify coordinate conversion from Z-up while preserving the source geometry's facing.
        std::string zUp(reinterpret_cast<const char*>(ascii.data()),ascii.size());
        auto replace=[&](const std::string& before,const std::string& after) { zUp.replace(zUp.find(before),before.size(),after); };
        replace("\"UpAxis\", \"int\", \"Integer\", \"\",1","\"UpAxis\", \"int\", \"Integer\", \"\",2");
        replace("\"FrontAxis\", \"int\", \"Integer\", \"\",2","\"FrontAxis\", \"int\", \"Integer\", \"\",1");
        replace("\"FrontAxisSign\", \"int\", \"Integer\", \"\",1","\"FrontAxisSign\", \"int\", \"Integer\", \"\",-1");
        const auto converted=FbxImporter::loadMemory(std::as_bytes(std::span(zUp)));
        bool zConverted=false;
        for(const auto& v:converted->vertices) zConverted |= close(v.position,{7,4,-2});
        check(zConverted,"Z-up input is converted to right-handed Y-up meters");
        checkGeometry(*converted);
        component.loadFbxFile(std::filesystem::path(FBX_FIXTURE_DIR)/"blender_cube_binary.fbx");
        check(component.mesh->indices.size()==36,"Native file importer populates the component");
        rejects([&]{component.loadFbxFile(std::filesystem::path(FBX_FIXTURE_DIR)/"missing.fbx");},"Missing file reports an error");
        check(component.mesh->indices.size()==36,"Missing-file import preserves component geometry");
        const auto imageBytes=read("uv-colors.png");
        const auto image=Texture::loadMemory(imageBytes,"Orientation fixture");
        check(image->width==2 && image->height==2 && image->pixels.size()==16,"PNG decodes to RGBA8");
        check(image->pixels[0]==255 && image->pixels[1]==0 && image->pixels[8]==0 && image->pixels[10]==255,"Decoder retains top-down red and blue rows");
        component.loadTexture(imageBytes); const auto oldTexture=component.texture;
        rejects([&]{component.loadTexture(invalidBytes);},"Malformed texture is rejected");
        check(component.texture==oldTexture,"Failed texture load preserves component texture");
        TextureLimits imageLimits; imageLimits.maxDimension=1;
        rejects([&]{Texture::loadMemory(imageBytes,{},imageLimits);},"Image dimension limit is enforced before decoding");
        imageLimits={}; imageLimits.maxDecodedBytes=15;
        rejects([&]{Texture::loadMemory(imageBytes,{},imageLimits);},"Decoded image byte limit is enforced");
        const auto embedded=read("textured_embedded_ascii.fbx");
        const auto textured=FbxImporter::loadMemory(embedded);
        check(textured->parts.size()==2 && textured->materials.size()==2 && textured->indices.size()==12,"FBX face materials become separate triangle ranges");
        check(textured->materials[0]->baseColorTexture && textured->materials[0]->baseColorTexture==textured->materials[1]->baseColorTexture,"Two FBX materials share one decoded embedded texture");
        check(textured->warnings.empty(),"Embedded base-color texture imports without missing-image warnings");
        if(textured->materials[0]->baseColorTexture) check(textured->materials[0]->baseColorTexture->wrapU==TextureWrap::Clamp && textured->materials[0]->baseColorTexture->wrapV==TextureWrap::Repeat,"FBX sampler wrapping is retained");
        bool leftBottom=false,seam0=false,seam1=false;
        for(const auto& v:textured->vertices) {
            if(close(v.position,{-1,0,0})) leftBottom=close(v.uv.x,0)&&close(v.uv.y,1);
            if(close(v.position,{0,0,0})) { seam0|=close(v.uv.x,0);seam1|=close(v.uv.x,1); }
        }
        check(leftBottom && seam0 && seam1,"FBX UVs flip V once and preserve corner seams at shared positions");
        std::string transformedUV(reinterpret_cast<const char*>(embedded.data()),embedded.size());
        const std::string wrapProperty="P: \"WrapModeU\"";
        transformedUV.insert(transformedUV.find(wrapProperty),"P: \"Scaling\", \"Vector3D\", \"Vector\", \"\",2,4,1\n   ");
        const auto uvScaled=FbxImporter::loadMemory(std::as_bytes(std::span(transformedUV)));
        bool scaled=false;
        for(const auto& v:uvScaled->vertices) if(close(v.position,{0,2,0}) && close(v.uv.x,0.5f) && close(v.uv.y,0.75f)) scaled=true;
        check(scaled,"FBX UV-to-texture transform is baked before V conversion");
        std::string namedUV(reinterpret_cast<const char*>(embedded.data()),embedded.size());
        const std::string uvName="Name: \"UVMap\"";
        namedUV.replace(namedUV.find(uvName),uvName.size(),"Name: \"Ignored\"");
        const std::string secondSet=R"fbx(
  LayerElementUV: 1 {
   Name: "UVMap"
   MappingInformationType: "ByPolygonVertex"
   ReferenceInformationType: "Direct"
   UV: *16 { a: 0.25,0.75,1,0,1,1,0,1,0,0,1,0,1,1,0,1 }
  }
  Layer: 1 { LayerElement: { Type: "LayerElementUV" TypedIndex: 1 } }
)fbx";
        namedUV.insert(namedUV.find("LayerElementMaterial:"),secondSet);
        const auto uvNamed=FbxImporter::loadMemory(std::as_bytes(std::span(namedUV)));
        bool selected=false;
        for(const auto& v:uvNamed->vertices) if(close(v.position,{-1,0,0}) && close(v.uv.x,0.25f) && close(v.uv.y,0.25f)) selected=true;
        check(selected,"Named texture UV set selects the second mesh UV layer");
        Scene materialScene; auto& materialComponent=materialScene.addChild<MeshComponent>("Materials",textured); materialComponent.color={1,1,1};
        auto materialFrame=materialScene.collect();
        check(materialFrame.meshes.size()==2 && materialFrame.meshCount==1 && materialFrame.meshes[0].indexCount==6 && materialFrame.meshes[1].firstIndex==6,"Scene submits both FBX material ranges and counts one component");
        check(close(materialFrame.meshes[1].color,{0.5f,1,1}) && materialFrame.meshes[0].texture==materialFrame.meshes[1].texture,"Scene preserves material tint and shared textures");
        materialComponent.texture=Texture::white();materialFrame=materialScene.collect();
        check(materialFrame.meshes[0].texture==Texture::white() && materialFrame.meshes[1].texture==Texture::white(),"Component texture overrides all imported material textures");
        const auto external=read("textured_external_ascii.fbx");
        const auto missing=FbxImporter::loadMemory(external);
        check(!missing->warnings.empty() && !missing->materials[0]->baseColorTexture && missing->materials[0]->texturePath=="Textures/uv-colors.png","Missing external image retains path and warns with color fallback");
        options={}; int resolutions=0;
        options.resolveTexture=[&](std::string_view name){check(name=="Textures/uv-colors.png","Resolver receives the FBX relative texture path");++resolutions;return imageBytes;};
        const auto resolved=FbxImporter::loadMemory(external,options);
        check(resolutions==1 && resolved->materials[0]->baseColorTexture && resolved->warnings.empty(),"Explicit external resolver is used once per shared texture");
        const auto fromFile=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"textured_external_ascii.fbx");
        check(fromFile->materials[0]->baseColorTexture!=nullptr,"Native importer resolves a relocated companion by basename");
        options={}; options.maxTextureBytes=15;
        rejects([&]{FbxImporter::loadMemory(embedded,options);},"Total imported texture budget is enforced");
        options={}; options.resolveTexture=[&](std::string_view){return std::vector<std::byte>(invalidBytes.begin(),invalidBytes.end());};
        const auto oldMesh=component.mesh;
        rejects([&]{component.loadFbx(external,options);},"Corrupt resolved texture fails the import");
        check(component.mesh==oldMesh && component.texture==oldTexture,"Failed textured import preserves both component resources");
        FbxImportMeasurements importMetrics;options={};options.measurements=&importMetrics;
        const auto knarr=FbxImporter::loadFile(std::filesystem::path(FBX_FIXTURE_DIR)/"Knarr.fbx",options);
        check(importMetrics.parseMs>0 && importMetrics.textureDecodeMs>0 && importMetrics.convertMs>0 && importMetrics.totalMs>=importMetrics.parseMs+importMetrics.textureDecodeMs,"Import measurements separate parse, image decode, conversion and total work");
        check(importMetrics.decodedTextures==21 && importMetrics.decodedTextureBytes==117440512,"Import measurements count shared base/PBR maps once and include packed map memory");
        rejects([&]{FbxImporter::loadMemory(invalidBytes,options);},"Timed malformed import reports failure");
        check(importMetrics.totalMs>=0 && importMetrics.decodedTextures==0,"Failed import clears stale asset measurements");
        check(knarr->indices.size()==122868 && knarr->materials.size()==7 && knarr->parts.size()==7,"Knarr keeps full geometry and seven material batches");
        check(knarr->warnings.empty(),"Knarr embedded textures resolve without warnings");
        for(const auto& material:knarr->materials) check(material->baseColorTexture && material->baseColorTexture->pixels.size()>0,"Every Knarr material has decoded base-color pixels");
    } catch(const std::exception& error) { ++failures; std::cerr<<"Unexpected failure: "<<error.what()<<'\n'; }
    if(failures) return 1;
    std::cout<<"FBX importer tests passed\n";
}
