cmake_minimum_required(VERSION 3.10)
project(tdchess)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 99)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if (COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall")
endif ()

# static link with fathom
set(FATHOM_SRC lib/Fathom/src/tbprobe.c)
add_library(fathom SHARED ${FATHOM_SRC})
target_include_directories(fathom PUBLIC lib/Fathom/src)
set_target_properties(fathom PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(tdchess main.cpp)
target_link_libraries(tdchess PRIVATE fathom)
