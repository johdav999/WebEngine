#include "engine/material_graph.hpp"
#include <array>
#include <iostream>

using namespace engine;
int main() {
    try {
        auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        Material source;source.name="Imported material";source.baseColorTexture=Texture::white();source.normalTexture=Texture::flatNormal();
        std::array<float,19> values={0.4f,0.5f,0.6f,0.3f,0.8f,1,1,4,2,1,0.7f,2,0.5f,1,0,1,1,0,0};
        const auto constant=bindMaterialGraph(source,values,{},64,0);
        check(constant.baseColorTexture==source.baseColorTexture&&constant.normalTexture==source.normalTexture,"Constant graph must inherit imported maps");
        check(constant.emissive.x==4&&constant.roughness==0.3f&&constant.alphaMode==AlphaMode::Blend,"Graph must retain HDR and alpha parameters");
        std::vector<std::uint8_t> pixels(64*64*4*2,255);pixels[0]=12;pixels[64*64*4]=128;
        const auto mapped=bindMaterialGraph(source,values,pixels,64,5);
        check(mapped.baseColorTexture->pixels[0]==12&&mapped.normalTexture->pixels[0]==128,"Packed maps must bind in ascending slot order");
        check(mapped.baseColorTexture->width==64&&mapped.baseColorTexture->pixels.size()==64*64*4,"Compiled map dimensions must match payload");
        auto reject=[&](auto function){bool rejected=false;try{function();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid graph must be rejected");};
        reject([&]{bindMaterialGraph(source,values,pixels,64,1);});reject([&]{bindMaterialGraph(source,values,{},512,0);});
        reject([&]{bindMaterialGraph(source,values,{},64,32);});reject([&]{bindMaterialGraph(source,std::span(values).first(18),{},64,0);});
        values[11]=0.5f;reject([&]{bindMaterialGraph(source,values,{},64,0);});values[11]=0;values[3]=2;reject([&]{bindMaterialGraph(source,values,{},64,0);});
        check(source.baseColorTexture==Texture::white()&&source.roughness==0.6f,"Failed graph builds must preserve source assets");
        // Rebuilding a constant graph from the original source restores maps after a procedural branch is disconnected.
        values[3]=0.3f;
        check(bindMaterialGraph(source,values,{},64,0).baseColorTexture==source.baseColorTexture,"Recompilation must restore source maps");
        std::cout<<"Material graph binding checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
