add_subdirectory(quests)
option(WEBENGINE_BUILD_QUEST_DEMO "Build rendered quest consumer for the selected host" OFF)
if(NOT WEBENGINE_BUILD_QUESTS OR NOT WEBENGINE_BUILD_QUEST_DEMO)
  return()
endif()
if(WEBENGINE_BUILD_QUEST_TESTS)
add_executable(quest_host_tests tests/quest_host_tests.cpp)
target_link_libraries(quest_host_tests PRIVATE WebEngine::Quests engine_core)
if(EMSCRIPTEN)
  set_target_properties(quest_host_tests PROPERTIES SUFFIX ".js")
  target_link_options(quest_host_tests PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
  add_test(NAME quest_host COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:quest_host_tests>)
else()
  add_test(NAME quest_host COMMAND quest_host_tests)
endif()
endif()
if(EMSCRIPTEN)
  add_executable(quest_demo examples/quests/browser.cpp src/renderer.cpp src/webgpu_host_browser.cpp
    src/webgpu_renderer.cpp src/webgpu_water.cpp src/webgl_renderer.cpp src/webgl_water.cpp src/gui_renderer.cpp)
  target_link_libraries(quest_demo PRIVATE WebEngine::Quests engine_core engine_gui)
  target_include_directories(quest_demo PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_options(quest_demo PRIVATE --use-port=emdawnwebgpu)
  target_link_options(quest_demo PRIVATE --use-port=emdawnwebgpu -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sENVIRONMENT=web "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']"
    "-sEXPORTED_FUNCTIONS=['_main','_quest_demo_command','_quest_demo_epoch','_quest_demo_import','_quest_demo_suspend','_quest_demo_shutdown','_malloc','_free']")
  set_target_properties(quest_demo PROPERTIES SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/quest-site")
  add_custom_command(TARGET quest_demo POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/quests/index.html" "${CMAKE_CURRENT_BINARY_DIR}/quest-site/index.html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/examples/quests/host.js" "${CMAKE_CURRENT_BINARY_DIR}/quest-site/host.js")
elseif(TARGET WebEngine::Desktop)
  add_executable(quest_demo_windows examples/quests/windows.cpp)
  target_link_libraries(quest_demo_windows PRIVATE WebEngine::Quests WebEngine::Desktop)
  target_compile_options(quest_demo_windows PRIVATE /utf-8)
  add_custom_command(TARGET quest_demo_windows POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:quest_demo_windows>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/noto/NotoSans-Regular.ttf" "$<TARGET_FILE_DIR:quest_demo_windows>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:quest_demo_windows>")
else()
  message(FATAL_ERROR "Quest demo needs Emscripten or WEBENGINE_BUILD_WINDOWS")
endif()
