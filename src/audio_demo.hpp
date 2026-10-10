#pragma once
#include "engine/scene.hpp"
#include <span>
// Application example, deliberately outside engine_core.
void audioDemoCommand(engine::Scene&,int command,float value);
bool audioDemoLoad(std::uint32_t rate,std::uint32_t channels,std::span<const float> samples);
void updateAudioDemo(const engine::Scene&,double seconds);
void suspendAudioDemo(bool value);
void shutdownAudioDemo();
