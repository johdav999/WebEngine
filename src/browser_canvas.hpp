#pragma once
#include <emscripten.h>

namespace engine::browser {

// The host may scale the canvas for display. Use untransformed CSS pixels for
// renderer and GUI layout, and let the render quality budget cap the pixel ratio.
inline double canvasCssWidth() {
    return EM_ASM_DOUBLE({ return document.getElementById('viewport')?.clientWidth ?? 0; });
}
inline double canvasCssHeight() {
    return EM_ASM_DOUBLE({ return document.getElementById('viewport')?.clientHeight ?? 0; });
}
inline double canvasPixelRatio() {
    return EM_ASM_DOUBLE({ return window.EngineMobilePreview?.pixelRatio() ?? (window.devicePixelRatio || 1); });
}

}
