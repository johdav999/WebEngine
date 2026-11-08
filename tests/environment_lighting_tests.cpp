#include "engine/scene.hpp"
#include <iostream>
#include <limits>

using namespace engine;

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool near(float a, float b) {
    return std::abs(a - b) < 1e-5f;
}

EnvironmentLevel level(std::uint32_t size, float base) {
    EnvironmentLevel out{size, {}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        for (std::uint32_t y = 0; y < size; ++y) {
            for (std::uint32_t x = 0; x < size; ++x)
                out.pixels.push_back({base + face, static_cast<float>(x), static_cast<float>(y), 1});
        }
    }
    return out;
}

std::shared_ptr<Environment> environment(std::uint32_t size, std::uint32_t diffuseSize, float base) {
    auto out = std::make_shared<Environment>();
    out->diffuse = level(diffuseSize, base);
    for (auto dimension = size; dimension; dimension /= 2) {
        out->specular.push_back(level(dimension, base));
        base += 6;
    }
    out->lutSize = 1;
    out->brdf.push_back({1, 0, 0, 1});
    out->validate();
    return out;
}

void verifyWeights(Scene& scene, const LightingZone& zone) {
    check(sampleLightingZones(scene.lightingZones(), {}).global == 1, "Empty scenes keep global lighting");
    scene.setLightingZones({zone});
    check(sampleLightingZones(scene.lightingZones(), {-1, 1, 1}).global == 1, "Exterior uses global lighting");
    check(sampleLightingZones(scene.lightingZones(), {0, 1, 1}).global == 1, "Zone face uses global lighting");
    const auto transition = sampleLightingZones(scene.lightingZones(), {.5f, 2, 2});
    check(near(transition.global, .5f) && near(transition.local[0], .5f), "Halfway fade");
    check(sampleLightingZones(scene.lightingZones(), {1, 2, 2}).local[0] == 1, "Inner region uses local lighting");
    check(sampleLightingZones(scene.lightingZones(), {1, 2, 3.5f}).global == .5f, "Nearest face sets fade");

    auto neighbor = zone;
    neighbor.minimum.x = 1;
    neighbor.maximum.x = 5;
    scene.setLightingZones({zone, neighbor});
    const auto overlap = sampleLightingZones(scene.lightingZones(), {2.5f, 2, 2});
    check(overlap.global == 0 && overlap.local[0] == .5f && overlap.local[1] == .5f, "Overlap normalizes local influences");
    const auto partial = sampleLightingZones(scene.lightingZones(), {1.5f, 2, 2});
    check(near(partial.local[0], 2.0f / 3.0f) && near(partial.local[1], 1.0f / 3.0f), "Overlap preserves relative influence");
    const auto corner = sampleLightingZones(scene.lightingZones(), {1.5f, .25f, 2});
    check(near(corner.global, .6875f) && near(corner.local[0], .15625f) && near(corner.local[1], .15625f),
          "Global retains unused influence in two faded zones");
    check(sampleLightingZones(scene.lightingZones(), {std::numeric_limits<float>::quiet_NaN(), 0, 0}).global == 1,
          "Non-finite query uses global lighting");
}

void verifyValidation(Scene& scene, const LightingZone& zone) {
    const auto before = scene.lightingZones();
    auto expectRejected = [&](std::vector<LightingZone> zones) {
        try {
            scene.setLightingZones(std::move(zones));
        } catch (const std::invalid_argument&) {
            const auto& after = scene.lightingZones();
            check(after.size() == before.size() && after[0].environment == before[0].environment &&
                  after[1].minimum.x == before[1].minimum.x, "Failed publication preserves all live zones");
            return;
        }
        throw std::runtime_error("Invalid publication accepted");
    };
    expectRejected({zone, zone, zone});
    auto invalid = zone;
    invalid.blendDistance = 3;
    expectRejected({zone, invalid});
    invalid = zone;
    invalid.intensity = -1;
    expectRejected({invalid});
    invalid = zone;
    invalid.environment.reset();
    expectRejected({invalid});
    invalid = zone;
    invalid.environment = std::make_shared<Environment>();
    expectRejected({invalid});

    auto darkness = zone;
    darkness.intensity = 0;
    scene.setLightingZones({darkness});
    check(sampleLightingZones(scene.lightingZones(), {2, 2, 2}).local[0] == 1, "An authored dark zone still overrides global lighting");
    scene.setLightingZones({});
    check(scene.lightingZones().empty(), "An empty publication clears the zones");
}

