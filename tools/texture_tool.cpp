#include "engine/texture_bundle.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace engine;
std::vector<std::byte> readFile(const char* path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in||in.tellg()<=0||in.tellg()>32*1024*1024)throw std::runtime_error("Input is missing, empty or exceeds 32 MiB");
    std::vector<std::byte> b(static_cast<std::size_t>(in.tellg()));in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(b.data()),b.size()))throw std::runtime_error("Input read failed");return b;
}
TextureSemantic semantic(const std::string& s) {
    if(s=="color")return TextureSemantic::Color;if(s=="data")return TextureSemantic::Data;if(s=="normal")return TextureSemantic::Normal;
    throw std::runtime_error("Semantic must be color, data or normal");
}
void save(const std::filesystem::path& p,std::span<const std::byte> b) {
    // Validate before writing, stage bytes, then publish into a new path.
    if(std::filesystem::exists(p))throw std::runtime_error("Output already exists");
    auto staged=p;staged += ".partial";
    if(std::filesystem::exists(staged))throw std::runtime_error("Staged output already exists");
    try {
        { std::ofstream out(staged,std::ios::binary);if(!out||!out.write(reinterpret_cast<const char*>(b.data()),b.size()))throw std::runtime_error("Output write failed");out.close();if(!out)throw std::runtime_error("Output close failed"); }
        std::filesystem::rename(staged,p);
    }catch(...){std::error_code ignored;std::filesystem::remove(staged,ignored);throw;}
}
int main(int argc,char** argv) {
    try {
        if(argc>=6&&std::string(argv[1])=="pack"&&(argc-4)%2==0) {
            const auto fallback=readFile(argv[2]);std::vector<TextureVariantSource> variants;
            for(int i=4;i<argc;i+=2)variants.push_back({semantic(argv[i]),readFile(argv[i+1])});
            const auto bundle=packTextureBundle(fallback,variants);save(argv[3],bundle);
            std::cout<<"Validated texture bundle: "<<bundle.size()<<" bytes\n";return 0;
        }
        if(argc==5&&std::string(argv[1])=="mips") {
            const auto texture=Texture::loadMemory(readFile(argv[2]));const auto levels=texture->mipChain(semantic(argv[4]));
            const std::filesystem::path folder=argv[3];if(std::filesystem::exists(folder))throw std::runtime_error("Output directory already exists");
            std::filesystem::create_directories(folder);
            for(std::size_t i=0;i<levels.size();++i) {
                const auto& mip=levels[i];std::vector<std::byte> tga(18);tga[2]=std::byte{2};tga[12]=std::byte(mip.width&255);tga[13]=std::byte(mip.width>>8);
                tga[14]=std::byte(mip.height&255);tga[15]=std::byte(mip.height>>8);tga[16]=std::byte{32};tga[17]=std::byte{40};
                for(std::size_t p=0;p<mip.pixels.size();p+=4)for(auto c:{2,1,0,3})tga.push_back(std::byte(mip.pixels[p+c]));
                save(folder/("mip-"+std::to_string(i)+".tga"),tga);
            }
            std::cout<<"Exported "<<levels.size()<<" semantic mips\n";return 0;
        }
        std::cerr<<"texture_tool mips input.png new-directory color|data|normal\ntexture_tool pack fallback.png output.wetex color|data|normal variant.ktx2 [semantic variant.ktx2 ...]\n";return 2;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
