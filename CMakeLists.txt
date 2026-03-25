cmake_minimum_required(VERSION 3.16)
project(CauseDeb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build in Release by default if not specified.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

include_directories(src)

# -----------------------------------------------------------------------
# Collector server
# -----------------------------------------------------------------------
add_executable(causedeb_collector
    src/collector_server.cpp
)
target_link_libraries(causedeb_collector PRIVATE pthread)

# -----------------------------------------------------------------------
# Replay engine
# -----------------------------------------------------------------------
add_executable(causedeb_replay
    src/replay_engine.cpp
)

# -----------------------------------------------------------------------
# Trace analyzer
# -----------------------------------------------------------------------
add_executable(causedeb_analyze
    src/causedeb_analyze.cpp
)

# -----------------------------------------------------------------------
# Install targets
# -----------------------------------------------------------------------
install(TARGETS causedeb_collector causedeb_replay causedeb_analyze
        RUNTIME DESTINATION bin)
install(FILES src/causedeb.h src/trace_io.h
        DESTINATION include/causedeb)