void verifyPacking(const Environment& global, LightingZone zone) {
    const auto small = environment(4, 2, 20);
    const auto matching = environment(8, 4, 50);
    zone.environment = small;
    auto second = zone;
    second.environment = matching;
    const std::array zones{zone, second};
    const auto packed = packLightingEnvironments(global, zones);
    check(packed.cubeCount == 3 && packed.specular.size() == global.specular.size(), "Global plus two ordered cubes");
    for (std::size_t mip = 0; mip < global.specular.size(); ++mip) {
        const auto& original = global.specular[mip];
        const auto& output = packed.specular[mip];
        check(output.size == original.size && output.pixels.size() == original.pixels.size() * 3, "Packed dimensions");
        check(std::equal(original.pixels.begin(), original.pixels.end(), output.pixels.begin()), "Global mip preserved exactly");
        check(std::equal(matching->specular[mip].pixels.begin(), matching->specular[mip].pixels.end(),
                         output.pixels.begin() + 2 * original.pixels.size()), "Matching local mip preserved exactly");
        for (std::uint32_t face = 0; face < 6; ++face) {
            const auto& sampled = output.pixels[original.pixels.size() + face * output.size * output.size];
            check(near(sampled[0], 20 + 4 * static_cast<float>(mip) + face), "Local mips resample by normalized roughness and preserve face order");
        }
    }
    const auto& diffuse = packed.diffuse;
    check(diffuse.size == global.diffuse.size && diffuse.pixels.size() == 3 * global.diffuse.pixels.size(), "Diffuse dimensions");
    check(std::equal(global.diffuse.pixels.begin(), global.diffuse.pixels.end(), diffuse.pixels.begin()), "Global diffuse preserved exactly");
    const auto& sampled = diffuse.pixels[global.diffuse.pixels.size() + diffuse.size + 2];
    check(near(sampled[1], .75f) && near(sampled[2], .25f), "Local diffuse uses bilinear face coordinates");

    const auto reflection = environment(4, 2, 80);
    const auto reflected = packLightingEnvironments(global, zones, reflection.get());
    check(reflected.cubeCount == 4, "Reflection cache follows global and two lighting zones");
    for (std::size_t mip = 0; mip < reflected.specular.size(); ++mip) {
        const auto& output = reflected.specular[mip];
        const auto& previous = packed.specular[mip];
        check(output.pixels.size() == global.specular[mip].pixels.size() * 4, "Reflection cube array has four complete cubes");
        check(std::equal(previous.pixels.begin(), previous.pixels.end(), output.pixels.begin()), "Appending reflection preserves baked specular cubes");
        for (std::uint32_t face = 0; face < 6; ++face) {
            const auto& value = output.pixels[previous.pixels.size() + face * output.size * output.size];
            check(near(value[0], 80 + 4 * static_cast<float>(mip) + face), "Reflection cache resamples roughness and face order");
        }
    }
    check(std::equal(packed.diffuse.pixels.begin(), packed.diffuse.pixels.end(), reflected.diffuse.pixels.begin()),
          "Appending reflection preserves all baked diffuse cubes");
    const auto withoutZones = packLightingEnvironments(global, {}, reflection.get());
    check(withoutZones.cubeCount == 2 && withoutZones.specular.front().pixels.size() == 2 * global.specular.front().pixels.size(),
          "Reflection cache uses cube one when no lighting zones are present");
}
} // namespace

int main() try {
    const auto global = environment(8, 4, 0);
    const LightingZone zone{{0, 0, 0}, {4, 4, 4}, 1, global};
    Scene scene;
    verifyWeights(scene, zone);
    verifyValidation(scene, zone);
    verifyPacking(*global, zone);
    std::cout << "environment lighting: influence, atomic validation and cube packing passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
