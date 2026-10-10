cmake_minimum_required(VERSION 3.22)
project(WebEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(cmake/jolt.cmake)

add_library(ufbx STATIC third_party/ufbx/ufbx.c)
target_include_directories(ufbx PUBLIC third_party/ufbx)
set_target_properties(ufbx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_compile_definitions(ufbx PRIVATE UFBX_NO_STDIO UFBX_NO_SUBDIVISION
  UFBX_NO_TESSELLATION UFBX_NO_GEOMETRY_CACHE
  UFBX_NO_SKINNING_EVALUATION UFBX_NO_ANIMATION_BAKING UFBX_NO_FORMAT_OBJ)

add_library(cgltf STATIC src/cgltf.c)
target_include_directories(cgltf PUBLIC third_party/cgltf)
set_target_properties(cgltf PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

add_library(meshoptimizer STATIC
  third_party/meshoptimizer/allocator.cpp
  third_party/meshoptimizer/indexgenerator.cpp
  third_party/meshoptimizer/simplifier.cpp
  third_party/meshoptimizer/vcacheoptimizer.cpp
  third_party/meshoptimizer/vfetchoptimizer.cpp)
target_include_directories(meshoptimizer PUBLIC third_party/meshoptimizer)

add_library(engine_core
  src/assets.cpp
  src/audio.cpp
  src/application.cpp
  src/prefab.cpp
  src/prefab_components.cpp
  src/physics.cpp
  src/physics_demo.cpp
  src/ship_stress_demo.cpp
  src/collider_component.cpp
  src/rigid_body_component.cpp
  src/character_motor_component.cpp
  src/scene_archive.cpp
  src/scene_archive_file.cpp
  src/input.cpp
  src/interaction_component.cpp
  src/selection.cpp
  src/performance.cpp
  src/texture.cpp
  src/material.cpp
  src/material_graph.cpp
  src/material_showcase.cpp
  src/environment.cpp
  src/node.cpp
  src/actor.cpp
  src/component.cpp
  src/mesh.cpp
  src/mesh_optimizer.cpp
  src/mesh_component.cpp
  src/fbx_importer.cpp
  src/glb_importer.cpp
  src/camera_component.cpp
  src/camera_controller.cpp
  src/light_component.cpp
  src/shadow.cpp
  src/orbit_controller.cpp
  src/scene.cpp
  src/render_frame.cpp
  src/mesh_detail.cpp
  src/render_view.cpp)
target_sources(engine_core PRIVATE src/animation.cpp src/skeleton_component.cpp
  src/animation_component.cpp src/animation_controller.cpp src/skeletal_mesh_component.cpp src/fbx_animation_import.cpp)
target_sources(engine_core PRIVATE src/particle_emitter_component.cpp src/effect_component.cpp src/effect_presets.cpp src/fx_frame.cpp)
target_include_directories(engine_core PUBLIC include)
target_link_libraries(engine_core PRIVATE ufbx cgltf meshoptimizer Jolt)
add_library(image_decoder STATIC src/image_decoder.c)
target_include_directories(image_decoder PUBLIC third_party/stb)
target_link_libraries(engine_core PRIVATE image_decoder)
add_library(engine_gui src/gui.cpp src/gui_font.cpp)
target_include_directories(engine_gui PUBLIC include PRIVATE third_party/stb)
target_link_libraries(engine_gui PUBLIC engine_core)
if(MSVC)
  target_compile_options(engine_core PRIVATE /W4)
else()
  target_compile_options(engine_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EMSCRIPTEN)
  option(WEBENGINE_BUILD_PERSISTENCE_WASM_TESTS "Build the Node/Wasm persistence regression runner" OFF)
  if(WEBENGINE_BUILD_PERSISTENCE_WASM_TESTS)
    add_executable(persistence_wasm_tests tests/persistence_tests.cpp)
    target_link_libraries(persistence_wasm_tests PRIVATE engine_core)
    target_compile_options(persistence_wasm_tests PRIVATE -fexceptions)
    target_link_options(persistence_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node
      -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
    set_target_properties(persistence_wasm_tests PROPERTIES SUFFIX ".js")
  endif()
  add_library(engine_assets_browser src/asset_browser.cpp)
  target_link_libraries(engine_assets_browser PUBLIC engine_core)
  target_compile_options(engine_assets_browser PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  target_link_options(engine_assets_browser INTERFACE -sFETCH=1)
  add_executable(asset_browser_tests EXCLUDE_FROM_ALL tests/asset_browser_tests.cpp)
  target_link_libraries(asset_browser_tests PRIVATE engine_assets_browser)
  target_compile_options(asset_browser_tests PRIVATE -fexceptions)
  target_link_options(asset_browser_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1048576 -sENVIRONMENT=web -sFILESYSTEM=0)
  set_target_properties(asset_browser_tests PROPERTIES SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asset-tests")
  add_custom_command(TARGET asset_browser_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/tests/asset-browser.html"
      "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/index.html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/tests/fixtures/Knarr.fbx"
      "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/Knarr.fbx")
  option(WEBENGINE_BUILD_MOVEMENT_WASM_TESTS "Build the Node/Wasm movement regression runner" OFF)
  if(WEBENGINE_BUILD_MOVEMENT_WASM_TESTS)
    add_executable(movement_wasm_tests tests/movement_tests.cpp)
    target_link_libraries(movement_wasm_tests PRIVATE engine_core)
    target_compile_options(movement_wasm_tests PRIVATE -fexceptions)
    target_link_options(movement_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node
      -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
    set_target_properties(movement_wasm_tests PROPERTIES SUFFIX ".js")
  endif()
  target_compile_options(engine_gui PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  target_compile_options(meshoptimizer PRIVATE -fexceptions)
  target_compile_options(engine_core PRIVATE -fexceptions)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.wgsl" MESH_SHADER)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles.wgsl" PARTICLE_SHADER)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gui.wgsl" GUI_SHADER)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gui.wgsl")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particles.wgsl")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.wgsl")
  configure_file(cmake/shaders.hpp.in generated/shaders.hpp @ONLY)
  add_library(engine_gui_browser src/gui_browser.cpp)
  target_link_libraries(engine_gui_browser PUBLIC engine_gui)
  target_compile_options(engine_gui_browser PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  add_library(engine_audio_browser src/audio_browser.cpp)
  target_link_libraries(engine_audio_browser PUBLIC engine_core)
  target_compile_options(engine_audio_browser PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  add_executable(webengine src/main.cpp src/renderer.cpp src/gui_renderer.cpp src/gui_demo.cpp src/compositing_demo.cpp src/audio_demo.cpp)
  target_link_libraries(webengine PRIVATE engine_core engine_gui engine_gui_browser engine_audio_browser)
  target_include_directories(webengine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(webengine PRIVATE --use-port=emdawnwebgpu -fexceptions -Wall -Wextra -Wpedantic)
  target_link_options(webengine PRIVATE
    --use-port=emdawnwebgpu
    -fexceptions
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1048576
    -sENVIRONMENT=web
    -sEXIT_RUNTIME=0
    -sFILESYSTEM=0
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']"
    "$<$<CONFIG:Debug>:-sASSERTIONS=2>"
    "$<$<CONFIG:Debug>:-sSAFE_HEAP=1>")
  set_target_properties(webengine PROPERTIES SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/site")
  add_custom_target(web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/web" "${CMAKE_SOURCE_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_BINARY_DIR}/site" "${CMAKE_SOURCE_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/tests/fixtures/Knarr.fbx" "${CMAKE_SOURCE_DIR}/dist/assets/Knarr.fbx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/tests/fixtures/skeletal_column_ascii.fbx" "${CMAKE_SOURCE_DIR}/dist/assets/skeletal_column_ascii.fbx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/tests/fixtures/morph_column_ascii.fbx" "${CMAKE_SOURCE_DIR}/dist/assets/morph_column_ascii.fbx"
    DEPENDS webengine
    COMMENT "Publishing selected browser build to dist")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${WEBENGINE_JOLT_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/dist/Jolt-LICENSE.txt")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/third_party/noto" "${CMAKE_SOURCE_DIR}/dist/assets/fonts")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/third_party/cgltf/LICENSE" "${CMAKE_SOURCE_DIR}/dist/cgltf-LICENSE.txt")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/third_party/ufbx/LICENSE" "${CMAKE_SOURCE_DIR}/dist/ufbx-LICENSE.txt")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/third_party/stb/LICENSE" "${CMAKE_SOURCE_DIR}/dist/stb-LICENSE.txt")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/third_party/meshoptimizer/LICENSE.md" "${CMAKE_SOURCE_DIR}/dist/meshoptimizer-LICENSE.txt")
else()
  include(CTest)
  if(BUILD_TESTING)
    add_executable(persistence_tests tests/persistence_tests.cpp)
    target_link_libraries(persistence_tests PRIVATE engine_core)
    target_compile_definitions(persistence_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME persistence COMMAND persistence_tests)
    add_executable(render_quality_tests tests/render_quality_tests.cpp)
    target_link_libraries(render_quality_tests PRIVATE engine_core)
    add_test(NAME render_quality COMMAND render_quality_tests)
    add_executable(asset_tests tests/asset_tests.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(asset_tests PRIVATE engine_core Threads::Threads)
    add_test(NAME assets COMMAND asset_tests)
    add_executable(audio_tests tests/audio_tests.cpp)
    target_link_libraries(audio_tests PRIVATE engine_core)
    add_test(NAME audio COMMAND audio_tests)
    add_executable(selection_tests tests/selection_tests.cpp)
    target_link_libraries(selection_tests PRIVATE engine_core)
    add_test(NAME selection COMMAND selection_tests)
    add_executable(application_tests tests/application_tests.cpp)
    add_executable(prefab_tests tests/prefab_tests.cpp)
    target_link_libraries(prefab_tests PRIVATE engine_core)
    add_test(NAME prefab COMMAND prefab_tests)
    target_link_libraries(application_tests PRIVATE engine_core)
    add_test(NAME application COMMAND application_tests)
    add_executable(mesh_detail_tests tests/mesh_detail_tests.cpp)
    target_link_libraries(mesh_detail_tests PRIVATE engine_core)
    target_compile_definitions(mesh_detail_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME mesh_detail COMMAND mesh_detail_tests)
    add_executable(scene_archive_tests tests/scene_archive_tests.cpp)
    target_link_libraries(scene_archive_tests PRIVATE engine_core)
    target_compile_definitions(scene_archive_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME scene_archive COMMAND scene_archive_tests)
    add_executable(material_graph_tests tests/material_graph_tests.cpp)
    target_link_libraries(material_graph_tests PRIVATE engine_core)
    add_test(NAME material_graph COMMAND material_graph_tests)
    find_program(WEBENGINE_NODE_EXECUTABLE node)
    if(WEBENGINE_NODE_EXECUTABLE)
      add_test(NAME input_adapter COMMAND "${WEBENGINE_NODE_EXECUTABLE}"
        --test "${CMAKE_SOURCE_DIR}/tests/input-adapter-tests.cjs")
      add_test(NAME material_graph_model COMMAND "${WEBENGINE_NODE_EXECUTABLE}"
        --test "${CMAKE_SOURCE_DIR}/tests/material-graph-tests.cjs")
    endif()
    add_executable(morph_tests tests/morph_tests.cpp)
    target_link_libraries(morph_tests PRIVATE engine_core)
    target_compile_definitions(morph_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME morph COMMAND morph_tests)
    add_executable(ship_stress_tests tests/ship_stress_tests.cpp)
    target_link_libraries(ship_stress_tests PRIVATE engine_core)
    target_compile_definitions(ship_stress_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME ship_stress COMMAND ship_stress_tests)
    add_executable(shadow_tests tests/shadow_tests.cpp)
    target_link_libraries(shadow_tests PRIVATE engine_core)
    add_test(NAME shadow COMMAND shadow_tests)
    add_executable(physics_tests tests/physics_tests.cpp)
    target_link_libraries(physics_tests PRIVATE engine_core)
    add_test(NAME physics COMMAND physics_tests)
    add_executable(movement_tests tests/movement_tests.cpp)
    target_link_libraries(movement_tests PRIVATE engine_core)
    add_test(NAME movement COMMAND movement_tests)
    add_executable(input_tests tests/input_tests.cpp)
    target_link_libraries(input_tests PRIVATE engine_core)
    add_test(NAME input COMMAND input_tests)
    add_executable(gui_tests tests/gui_tests.cpp)
    target_link_libraries(gui_tests PRIVATE engine_gui)
    target_compile_definitions(gui_tests PRIVATE GUI_FONT_PATH="${CMAKE_SOURCE_DIR}/third_party/noto/NotoSans-Regular.ttf")
    add_test(NAME gui COMMAND gui_tests)
    add_executable(material_tests tests/material_tests.cpp)
    target_link_libraries(material_tests PRIVATE engine_core)
    target_compile_definitions(material_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME material COMMAND material_tests)
    add_executable(fx_tests tests/fx_tests.cpp)
    target_link_libraries(fx_tests PRIVATE engine_core)
    add_test(NAME fx COMMAND fx_tests)
    add_executable(component_tests tests/component_tests.cpp)
    target_link_libraries(component_tests PRIVATE engine_core)
    add_test(NAME component COMMAND component_tests)
    add_executable(actor_tests tests/actor_tests.cpp)
    target_link_libraries(actor_tests PRIVATE engine_core)
    target_compile_definitions(actor_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME actor COMMAND actor_tests)
    add_executable(render_view_tests tests/render_view_tests.cpp)
    target_link_libraries(render_view_tests PRIVATE engine_core)
    add_test(NAME render_view COMMAND render_view_tests)
    add_executable(camera_tests tests/camera_tests.cpp)
    add_executable(camera_controller_tests tests/camera_controller_tests.cpp)
    target_link_libraries(camera_controller_tests PRIVATE engine_core)
    add_test(NAME camera_controller COMMAND camera_controller_tests)
    add_executable(frustum_culling_tests tests/frustum_culling_tests.cpp)
    target_link_libraries(frustum_culling_tests PRIVATE engine_core)
    add_test(NAME frustum_culling COMMAND frustum_culling_tests)
    target_link_libraries(camera_tests PRIVATE engine_core)
    add_test(NAME camera COMMAND camera_tests)
    add_executable(animation_tests tests/animation_tests.cpp)
    target_link_libraries(animation_tests PRIVATE engine_core)
    target_compile_definitions(animation_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME animation COMMAND animation_tests)
    add_executable(animation_control_tests tests/animation_control_tests.cpp)
    target_link_libraries(animation_control_tests PRIVATE engine_core)
    add_test(NAME animation_control COMMAND animation_control_tests)
    add_executable(root_motion_tests tests/root_motion_tests.cpp)
    target_link_libraries(root_motion_tests PRIVATE engine_core)
    add_test(NAME root_motion COMMAND root_motion_tests)
    add_executable(instancing_tests tests/instancing_tests.cpp)
    target_link_libraries(instancing_tests PRIVATE engine_core)
    add_test(NAME instancing COMMAND instancing_tests)
    add_executable(transform_cache_tests tests/transform_cache_tests.cpp)
    target_link_libraries(transform_cache_tests PRIVATE engine_core)
    add_test(NAME transform_cache COMMAND transform_cache_tests)
    add_executable(render_frame_tests tests/render_frame_tests.cpp)
    target_link_libraries(render_frame_tests PRIVATE engine_core)
    add_test(NAME render_frame COMMAND render_frame_tests)
    add_executable(mesh_optimizer_tests tests/mesh_optimizer_tests.cpp third_party/meshoptimizer/indexanalyzer.cpp)
    target_link_libraries(mesh_optimizer_tests PRIVATE engine_core meshoptimizer)
    target_compile_definitions(mesh_optimizer_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME mesh_optimizer COMMAND mesh_optimizer_tests)
    add_executable(performance_tests tests/performance_tests.cpp)
    target_link_libraries(performance_tests PRIVATE engine_core)
    add_test(NAME performance COMMAND performance_tests)
    add_executable(engine_tests tests/core_tests.cpp)
    target_link_libraries(engine_tests PRIVATE engine_core)
    add_test(NAME engine_core COMMAND engine_tests)
    add_executable(glb_importer_tests tests/glb_importer_tests.cpp)
    target_link_libraries(glb_importer_tests PRIVATE engine_core)
    target_compile_definitions(glb_importer_tests PRIVATE GLB_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME glb_importer COMMAND glb_importer_tests)
    add_executable(fbx_importer_tests tests/fbx_importer_tests.cpp)
    target_link_libraries(fbx_importer_tests PRIVATE engine_core)
    target_compile_definitions(fbx_importer_tests PRIVATE
      FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME fbx_importer COMMAND fbx_importer_tests)
  endif()
  message(STATUS "Native build: engine core and tests. Use emcmake for the browser renderer.")
endif()

# Explicit portable Wasm regressions; building does not publish the playground.
if(EMSCRIPTEN)
  add_executable(animation_control_wasm_tests EXCLUDE_FROM_ALL tests/animation_control_tests.cpp)
  target_link_libraries(animation_control_wasm_tests PRIVATE engine_core)
  target_compile_options(animation_control_wasm_tests PRIVATE -fexceptions)
  target_link_options(animation_control_wasm_tests PRIVATE -fexceptions
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFILESYSTEM=0
    -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  set_target_properties(animation_control_wasm_tests PROPERTIES SUFFIX ".js")
  add_executable(spatial_query_tests EXCLUDE_FROM_ALL tests/physics_tests.cpp)
  target_link_libraries(spatial_query_tests PRIVATE engine_core)
  target_compile_options(spatial_query_tests PRIVATE -fexceptions)
  target_link_options(spatial_query_tests PRIVATE -fexceptions
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFILESYSTEM=0
    -sENVIRONMENT=web -sEXIT_RUNTIME=1 -sMODULARIZE=1
    -sEXPORT_NAME=createSpatialQueryTests)
  set_target_properties(spatial_query_tests PROPERTIES SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/spatial-tests")
endif()
