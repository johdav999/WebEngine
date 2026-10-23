# Can be consumed on its own with add_subdirectory(WebEngine/modules), without
# renderer, physics, network dependency discovery or engine demo targets.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/WebEngineModule.cmake")
option(WEBENGINE_BUILD_MODULES "Build portable compiled-module composition" ON)
option(WEBENGINE_BUILD_MODULE_TESTS "Build module contract tests" OFF)
if(NOT WEBENGINE_BUILD_MODULES)
  return()
endif()
webengine_add_module(engine_modules ALIAS WebEngine::Modules
  PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../include"
  SOURCES ../src/modules.cpp)
if(WEBENGINE_BUILD_MODULE_TESTS)
  add_executable(module_tests ../tests/modules_tests.cpp)
  target_link_libraries(module_tests PRIVATE WebEngine::Modules)
  if(EMSCRIPTEN)
    set_target_properties(module_tests PROPERTIES SUFFIX ".js")
    target_link_options(module_tests PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
  else()
    add_test(NAME modules COMMAND module_tests)
  endif()
endif()
