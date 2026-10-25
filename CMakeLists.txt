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
  src/asset_reload.cpp
  src/asset_decode.cpp
  src/texture_bundle.cpp
  src/audio.cpp
  src/cinematic_sequence.cpp
  src/replay.cpp
  src/replay_ghost.cpp
  src/application.cpp
  src/prefab_asset.cpp
  src/prefab.cpp
  src/prefab_components.cpp
  src/physics.cpp
  src/physics_demo.cpp
  src/ship_stress_demo.cpp
  src/physical_surface.cpp
  src/collider_component.cpp
  src/rigid_body_component.cpp
  src/spline.cpp
  src/region_stream.cpp
  src/route.cpp
  src/vehicle_component.cpp
  src/character_motor_component.cpp
  src/scene_archive.cpp
  src/scene_archive_file.cpp
  src/input.cpp
  src/interaction_component.cpp
  src/selection.cpp
  src/manipulation.cpp
  src/performance.cpp
  src/performance_timeline.cpp
  src/adaptive_quality.cpp
  src/texture.cpp
  src/material.cpp
  src/material_graph.cpp
  src/material_showcase.cpp
  src/environment.cpp
  src/fog.cpp
  src/sky.cpp
  src/water.cpp
  src/water_region.cpp
  src/terrain.cpp
  src/terrain_seam.cpp
  src/terrain_mesh.cpp
  src/terrain_cliff.cpp
  src/terrain_mesh_holes.cpp
  src/terrain_paging.cpp
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
  src/camera_follow.cpp
  src/light_component.cpp
  src/shadow.cpp
  src/orbit_controller.cpp
  src/scene.cpp
  src/render_frame.cpp
  src/mesh_detail.cpp
  src/render_view.cpp)
target_sources(engine_core PRIVATE src/animation.cpp src/skeleton_component.cpp
  src/ik.cpp src/animation_retarget.cpp src/animation_component.cpp src/animation_controller.cpp src/animation_library.cpp src/skeletal_mesh_component.cpp src/fbx_animation_import.cpp)
target_sources(engine_core PRIVATE src/particle_emitter_component.cpp src/particle_events.cpp src/particle_collision.cpp src/decal.cpp src/mesh_particle.cpp src/trail.cpp src/effect_archive.cpp src/effect_component.cpp src/effect_presets.cpp src/fx_frame.cpp)
target_include_directories(engine_core PUBLIC include)
target_link_libraries(engine_core PRIVATE ufbx cgltf meshoptimizer Jolt)
add_library(image_decoder STATIC src/image_decoder.c)
target_include_directories(image_decoder PUBLIC third_party/stb)
target_link_libraries(engine_core PRIVATE image_decoder)
include(cmake/recast.cmake)
add_library(engine_ai src/ai.cpp src/navmesh.cpp src/state_machine.cpp src/ai_controller.cpp src/ai_tasks.cpp)
target_sources(engine_ai PRIVATE src/crowd_navigation.cpp src/dynamic_navigation.cpp)
target_link_libraries(engine_ai PUBLIC engine_core)
target_link_libraries(engine_ai PRIVATE engine_recast engine_detour)
if(MSVC)
  target_compile_options(engine_ai PRIVATE /W4)
