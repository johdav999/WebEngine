#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
namespace engine {
// Windows SDL window/event coordinates are pixels even at >100% content scale.
// Shared GUI/camera input uses logical units; backing size remains physical pixels.
inline float desktopCoordinateScale(float displayScale,float pixelDensity) {
    return std::isfinite(displayScale)&&std::isfinite(pixelDensity)&&displayScale>0&&pixelDensity>0?displayScale/pixelDensity:1.f;
}
inline float desktopCoordinateScale(SDL_Window& window) {
    return desktopCoordinateScale(SDL_GetWindowDisplayScale(&window),SDL_GetWindowPixelDensity(&window));
}
}
