cmake_minimum_required(VERSION 4.1)
project(tdchess)

set(CMAKE_CXX_STANDARD 23)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if (COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall")
endif ()


add_executable(tdchess main.cpp)