else()
  target_compile_options(engine_ai PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_library(engine_gui src/gui.cpp src/gui_font.cpp)
target_include_directories(engine_gui PUBLIC include PRIVATE third_party/stb)
target_link_libraries(engine_gui PUBLIC engine_core)
add_library(engine_editor src/editor.cpp src/editor_properties.cpp src/editor_content.cpp src/editor_structure.cpp src/editor_play.cpp src/editor_terrain.cpp src/editor_fx.cpp src/effect_preview.cpp src/editor_ai.cpp)
target_link_libraries(engine_editor PUBLIC engine_core engine_ai)
target_sources(engine_editor PRIVATE src/editor_animation.cpp src/editor_prefab.cpp)
target_sources(engine_editor PRIVATE src/editor_cinematic.cpp)
add_library(engine_editor_gui src/editor_authoring.cpp)
target_link_libraries(engine_editor_gui PUBLIC engine_editor engine_gui)
target_include_directories(engine_editor_gui PUBLIC include)
if(MSVC)
  target_compile_options(engine_core PRIVATE /W4)
else()
  target_compile_options(engine_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EMSCRIPTEN)
  target_compile_options(engine_ai PRIVATE -fexceptions)
  option(WEBENGINE_BUILD_PERSISTENCE_WASM_TESTS "Build the Node/Wasm persistence regression runner" OFF)
  if(WEBENGINE_BUILD_PERSISTENCE_WASM_TESTS)
    add_executable(persistence_wasm_tests tests/persistence_tests.cpp)
    target_link_libraries(persistence_wasm_tests PRIVATE engine_core)
    target_compile_options(persistence_wasm_tests PRIVATE -fexceptions)
    target_link_options(persistence_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node
      -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
    set_target_properties(persistence_wasm_tests PROPERTIES SUFFIX ".js")
  endif()
  add_library(engine_assets_browser src/asset_browser.cpp src/asset_worker_browser.cpp src/terrain_browser.cpp)
  target_link_libraries(engine_assets_browser PUBLIC engine_core)
  target_compile_options(engine_assets_browser PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  target_link_options(engine_assets_browser INTERFACE -sFETCH=1)
  add_executable(asset_decode_worker src/asset_decode_worker.cpp)
  target_link_libraries(asset_decode_worker PRIVATE engine_core)
  target_compile_options(asset_decode_worker PRIVATE -fexceptions)
  target_link_options(asset_decode_worker PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1048576 -sENVIRONMENT=worker -sFILESYSTEM=0 --no-entry
    -sMODULARIZE=1 -sEXPORT_NAME=createAssetDecoder
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_engine_decode_asset','_engine_build_terrain_page','_engine_decode_size','_engine_decode_error']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','UTF8ToString']")
  set_target_properties(asset_decode_worker PROPERTIES SUFFIX ".js" OUTPUT_NAME "asset_decode"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/site")
  add_dependencies(engine_assets_browser asset_decode_worker)
  add_executable(performance_timeline_wasm_tests EXCLUDE_FROM_ALL tests/performance_timeline_tests.cpp)
  target_link_libraries(performance_timeline_wasm_tests PRIVATE engine_core)
  target_compile_options(performance_timeline_wasm_tests PRIVATE -fexceptions)
  target_link_options(performance_timeline_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
  set_target_properties(performance_timeline_wasm_tests PROPERTIES SUFFIX ".js")
  add_executable(adaptive_quality_wasm_tests EXCLUDE_FROM_ALL tests/adaptive_quality_tests.cpp)
  target_link_libraries(adaptive_quality_wasm_tests PRIVATE engine_core)
  target_compile_options(adaptive_quality_wasm_tests PRIVATE -fexceptions)
  target_link_options(adaptive_quality_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
  set_target_properties(adaptive_quality_wasm_tests PROPERTIES SUFFIX ".js")
  add_executable(crowd_navigation_wasm_tests EXCLUDE_FROM_ALL tests/crowd_navigation_tests.cpp src/crowd_demo.cpp)
  target_link_libraries(crowd_navigation_wasm_tests PRIVATE engine_ai)
  target_compile_options(crowd_navigation_wasm_tests PRIVATE -fexceptions)
  target_link_options(crowd_navigation_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
  set_target_properties(crowd_navigation_wasm_tests PROPERTIES SUFFIX ".js")
  add_executable(prefab_asset_wasm_tests EXCLUDE_FROM_ALL tests/prefab_asset_tests.cpp)
  add_executable(editor_authoring_wasm_tests EXCLUDE_FROM_ALL tests/editor_authoring_tests.cpp)
  target_link_libraries(editor_authoring_wasm_tests PRIVATE engine_editor_gui)
  target_compile_options(engine_editor_gui PRIVATE -fexceptions)
  target_compile_options(editor_authoring_wasm_tests PRIVATE -fexceptions)
  target_link_options(editor_authoring_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
  set_target_properties(editor_authoring_wasm_tests PROPERTIES SUFFIX ".js")
  add_executable(content_iteration_wasm_tests EXCLUDE_FROM_ALL tests/content_iteration_tests.cpp)
  target_link_libraries(content_iteration_wasm_tests PRIVATE engine_editor)
  target_compile_options(content_iteration_wasm_tests PRIVATE -fexceptions)
  target_link_options(content_iteration_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
  set_target_properties(content_iteration_wasm_tests PROPERTIES SUFFIX ".js")
  target_link_libraries(prefab_asset_wasm_tests PRIVATE engine_editor)
  target_compile_options(prefab_asset_wasm_tests PRIVATE -fexceptions)
  target_link_options(prefab_asset_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
  set_target_properties(prefab_asset_wasm_tests PROPERTIES SUFFIX ".js")
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
      "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/Knarr.fbx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/web/asset-worker.js"
      "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/asset-worker.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:asset_decode_worker>"
      "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/asset_decode.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/site/asset_decode.wasm"
      "${CMAKE_CURRENT_BINARY_DIR}/asset-tests/asset_decode.wasm")
  option(WEBENGINE_BUILD_MOVEMENT_WASM_TESTS "Build the Node/Wasm movement regression runner" OFF)
  if(WEBENGINE_BUILD_MOVEMENT_WASM_TESTS)
    add_executable(movement_wasm_tests tests/movement_tests.cpp)
    target_link_libraries(movement_wasm_tests PRIVATE engine_core)
    target_compile_options(movement_wasm_tests PRIVATE -fexceptions)
    target_link_options(movement_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node
      -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXIT_RUNTIME=1)
    set_target_properties(movement_wasm_tests PROPERTIES SUFFIX ".js")
  endif()
  target_compile_options(engine_editor PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  target_compile_options(engine_gui PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  target_compile_options(meshoptimizer PRIVATE -fexceptions)
  target_compile_options(engine_core PRIVATE -fexceptions)
  include(cmake/shaders.cmake)
  add_library(engine_gui_browser src/gui_browser.cpp)
  target_link_libraries(engine_gui_browser PUBLIC engine_gui)
  target_compile_options(engine_gui_browser PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  add_library(engine_audio_browser src/audio_browser.cpp)
  target_link_libraries(engine_audio_browser PUBLIC engine_core)
  target_compile_options(engine_audio_browser PRIVATE -fexceptions -Wall -Wextra -Wpedantic)
  add_executable(webengine src/editor_browser.cpp src/main.cpp src/renderer.cpp src/webgpu_host_browser.cpp src/webgpu_renderer.cpp src/webgpu_water.cpp src/webgl_renderer.cpp src/webgl_water.cpp src/gui_renderer.cpp src/gui_demo.cpp src/compositing_demo.cpp src/audio_demo.cpp src/driving_audio_demo.cpp src/sky_demo.cpp src/water_demo.cpp src/terrain_demo.cpp)
  target_sources(webengine PRIVATE src/ai_demo.cpp src/editor_ai_browser.cpp src/editor_ai_controller_browser.cpp src/editor_animation_browser.cpp src/editor_prefab_browser.cpp)
  target_sources(webengine PRIVATE src/crowd_demo.cpp)
  target_sources(webengine PRIVATE src/fx_contact_fixture.cpp)
  target_link_libraries(webengine PRIVATE engine_ai engine_editor engine_editor_gui engine_core engine_gui engine_gui_browser engine_audio_browser engine_assets_browser)
  target_include_directories(webengine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(webengine PRIVATE --use-port=emdawnwebgpu -fexceptions -Wall -Wextra -Wpedantic)
  target_link_options(webengine PRIVATE
    --use-port=emdawnwebgpu
    -fexceptions
    -sMIN_WEBGL_VERSION=2
    -sMAX_WEBGL_VERSION=2
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
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_custom_target(terrain_demo_pages
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/terrain_demo_pages.py"
      --output "${CMAKE_CURRENT_BINARY_DIR}/site/assets/terrain"
    COMMENT "Preparing bounded terrain demo source pages")
  add_dependencies(webengine terrain_demo_pages)
  add_custom_target(web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_BINARY_DIR}/site" "${CMAKE_SOURCE_DIR}/dist"
    # Current adapters must win over old host files retained in a reused site.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/web" "${CMAKE_SOURCE_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/tests/fixtures/Knarr.fbx" "${CMAKE_SOURCE_DIR}/dist/assets/Knarr.fbx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/tests/fixtures/skeletal_column_ascii.fbx" "${CMAKE_SOURCE_DIR}/dist/assets/skeletal_column_ascii.fbx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/tests/fixtures/morph_column_ascii.fbx" "${CMAKE_SOURCE_DIR}/dist/assets/morph_column_ascii.fbx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/tests/fixtures/audio-tone.mp3" "${CMAKE_SOURCE_DIR}/dist/assets/audio-stream-preview.mp3"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/tests/fixtures/static_instances.glb" "${CMAKE_SOURCE_DIR}/dist/assets/iteration.glb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/content-iteration.tga" "${CMAKE_SOURCE_DIR}/dist/assets/iteration.tga"
    DEPENDS webengine
    COMMENT "Publishing selected browser build to dist")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/assets/formula1-red-driving.glb" "${CMAKE_SOURCE_DIR}/dist/assets/formula1-red-driving.glb")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${WEBENGINE_JOLT_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/dist/Jolt-LICENSE.txt")
  add_custom_command(TARGET web_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/third_party/recastnavigation/License.txt" "${CMAKE_SOURCE_DIR}/dist/RecastNavigation-LICENSE.txt")
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
  add_executable(texture_tool tools/texture_tool.cpp)
  target_link_libraries(texture_tool PRIVATE engine_core)
  include(CTest)
  if(BUILD_TESTING)
    add_executable(formula_demo_tests tests/formula_demo_tests.cpp)
    target_link_libraries(formula_demo_tests PRIVATE engine_core)
    add_test(NAME formula_demo COMMAND formula_demo_tests)
    add_executable(formula_handling_tests tests/formula_handling_tests.cpp)
    target_link_libraries(formula_handling_tests PRIVATE engine_core)
    add_test(NAME formula_handling COMMAND formula_handling_tests)
    add_executable(ai_task_tests tests/ai_task_tests.cpp)
    target_link_libraries(ai_task_tests PRIVATE engine_editor)
    add_test(NAME ai_tasks COMMAND ai_task_tests)
    add_executable(ai_controller_tests tests/ai_controller_tests.cpp)
    target_link_libraries(ai_controller_tests PRIVATE engine_editor)
    add_test(NAME ai_controller COMMAND ai_controller_tests)
    add_executable(state_machine_tests tests/state_machine_tests.cpp)
    target_link_libraries(state_machine_tests PRIVATE engine_editor)
    add_test(NAME state_machine COMMAND state_machine_tests)
    add_executable(navmesh_tests tests/navmesh_tests.cpp)
    add_executable(crowd_navigation_tests tests/crowd_navigation_tests.cpp src/crowd_demo.cpp)
    target_link_libraries(crowd_navigation_tests PRIVATE engine_ai)
    add_test(NAME crowd_navigation COMMAND crowd_navigation_tests)
    target_link_libraries(navmesh_tests PRIVATE engine_ai)
    add_test(NAME navmesh COMMAND navmesh_tests)
    add_executable(ai_tests tests/ai_tests.cpp src/ai_demo.cpp)
    target_include_directories(ai_tests PRIVATE src)
    target_link_libraries(ai_tests PRIVATE engine_ai)
    add_test(NAME ai COMMAND ai_tests)
    add_executable(editor_fx_tests tests/editor_fx_tests.cpp)
    target_link_libraries(editor_fx_tests PRIVATE engine_editor)
    add_test(NAME editor_fx COMMAND editor_fx_tests)
    add_executable(editor_terrain_tests tests/editor_terrain_tests.cpp)
    target_link_libraries(editor_terrain_tests PRIVATE engine_editor)
    add_test(NAME editor_terrain COMMAND editor_terrain_tests)
    add_executable(editor_content_tests tests/editor_content_tests.cpp)
    target_link_libraries(editor_content_tests PRIVATE engine_editor)
    target_compile_definitions(editor_content_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME editor_content COMMAND editor_content_tests)
  add_executable(editor_structure_tests tests/editor_structure_tests.cpp)
  target_link_libraries(editor_structure_tests PRIVATE engine_editor)
  target_compile_definitions(editor_structure_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME editor_structure COMMAND editor_structure_tests)
    add_executable(editor_authoring_tests tests/editor_authoring_tests.cpp)
    target_link_libraries(editor_authoring_tests PRIVATE engine_editor_gui)
    add_test(NAME editor_authoring COMMAND editor_authoring_tests)
    add_executable(editor_tests tests/editor_tests.cpp)
    add_executable(content_iteration_tests tests/content_iteration_tests.cpp)
    target_link_libraries(content_iteration_tests PRIVATE engine_editor)
    add_test(NAME content_iteration COMMAND content_iteration_tests)
    target_link_libraries(editor_tests PRIVATE engine_editor)
    add_test(NAME editor COMMAND editor_tests)
    add_executable(terrain_tests tests/terrain_tests.cpp)
    target_link_libraries(terrain_tests PRIVATE engine_core)
    target_compile_definitions(terrain_tests PRIVATE TERRAIN_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME terrain COMMAND terrain_tests)
    add_executable(terrain_paging_tests tests/terrain_paging_tests.cpp)
    target_link_libraries(terrain_paging_tests PRIVATE engine_core)
    add_test(NAME terrain_paging COMMAND terrain_paging_tests)
    add_executable(terrain_page_edit_tests tests/terrain_page_edit_tests.cpp)
    target_link_libraries(terrain_page_edit_tests PRIVATE engine_core)
    add_test(NAME terrain_page_edit COMMAND terrain_page_edit_tests)
    add_executable(water_tests tests/water_tests.cpp)
    target_link_libraries(water_tests PRIVATE engine_core)
    add_test(NAME water COMMAND water_tests)
    add_executable(fog_tests tests/fog_tests.cpp)
    target_link_libraries(fog_tests PRIVATE engine_core)
    add_test(NAME fog COMMAND fog_tests)
    add_executable(sky_tests tests/sky_tests.cpp)
    target_link_libraries(sky_tests PRIVATE engine_core)
    add_test(NAME sky COMMAND sky_tests)
    add_executable(persistence_tests tests/persistence_tests.cpp)
    target_link_libraries(persistence_tests PRIVATE engine_core)
    target_compile_definitions(persistence_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME persistence COMMAND persistence_tests)
    add_executable(render_quality_tests tests/render_quality_tests.cpp)
    target_link_libraries(render_quality_tests PRIVATE engine_core)
    add_test(NAME render_quality COMMAND render_quality_tests)
    add_executable(mobile_asset_tests tests/mobile_asset_tests.cpp)
    target_link_libraries(mobile_asset_tests PRIVATE engine_core)
    target_compile_definitions(mobile_asset_tests PRIVATE FBX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME mobile_assets COMMAND mobile_asset_tests)
    add_executable(asset_tests tests/asset_tests.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(asset_tests PRIVATE engine_core Threads::Threads)
    add_test(NAME assets COMMAND asset_tests)
    add_executable(spline_route_tests tests/spline_route_tests.cpp)
    target_link_libraries(spline_route_tests PRIVATE engine_editor)
    add_test(NAME spline_route COMMAND spline_route_tests)
    add_executable(audio_dsp_tests tests/audio_dsp_tests.cpp)
    target_link_libraries(audio_dsp_tests PRIVATE engine_core)
    add_test(NAME audio_dsp COMMAND audio_dsp_tests)
    add_executable(audio_tests tests/audio_tests.cpp)
    add_executable(cinematic_sequence_tests tests/cinematic_sequence_tests.cpp)
    add_executable(replay_tests tests/replay_tests.cpp)
    target_link_libraries(replay_tests PRIVATE engine_core)
    add_test(NAME replay COMMAND replay_tests)
    target_link_libraries(cinematic_sequence_tests PRIVATE engine_editor)
    add_test(NAME cinematic_sequence COMMAND cinematic_sequence_tests)
    target_link_libraries(audio_tests PRIVATE engine_core)
    add_test(NAME audio COMMAND audio_tests)
    add_executable(selection_tests tests/selection_tests.cpp)
    target_link_libraries(selection_tests PRIVATE engine_core)
    add_test(NAME selection COMMAND selection_tests)
    add_executable(manipulation_tests tests/manipulation_tests.cpp)
    target_link_libraries(manipulation_tests PRIVATE engine_core)
    add_test(NAME manipulation COMMAND manipulation_tests)
    add_executable(application_tests tests/application_tests.cpp)
    add_executable(prefab_asset_tests tests/prefab_asset_tests.cpp)
    target_link_libraries(prefab_asset_tests PRIVATE engine_editor)
    add_test(NAME prefab_asset COMMAND prefab_asset_tests)
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
    add_executable(physical_surface_tests tests/physical_surface_tests.cpp)
    target_link_libraries(physical_surface_tests PRIVATE engine_editor)
    add_test(NAME physical_surface COMMAND physical_surface_tests)
    add_executable(vehicle_tests tests/vehicle_tests.cpp)
    target_link_libraries(vehicle_tests PRIVATE engine_editor)
    add_test(NAME vehicle COMMAND vehicle_tests)
    add_executable(physics_tests tests/physics_tests.cpp)
    target_link_libraries(physics_tests PRIVATE engine_core)
    add_test(NAME physics COMMAND physics_tests)
    add_executable(physics_joint_tests tests/physics_joint_tests.cpp)
    target_link_libraries(physics_joint_tests PRIVATE engine_core)
    add_test(NAME physics_joints COMMAND physics_joint_tests)
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
    add_executable(fx_contact_tests tests/fx_contact_tests.cpp)
    target_link_libraries(fx_contact_tests PRIVATE engine_editor)
    add_test(NAME fx_contact COMMAND fx_contact_tests)
    add_executable(fx_composition_tests tests/fx_composition_tests.cpp)
    target_link_libraries(fx_composition_tests PRIVATE engine_editor)
    add_test(NAME fx_composition COMMAND fx_composition_tests)

    add_executable(mesh_particle_tests tests/mesh_particle_tests.cpp)
    target_link_libraries(mesh_particle_tests PRIVATE engine_editor)
    add_test(NAME mesh_particle COMMAND mesh_particle_tests)

    add_executable(plane_particle_tests tests/plane_particle_tests.cpp)
    target_link_libraries(plane_particle_tests PRIVATE engine_editor)
    add_test(NAME plane_particle COMMAND plane_particle_tests)

    add_executable(trail_tests tests/trail_tests.cpp)
    target_link_libraries(trail_tests PRIVATE engine_editor)
    add_test(NAME trail COMMAND trail_tests)
    add_executable(surface_trail_tests tests/surface_trail_tests.cpp)
    target_link_libraries(surface_trail_tests PRIVATE engine_editor)
    add_test(NAME surface_trail COMMAND surface_trail_tests)
    add_executable(region_stream_tests tests/region_stream_tests.cpp)
    target_link_libraries(region_stream_tests PRIVATE engine_editor)
    add_test(NAME region_stream COMMAND region_stream_tests)
    add_executable(spotlight_tests tests/spotlight_tests.cpp)
    target_link_libraries(spotlight_tests PRIVATE engine_editor)
    add_test(NAME spotlight COMMAND spotlight_tests)

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
    add_executable(animation_library_tests tests/animation_library_tests.cpp)
    target_link_libraries(animation_library_tests PRIVATE engine_editor)
    add_test(NAME animation_library COMMAND animation_library_tests)
    add_executable(animation_retarget_tests tests/animation_retarget_tests.cpp)
    target_link_libraries(animation_retarget_tests PRIVATE engine_editor)
    add_test(NAME animation_retarget COMMAND animation_retarget_tests)
    add_executable(ik_tests tests/ik_tests.cpp)
    target_link_libraries(ik_tests PRIVATE engine_editor)
    add_test(NAME ik COMMAND ik_tests)
    add_executable(animation_layers_tests tests/animation_layers_tests.cpp)
    target_link_libraries(animation_layers_tests PRIVATE engine_editor)
    add_test(NAME animation_layers COMMAND animation_layers_tests)
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
    add_executable(performance_timeline_tests tests/performance_timeline_tests.cpp)
    add_executable(adaptive_quality_tests tests/adaptive_quality_tests.cpp)
    target_link_libraries(adaptive_quality_tests PRIVATE engine_core)
    add_test(NAME adaptive_quality COMMAND adaptive_quality_tests)
    target_link_libraries(performance_timeline_tests PRIVATE engine_core)
    add_test(NAME performance_timeline COMMAND performance_timeline_tests)
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
  message(STATUS "Native core/tests enabled. Optional Windows rendering: WEBENGINE_BUILD_WINDOWS=ON; browser: emcmake.")
endif()

# Explicit portable Wasm regressions; building does not publish the playground.
if(EMSCRIPTEN)
  add_executable(physics_joint_wasm_tests EXCLUDE_FROM_ALL tests/physics_joint_tests.cpp)
  target_link_libraries(physics_joint_wasm_tests PRIVATE engine_core)
  target_compile_options(physics_joint_wasm_tests PRIVATE -fexceptions)
  target_link_options(physics_joint_wasm_tests PRIVATE -fexceptions
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFILESYSTEM=0
    -sENVIRONMENT=web -sEXIT_RUNTIME=1 -sMODULARIZE=1
    -sEXPORT_NAME=createPhysicsJointTests)
  set_target_properties(physics_joint_wasm_tests PROPERTIES SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/joint-tests")
  add_executable(animation_control_wasm_tests EXCLUDE_FROM_ALL tests/animation_control_tests.cpp)
  target_link_libraries(animation_control_wasm_tests PRIVATE engine_core)
  target_compile_options(animation_control_wasm_tests PRIVATE -fexceptions)
  target_link_options(animation_control_wasm_tests PRIVATE -fexceptions
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFILESYSTEM=0
    -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  set_target_properties(animation_control_wasm_tests PROPERTIES SUFFIX ".js")
  add_executable(animation_retarget_wasm_tests EXCLUDE_FROM_ALL tests/animation_retarget_tests.cpp)
  target_link_libraries(animation_retarget_wasm_tests PRIVATE engine_editor)
  target_compile_options(animation_retarget_wasm_tests PRIVATE -fexceptions)
  target_link_options(animation_retarget_wasm_tests PRIVATE -fexceptions
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFILESYSTEM=0
    -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  set_target_properties(animation_retarget_wasm_tests PROPERTIES SUFFIX ".js")
  add_executable(ik_wasm_tests EXCLUDE_FROM_ALL tests/ik_tests.cpp)
  target_link_libraries(ik_wasm_tests PRIVATE engine_editor)
  target_compile_options(ik_wasm_tests PRIVATE -fexceptions)
  target_link_options(ik_wasm_tests PRIVATE -fexceptions
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFILESYSTEM=0
    -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  set_target_properties(ik_wasm_tests PROPERTIES SUFFIX ".js")
  add_executable(animation_layers_wasm_tests EXCLUDE_FROM_ALL tests/animation_layers_tests.cpp)
  target_link_libraries(animation_layers_wasm_tests PRIVATE engine_editor)
  target_compile_options(animation_layers_wasm_tests PRIVATE -fexceptions)
  target_link_options(animation_layers_wasm_tests PRIVATE -fexceptions
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFILESYSTEM=0
    -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  set_target_properties(animation_layers_wasm_tests PROPERTIES SUFFIX ".js")
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

if(EMSCRIPTEN)
  add_executable(vehicle_wasm_tests EXCLUDE_FROM_ALL tests/vehicle_tests.cpp)
  target_link_libraries(vehicle_wasm_tests PRIVATE engine_editor)
  target_compile_options(vehicle_wasm_tests PRIVATE -fexceptions)
  target_link_options(vehicle_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  set_target_properties(vehicle_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(EMSCRIPTEN)
  add_executable(physical_surface_wasm_tests EXCLUDE_FROM_ALL tests/physical_surface_tests.cpp)
  target_link_libraries(physical_surface_wasm_tests PRIVATE engine_editor)
  target_compile_options(physical_surface_wasm_tests PRIVATE -fexceptions)
  target_link_options(physical_surface_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  set_target_properties(physical_surface_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(NOT EMSCRIPTEN AND BUILD_TESTING)
 add_executable(camera_follow_tests tests/camera_follow_tests.cpp)
 target_link_libraries(camera_follow_tests PRIVATE engine_editor)
 add_test(NAME camera_follow COMMAND camera_follow_tests)
elseif(EMSCRIPTEN)
 add_executable(camera_follow_wasm_tests EXCLUDE_FROM_ALL tests/camera_follow_tests.cpp)
 target_link_libraries(camera_follow_wasm_tests PRIVATE engine_editor)
 target_compile_options(camera_follow_wasm_tests PRIVATE -fexceptions)
 target_link_options(camera_follow_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
 set_target_properties(camera_follow_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(EMSCRIPTEN)
 add_executable(spline_route_wasm_tests EXCLUDE_FROM_ALL tests/spline_route_tests.cpp)
 target_link_libraries(spline_route_wasm_tests PRIVATE engine_editor)
 target_compile_options(spline_route_wasm_tests PRIVATE -fexceptions)
 target_link_options(spline_route_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
 set_target_properties(spline_route_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(EMSCRIPTEN)
 add_executable(surface_trail_wasm_tests EXCLUDE_FROM_ALL tests/surface_trail_tests.cpp)
 target_link_libraries(surface_trail_wasm_tests PRIVATE engine_editor)
 target_compile_options(surface_trail_wasm_tests PRIVATE -fexceptions)
 target_link_options(surface_trail_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
 set_target_properties(surface_trail_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(EMSCRIPTEN)
 add_executable(audio_wasm_tests EXCLUDE_FROM_ALL tests/audio_tests.cpp)
 target_link_libraries(audio_wasm_tests PRIVATE engine_core)
 target_compile_options(audio_wasm_tests PRIVATE -fexceptions)
 target_link_options(audio_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
 set_target_properties(audio_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(EMSCRIPTEN)
 add_executable(cinematic_sequence_wasm_tests EXCLUDE_FROM_ALL tests/cinematic_sequence_tests.cpp)
 target_link_libraries(cinematic_sequence_wasm_tests PRIVATE engine_editor)
 target_compile_options(cinematic_sequence_wasm_tests PRIVATE -fexceptions)
 target_link_options(cinematic_sequence_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
 set_target_properties(cinematic_sequence_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(EMSCRIPTEN)
 add_executable(replay_wasm_tests EXCLUDE_FROM_ALL tests/replay_tests.cpp)
 target_link_libraries(replay_wasm_tests PRIVATE engine_core)
 target_compile_options(replay_wasm_tests PRIVATE -fexceptions)
 target_link_options(replay_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
 set_target_properties(replay_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(EMSCRIPTEN)
 add_executable(spotlight_wasm_tests EXCLUDE_FROM_ALL tests/spotlight_tests.cpp)
 target_link_libraries(spotlight_wasm_tests PRIVATE engine_editor)
 target_compile_options(spotlight_wasm_tests PRIVATE -fexceptions)
 target_link_options(spotlight_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
 set_target_properties(spotlight_wasm_tests PROPERTIES SUFFIX ".js")
endif()

if(EMSCRIPTEN)
 add_executable(region_stream_wasm_tests EXCLUDE_FROM_ALL tests/region_stream_tests.cpp)
 target_link_libraries(region_stream_wasm_tests PRIVATE engine_editor)
 target_compile_options(region_stream_wasm_tests PRIVATE -fexceptions)
 target_link_options(region_stream_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node -sSTACK_SIZE=2097152)
 set_target_properties(region_stream_wasm_tests PROPERTIES SUFFIX ".cjs")
endif()

option(WEBENGINE_BUILD_WINDOWS "Build the SDL3/Dawn Windows desktop host and vehicle application" OFF)
if(WEBENGINE_BUILD_WINDOWS)
  include(cmake/windows.cmake)
endif()

if(EMSCRIPTEN)
 add_executable(audio_dsp_wasm_tests EXCLUDE_FROM_ALL tests/audio_dsp_tests.cpp)
 target_link_libraries(audio_dsp_wasm_tests PRIVATE engine_core)
 target_compile_options(audio_dsp_wasm_tests PRIVATE -fexceptions)
 target_link_options(audio_dsp_wasm_tests PRIVATE -fexceptions -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1)
 set_target_properties(audio_dsp_wasm_tests PROPERTIES SUFFIX ".js")
endif()

# Optional portable compiled modules and host examples.
include(cmake/modules.cmake)
include(cmake/quests.cmake)
include(cmake/rpg.cmake)
if(WEBENGINE_BUILD_RPG_TESTS)
  enable_testing()
endif()
include(cmake/hybrid_terrain.cmake)
include(cmake/network.cmake)
include(cmake/planet.cmake)

include(cmake/browser.cmake)
include(cmake/host_integration.cmake)
