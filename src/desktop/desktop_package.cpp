#include "engine/desktop_runtime.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <cwctype>
#include <array>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace engine {
namespace {
std::string digest(const std::filesystem::path& path){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    struct Guard{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Guard(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}}guard{algorithm,hash};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA-256 initialization failed");
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read package file");std::array<unsigned char,65536> bytes;
    while(stream){stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size());const auto n=stream.gcount();if(n&&BCryptHashData(hash,bytes.data(),ULONG(n),0)<0)throw std::runtime_error("SHA-256 update failed");}
    if(!stream.eof())throw std::runtime_error("Package file read failed");std::array<unsigned char,32> result{};if(BCryptFinishHash(hash,result.data(),ULONG(result.size()),0)<0)throw std::runtime_error("SHA-256 finalization failed");
    constexpr char digits[]="0123456789abcdef";std::string text;for(auto c:result){text+=digits[c>>4];text+=digits[c&15];}return text;
}
}
DesktopPackageReport inspectDesktopPackage(const std::filesystem::path& directory){
    DesktopPackageReport report;
    try{
        const auto root=std::filesystem::weakly_canonical(directory);const auto manifest=root/L"package.manifest";
        if(!std::filesystem::is_regular_file(manifest)||std::filesystem::file_size(manifest)>1024*1024)throw std::runtime_error("Missing or oversized package.manifest; extract the complete distribution ZIP");
        std::ifstream in(manifest,std::ios::binary);std::string line;std::getline(in,line);if(line!="WEBENGINE_PACKAGE 1")throw std::runtime_error("Unsupported package manifest");
        std::getline(in,line);if(!line.starts_with("version\t")||line.size()>160)throw std::runtime_error("Invalid package version");report.version=line.substr(8);
        std::set<std::wstring> seen;std::size_t count=0;
        while(std::getline(in,line)){
            if(line.empty())continue;if(++count>4096||line.size()>4096)throw std::runtime_error("Package manifest exceeds limits");
            const auto a=line.find('\t'),b=a==line.npos?a:line.find('\t',a+1);
            if(a!=64||b==line.npos||line.find_first_not_of("0123456789abcdef",0)<64)throw std::runtime_error("Invalid package checksum record");
            const auto sizeText=line.substr(a+1,b-a-1);if(sizeText.empty()||sizeText.find_first_not_of("0123456789")!=sizeText.npos)throw std::runtime_error("Invalid package size");
            const auto expectedSize=std::stoull(sizeText);if(expectedSize>1024ull*1024*1024)throw std::runtime_error("Package member exceeds limit");
            const auto name=line.substr(b+1);const std::filesystem::path relative(std::u8string(name.begin(),name.end()));
            if(relative.empty()||relative.is_absolute()||relative.has_root_name())throw std::runtime_error("Invalid package member path");
            for(const auto& part:relative){const auto p=part.native();if(p.empty()||p==L"."||p==L".."||p.back()=='.'||p.back()==' '||p.find_first_of(L":*?\"<>|\r\n\t")!=p.npos)throw std::runtime_error("Unsafe package member path");}
            auto identity=relative.native();for(auto& c:identity)c=std::towlower(c);if(!seen.insert(identity).second)throw std::runtime_error("Duplicate package member");
            const auto path=std::filesystem::weakly_canonical(root/relative);auto at=path.begin();for(const auto& part:root){if(at==path.end()||*at++!=part)throw std::runtime_error("Package member escapes distribution root");}
            if(!std::filesystem::is_regular_file(path)){report.errors.push_back("Missing package file: "+name);continue;}
            if(std::filesystem::file_size(path)!=expectedSize||digest(path)!=line.substr(0,64)){report.errors.push_back("Damaged package file: "+name+"; reinstall from the complete ZIP");continue;}++report.verifiedFiles;
        }
        if(!count)throw std::runtime_error("Empty package manifest");
    }catch(const std::exception& e){report.errors.push_back(e.what());}return report;
}
}
