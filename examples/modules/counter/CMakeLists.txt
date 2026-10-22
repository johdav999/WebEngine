webengine_add_module(example_counter ALIAS Example::Counter
  PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include"
  SOURCES counter.cpp PUBLIC_DEPENDENCIES WebEngine::Modules)
