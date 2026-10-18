#include "engine/asset_decode.hpp"
#include "engine/terrain_paging.hpp"
#include <emscripten.h>
#include <string>
namespace {
std::vector<std::byte> output;
std::string error;
}
extern "C" {
EMSCRIPTEN_KEEPALIVE const void* engine_decode_asset(const void* bytes,int size,int kind,int maxResultBytes) {
    output.clear();error.clear();
    try {
        if(size<=0||maxResultBytes<=0) throw std::invalid_argument("Invalid decode limits");
        engine::ArchiveLimits limits;limits.maxBytes=limits.maxAllocatedBytes=maxResultBytes;
        output=engine::decodeAssetBytes({static_cast<const std::byte*>(bytes),static_cast<std::size_t>(size)},static_cast<engine::AssetDecodeKind>(kind),limits);
        return output.data();
    } catch(const std::exception& e) { error=e.what();return nullptr; }
}
EMSCRIPTEN_KEEPALIVE const void* engine_build_terrain_page(const void* page,int pageBytes,const void* material,int materialBytes,int collision) {
    output.clear();error.clear();try {
        if(pageBytes<=0||pageBytes>256*1024||materialBytes<=0||materialBytes>8*1024*1024) throw std::invalid_argument("Invalid terrain worker input limits");
        output=engine::buildTerrainPageTransport({static_cast<const std::byte*>(page),std::size_t(pageBytes)},{static_cast<const std::byte*>(material),std::size_t(materialBytes)},collision!=0);return output.data();
    }catch(const std::exception& e){error=e.what();return nullptr;}
}
EMSCRIPTEN_KEEPALIVE int engine_decode_size() { return output.size(); }
EMSCRIPTEN_KEEPALIVE const char* engine_decode_error() { return error.c_str(); }
}
