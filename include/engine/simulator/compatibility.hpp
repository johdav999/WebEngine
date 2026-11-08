#pragma once
#include "runtime.hpp"

namespace engine::simulator {
// Stable FNV-1a over explicit little-endian words and length-prefixed UTF-8.
// A compatibility/checkpoint fingerprint, not authentication or a cryptographic hash.
class ContentHash {
    std::uint64_t value_=14695981039346656037ULL;
public:
    void bytes(std::span<const std::uint8_t> values) noexcept {for(auto b:values){value_^=b;value_*=1099511628211ULL;}}
    void word(std::uint64_t value) noexcept {for(int i=0;i<8;++i){value_^=std::uint8_t(value>>(8*i));value_*=1099511628211ULL;}}
    void text(std::string_view value) noexcept {word(value.size());for(unsigned char c:value){value_^=c;value_*=1099511628211ULL;}}
    std::uint64_t value() const noexcept {return value_;}
};
struct ContentStamp {
    std::uint32_t version=0;
    std::uint64_t fingerprint=0;
    bool operator==(const ContentStamp&) const=default;
};
inline Status checkContent(ContentStamp saved,ContentStamp current) noexcept {
    if(!saved.version || !current.version)return {{},Error::InvalidDefinition};
    if(saved.version!=current.version)return {{},Error::UnsupportedVersion};
    if(saved.fingerprint!=current.fingerprint)return {{},Error::DefinitionMismatch};
    return {};
}
inline std::uint64_t checkpointHash(std::span<const std::uint8_t> bytes) noexcept {ContentHash hash;hash.bytes(bytes);return hash.value();}
} // namespace engine::simulator
