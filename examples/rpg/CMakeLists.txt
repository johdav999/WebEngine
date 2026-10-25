cmake_minimum_required(VERSION 3.22)
project(WebEngineRpgConsumer LANGUAGES CXX)
include(CTest)
set(WEBENGINE_SOURCE_DIR "" CACHE PATH "WebEngine checkout containing modules/ and rpg/")
if(NOT EXISTS "${WEBENGINE_SOURCE_DIR}/rpg/CMakeLists.txt")
  message(FATAL_ERROR "Set WEBENGINE_SOURCE_DIR to a WebEngine checkout with rpg/")
endif()
add_subdirectory("${WEBENGINE_SOURCE_DIR}/modules" engine-modules)
if(WEBENGINE_BUILD_QUESTS)
  add_subdirectory("${WEBENGINE_SOURCE_DIR}/quests" engine-quests)
endif()
add_subdirectory("${WEBENGINE_SOURCE_DIR}/rpg" engine-rpg)
if(TARGET WebEngine::RPG)
  add_executable(rpg_consumer consumer.cpp)
  target_link_libraries(rpg_consumer PRIVATE WebEngine::RPG)
  add_executable(rpg_character_consumer character_consumer.cpp)
  target_link_libraries(rpg_character_consumer PRIVATE WebEngine::RPG)
  if(EMSCRIPTEN)
    set_target_properties(rpg_character_consumer PROPERTIES SUFFIX ".js")
    target_link_options(rpg_character_consumer PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  endif()
  add_test(NAME rpg_character_consumer COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:rpg_character_consumer>)
else()
  if(TARGET engine_rpg OR TARGET rpg_foundation_tests)
    message(FATAL_ERROR "Disabled RPG leaked a target")
  endif()
  add_executable(rpg_consumer disabled.cpp)
endif()
if(EMSCRIPTEN)
  set_target_properties(rpg_consumer PROPERTIES SUFFIX ".js")
  target_link_options(rpg_consumer PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1)
endif()
add_test(NAME rpg_consumer COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:rpg_consumer>)
if(WEBENGINE_BUILD_RPG_TESTS)
  add_test(NAME rpg_cmake_contract COMMAND "${CMAKE_COMMAND}"
    "-DENGINE_SOURCE=${WEBENGINE_SOURCE_DIR}"
    "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/configuration-tests"
    "-DGENERATOR=${CMAKE_GENERATOR}"
    "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}"
    -P "${WEBENGINE_SOURCE_DIR}/tests/rpg_cmake_contract.cmake")
  set_tests_properties(rpg_cmake_contract PROPERTIES TIMEOUT 180)
  add_test(NAME rpg_bridge_off COMMAND "${CMAKE_COMMAND}"
    "-DENGINE_SOURCE=${WEBENGINE_SOURCE_DIR}"
    "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/bridge-off-tests"
    "-DGENERATOR=${CMAKE_GENERATOR}" "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}" -DONLY_BRIDGE_OFF=ON
    -P "${WEBENGINE_SOURCE_DIR}/tests/rpg_cmake_contract.cmake")
  # This test compiles the complete base RPG library, unlike the disabled-consumer matrix.
  set_tests_properties(rpg_bridge_off PROPERTIES TIMEOUT 300)
endif()
# Structural check: only portable Modules/RPG and explicitly selected Quests are allowed.
if(NOT WEBENGINE_BUILD_RPG_QUESTS AND TARGET engine_rpg_quests)
  message(FATAL_ERROR "Disabled bridge leaked a target")
endif()
if(NOT WEBENGINE_BUILD_QUESTS AND TARGET engine_quests)
  message(FATAL_ERROR "Base RPG unexpectedly configured Quests")
endif()
foreach(forbidden engine_core Jolt engine_desktop engine_gui)
  if(TARGET ${forbidden})
    message(FATAL_ERROR "Standalone RPG consumer configured forbidden target: ${forbidden}")
  endif()
endforeach()
