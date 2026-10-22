#pragma once
#include <chrono>
#include <string>
namespace engine::asset_detail {
inline std::uint64_t freshSerial=0;
inline std::string freshUrl(std::string url){
    const auto marker=url.find('#');const auto fragment=marker==url.npos?std::string{}:url.substr(marker);if(marker!=url.npos)url.resize(marker);
    const auto epoch=std::chrono::system_clock::now().time_since_epoch().count();
    url+=(url.find('?')==url.npos?'?':'&');url+="__webengine_reload="+std::to_string(epoch)+"-"+std::to_string(++freshSerial);return url+fragment;
}
}
