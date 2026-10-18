#pragma once
#include "engine/effect_component.hpp"
namespace engine {
// Small procedural example assets; production effects can use imported PNG flipbooks.
std::shared_ptr<const EffectAsset> campfireEffect();
std::shared_ptr<const EffectAsset> explosionEffect();
std::shared_ptr<const EffectAsset> fountainEffect();
std::shared_ptr<const EffectAsset> ribbonEffect();
std::shared_ptr<const EffectAsset> groundRingEffect();
std::shared_ptr<const EffectAsset> debrisEffect();
}
