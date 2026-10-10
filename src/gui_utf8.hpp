#pragma once
#include <cstdint>
#include <string_view>
#include <stdexcept>

namespace engine::gui_detail {
inline bool continuation(unsigned char c) { return (c&0xc0)==0x80; }
inline std::uint32_t next(std::string_view value,std::size_t& offset) {
    const auto c=static_cast<unsigned char>(value[offset++]);
    if(c<128) return c;
    const int count=c>=0xf0 ? 3 : c>=0xe0 ? 2 : c>=0xc2 ? 1 : 0;
    if(!count || c>0xf4 || offset+count>value.size()) throw std::invalid_argument("Invalid GUI UTF-8");
    std::uint32_t result=c&((1u<<(6-count))-1);
    for(int i=0;i<count;++i) {
        const auto b=static_cast<unsigned char>(value[offset++]);
        if(!continuation(b)) throw std::invalid_argument("Invalid GUI UTF-8");
        result=(result<<6)|(b&63);
    }
    if(result<(count==1 ? 128u : count==2 ? 2048u : 65536u) || result>0x10ffff || (result>=0xd800&&result<=0xdfff))
        throw std::invalid_argument("Invalid GUI UTF-8");
    return result;
}
inline void validate(std::string_view value) {
    for(std::size_t offset=0;offset<value.size();) next(value,offset);
}
inline std::size_t boundary(std::string_view value,std::size_t offset) {
    offset=std::min(offset,value.size());
    while(offset>0 && offset<value.size() && continuation(static_cast<unsigned char>(value[offset]))) --offset;
    return offset;
}
}
