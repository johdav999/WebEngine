#include "engine/scene_archive.hpp"
#ifndef __EMSCRIPTEN__
#include <atomic>
#include <chrono>
#include <fstream>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace engine {
namespace {
std::vector<std::byte> read(const std::filesystem::path& path,const ArchiveLimits& limits) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("Could not open archive file");
    const auto count=file.tellg();
    if(count<=0||std::uint64_t(count)>limits.maxBytes) throw std::invalid_argument("Archive file is empty or exceeds the size limit");
    std::vector<std::byte> bytes(static_cast<std::size_t>(count));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),count)) throw std::runtime_error("Could not read complete archive file");
    return bytes;
}
void write(const std::filesystem::path& path,std::span<const std::byte> bytes) {
    static std::atomic<std::uint64_t> serial{0};
    auto temporary=path;
    temporary+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial.fetch_add(1));
    bool created=false;
    try {
#ifdef _WIN32
        const auto handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE) throw std::system_error(GetLastError(),std::system_category(),"Could not create temporary archive");
        created=true;
        bool okay=true;
        for(std::size_t offset=0;okay&&offset<bytes.size();) {
            const auto count=DWORD(std::min<std::size_t>(bytes.size()-offset,1024*1024));DWORD written=0;
            okay=WriteFile(handle,bytes.data()+offset,count,&written,nullptr)&&written==count;offset+=written;
        }
        okay=okay&&FlushFileBuffers(handle);const auto code=GetLastError();CloseHandle(handle);
        if(!okay) throw std::system_error(code,std::system_category(),"Could not write complete archive");
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(),std::system_category(),"Could not replace archive file");
#else
        const int handle=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(handle<0) throw std::system_error(errno,std::generic_category(),"Could not create temporary archive");
        created=true;bool okay=true;
        for(std::size_t offset=0;okay&&offset<bytes.size();) {
            const auto written=::write(handle,bytes.data()+offset,bytes.size()-offset);
            if(written<0&&errno==EINTR) continue;
            okay=written>0;if(okay) offset+=static_cast<std::size_t>(written);
        }
        okay=okay&&::fsync(handle)==0;const auto code=errno;::close(handle);
        if(!okay) throw std::system_error(code,std::generic_category(),"Could not write complete archive");
        std::filesystem::rename(temporary,path);
#endif
    } catch(...) {
        if(created) { std::error_code ignored;std::filesystem::remove(temporary,ignored); }
        throw;
    }
}
}
void SceneArchive::saveFile(const std::filesystem::path& path,const Scene& scene,std::span<const RenderView> views,const AssetLibrary& assets,std::span<const std::byte> data,const ArchiveLimits& limits,const ComponentCodecs& codecs) {
    const auto bytes=save(scene,views,assets,data,limits,codecs);write(path,bytes);
}
SceneDocument SceneArchive::loadFile(const std::filesystem::path& path,const ArchiveLimits& limits,const ComponentCodecs& codecs) { const auto bytes=read(path,limits);return load(bytes,limits,codecs); }
void SceneArchive::saveAssetsFile(const std::filesystem::path& path,const AssetLibrary& assets,const ArchiveLimits& limits) { const auto bytes=saveAssets(assets,limits);write(path,bytes); }
AssetLibrary SceneArchive::loadAssetsFile(const std::filesystem::path& path,const ArchiveLimits& limits) { const auto bytes=read(path,limits);return loadAssets(bytes,limits); }
} // namespace engine
#endif
