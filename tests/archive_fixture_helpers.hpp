#pragma once
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <vector>
#include <cstddef>
#include <cstdint>
inline void stripDefaultCameraFollow(std::vector<std::byte>& bytes){
    std::vector<std::byte> fields={std::byte{0},std::byte{1},std::byte{1},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
    for(float value:{0.f,1.f,0.f,0.f,3.f,-8.f,.3f,4.f,.12f,.1f,.3f,.05f}){auto word=std::bit_cast<std::uint32_t>(value);for(int i=0;i<4;++i)fields.push_back(std::byte((word>>(i*8))&255));}fields.push_back(std::byte{255});fields.push_back(std::byte{255});
    const auto found=std::search(bytes.begin()+24,bytes.end(),fields.begin(),fields.end());if(found==bytes.end())throw std::runtime_error("Default v26 camera follow fixture missing");bytes.erase(found,found+fields.size());
}
inline void repairFixture(std::vector<std::byte>& bytes,unsigned version){bytes[8]=std::byte(version);const auto size=std::uint64_t(bytes.size()-24);for(int i=0;i<8;++i)bytes[12+i]=std::byte((size>>(i*8))&255);std::uint32_t crc=0xffffffff;for(std::size_t i=24;i<bytes.size();++i){crc^=std::to_integer<unsigned>(bytes[i]);for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}crc=~crc;for(int i=0;i<4;++i)bytes[20+i]=std::byte((crc>>(i*8))&255);}
