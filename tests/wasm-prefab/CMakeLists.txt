cmake_minimum_required(VERSION 3.22)
project(WebEnginePrefabContracts LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Can also build the focused native contracts with -DBUILD_TESTING=OFF.
add_subdirectory("../.." engine)
add_executable(prefab_wasm_tests ../prefab_tests.cpp)
target_link_libraries(prefab_wasm_tests PRIVATE engine_core)
if(EMSCRIPTEN)
  target_compile_options(prefab_wasm_tests PRIVATE -fexceptions)
  target_link_options(prefab_wasm_tests PRIVATE -fexceptions -sENVIRONMENT=node
    -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sFILESYSTEM=0 -sSTACK_SIZE=1048576)
  set_target_properties(prefab_wasm_tests PROPERTIES SUFFIX ".js")
endif()
