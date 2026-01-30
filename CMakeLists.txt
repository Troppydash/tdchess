cmake_minimum_required(VERSION 3.10)
project(tdchess)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 99)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if (COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall")
endif ()

if (POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif ()
find_package(Boost REQUIRED)


# static link with fathom
set(FATHOM_SRC lib/Fathom/src/tbprobe.c)
add_library(fathom SHARED ${FATHOM_SRC})
target_include_directories(fathom PUBLIC lib/Fathom/src)
set_target_properties(fathom PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(tdchess_uci src/main.cpp)
target_link_libraries(tdchess_uci PRIVATE fathom)
target_compile_definitions(tdchess_uci PRIVATE TDCHESS_UCI)

add_executable(tdchess_test src/main.cpp)
target_link_libraries(tdchess_test PRIVATE fathom)

add_executable(tdchess_elo src/main.cpp)
target_link_libraries(tdchess_elo PRIVATE fathom)
target_compile_definitions(tdchess_elo PRIVATE TDCHESS_ELO)
target_include_directories(tdchess_elo PRIVATE ${Boost_INCLUDE_DIRS})