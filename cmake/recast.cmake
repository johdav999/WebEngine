# Unmodified upstream Recast/Detour 1.6.0; see third_party/recastnavigation/README.md.
file(GLOB RECAST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/recastnavigation/Recast/Source/*.cpp")
file(GLOB DETOUR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/recastnavigation/Detour/Source/*.cpp")
add_library(engine_recast STATIC ${RECAST_SOURCES})
target_include_directories(engine_recast PUBLIC third_party/recastnavigation/Recast/Include)
add_library(engine_detour STATIC ${DETOUR_SOURCES})
target_include_directories(engine_detour PUBLIC third_party/recastnavigation/Detour/Include)
