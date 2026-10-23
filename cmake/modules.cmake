add_subdirectory(modules)
option(WEBENGINE_BUILD_MODULE_DEMO "Build rendered module consumer for the selected host" OFF)
if(NOT WEBENGINE_BUILD_MODULES OR NOT WEBENGINE_BUILD_MODULE_DEMO)
  return()
endif()
add_subdirectory(examples/modules/counter)
if(EMSCRIPTEN)
  add_executable(module_demo examples/modules/browser.cpp src/renderer.cpp src/webgpu_host_browser.cpp
    src/webgpu_renderer.cpp src/webgpu_water.cpp src/webgl_renderer.cpp src/webgl_water.cpp src/gui_renderer.cpp)
  target_link_libraries(module_demo PRIVATE Example::Counter engine_core engine_gui)
  target_include_directories(module_demo PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(module_demo PRIVATE --use-port=emdawnwebgpu)
  target_link_options(module_demo PRIVATE --use-port=emdawnwebgpu -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=web
    "-sEXPORTED_FUNCTIONS=['_main','_module_demo_command']")
  set_target_properties(module_demo PROPERTIES SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/module-site")
  add_custom_command(TARGET module_demo POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/modules/index.html" "${CMAKE_CURRENT_BINARY_DIR}/module-site/index.html")
elseif(TARGET WebEngine::Desktop)
  add_executable(module_demo_windows examples/modules/windows.cpp)
  target_link_libraries(module_demo_windows PRIVATE Example::Counter WebEngine::Desktop)
  target_compile_options(module_demo_windows PRIVATE /utf-8)
  add_custom_command(TARGET module_demo_windows POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:module_demo_windows>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/noto/NotoSans-Regular.ttf" "$<TARGET_FILE_DIR:module_demo_windows>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:module_demo_windows>")
else()
  message(FATAL_ERROR "Module demo needs Emscripten or WEBENGINE_BUILD_WINDOWS")
endif()
