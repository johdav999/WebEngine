# Reusable browser renderer; consumers never reproduce its source list.
if(NOT EMSCRIPTEN)
  return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/shaders.cmake")
add_library(engine_browser STATIC src/renderer.cpp src/webgpu_host_browser.cpp
  src/webgpu_renderer.cpp src/webgpu_water.cpp src/webgl_renderer.cpp src/webgl_water.cpp src/gui_renderer.cpp)
add_library(WebEngine::Browser ALIAS engine_browser)
target_include_directories(engine_browser PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(engine_browser PUBLIC engine_core engine_gui)
target_compile_options(engine_browser PUBLIC --use-port=emdawnwebgpu -fexceptions)
target_link_options(engine_browser INTERFACE --use-port=emdawnwebgpu -fexceptions
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2)
