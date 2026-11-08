#include "../examples/stone_arch.hpp"
#include "engine/cluster_stream.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) try {
    if(argc!=2)throw std::invalid_argument("Usage: stone_arch_cook <output directory>");
    const std::filesystem::path directory=argv[1];
    std::filesystem::create_directories(directory);
    const auto write=[&](const std::filesystem::path& name,std::span<const std::byte> bytes) {
        std::ofstream output(directory/name,std::ios::binary|std::ios::trunc);
        if(!output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size())))
            throw std::runtime_error("Cannot write "+name.string());
    };
    const auto lod=engine::ClusterLod::build(*engine::stone_arch_example::archMesh(),.065f);
    const auto manifest=engine::ClusterPageAsset::cook(*lod,[&](auto id,auto bytes) {
        write(std::to_string(id)+".wecp",bytes);
    });
    const auto asset=engine::ClusterPageAsset::open(manifest);
    write("arch.wecl",manifest);
    std::cout<<"Cooked "<<asset->pages().size()-1<<" fine pages and "<<manifest.size()<<" manifest bytes\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
