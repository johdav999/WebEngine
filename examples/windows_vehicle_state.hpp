#pragma once
#include "engine/persistence.hpp"
#include "engine/gui.hpp"
#include "engine/desktop_services.hpp"
#include <bit>

namespace vehicle_example {
struct DriverState {
    std::string name="Driver";
    float resolution=1,volume=.5f;
    bool shadows=true,msaa=false,fullscreen=false;
    engine::Transform checkpoint;
};
inline DriverState decodeDriverState(std::span<const std::byte> bytes) {
    std::size_t at=0;auto integer=[&] {
        if(bytes.size()-at<4)throw std::invalid_argument("Truncated vehicle state");
        std::uint32_t value=0;for(int i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[at++])<<(i*8);return value;
    };
    const auto size=integer();if(size>32768||size>bytes.size()-at)throw std::invalid_argument("Invalid saved driver name");
    DriverState state;state.name.assign(reinterpret_cast<const char*>(bytes.data()+at),size);at+=size;
    engine::TextInput validation;validation.setText(state.name);
    if(validation.text()!=state.name)throw std::invalid_argument("Driver name must be single-line");
    for(unsigned char c:state.name)if(c<32||c==127)throw std::invalid_argument("Invalid saved driver name");
    auto number=[&]{const float value=std::bit_cast<float>(integer());if(!std::isfinite(value))throw std::invalid_argument("Invalid saved vehicle number");return value;};
    state.resolution=number();state.volume=number();const auto flags=integer();
    if(state.resolution<.5f||state.resolution>1||state.volume<0||state.volume>1||flags>7)throw std::invalid_argument("Invalid vehicle settings");
    state.shadows=(flags&1)!=0;state.msaa=(flags&2)!=0;state.fullscreen=(flags&4)!=0;
    state.checkpoint.position={number(),number(),number()};state.checkpoint.rotation={number(),number(),number()};
    for(auto value:{state.checkpoint.position.x,state.checkpoint.position.y,state.checkpoint.position.z,state.checkpoint.rotation.x,state.checkpoint.rotation.y,state.checkpoint.rotation.z})
        if(std::abs(value)>100000)throw std::invalid_argument("Saved vehicle transform exceeds limits");
    if(at!=bytes.size())throw std::invalid_argument("Trailing vehicle state");return state;
}
inline std::vector<std::byte> encodeDriverState(const DriverState& state) {
    std::vector<std::byte> bytes;auto integer=[&](std::uint32_t value){for(int i=0;i<4;++i)bytes.push_back(std::byte((value>>(i*8))&255));};
    integer(std::uint32_t(state.name.size()));for(unsigned char c:state.name)bytes.push_back(std::byte(c));
    auto number=[&](float v){integer(std::bit_cast<std::uint32_t>(v));};number(state.resolution);number(state.volume);
    integer((state.shadows?1u:0u)|(state.msaa?2u:0u)|(state.fullscreen?4u:0u));
    for(float v:{state.checkpoint.position.x,state.checkpoint.position.y,state.checkpoint.position.z,state.checkpoint.rotation.x,state.checkpoint.rotation.y,state.checkpoint.rotation.z})number(v);
    (void)decodeDriverState(bytes);return bytes;
}
inline engine::ApplicationStateCodec driverStateCodec() {
    return {"webengine.windows.vehicle",1,[](auto bytes){(void)decodeDriverState(bytes);},{}};
}
inline DriverState loadDriverStateArchive(std::span<const std::byte> bytes) {
    engine::ArchiveLimits limits;limits.maxBytes=65536;limits.maxAllocatedBytes=1024*1024;limits.maxNodes=128;limits.maxResources=64;limits.maxElements=65536;limits.maxDepth=16;
    const auto document=engine::ApplicationArchive::load(bytes,driverStateCodec(),limits);
    if(!document.scenes.empty()||!document.activeScene.empty())throw std::invalid_argument("Expected state-only vehicle checkpoint");
    return decodeDriverState(document.state);
}
inline engine::DesktopFileSettings vehicleFileSettings() {engine::DesktopFileSettings settings;settings.application="WindowsVehicle";return settings;}
}
