cmake_minimum_required(VERSION 3.10)
project(tdchess)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 99)

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
target_link_options(tdchess_uci PRIVATE
        "-flto"
#        "-fprofile-use=./pgo"
)
target_compile_options(tdchess_uci PRIVATE
        "-O3"
        "-march=native"
        "-mtune=native"
        "-funroll-loops"
        "-fno-trapping-math"
        "-fipa-pta"
        "-fomit-frame-pointer"
        "-Wall"
        "-Wextra"
#        "-fprofile-dir=./pgo"
#        "-fprofile-correction"
)

add_executable(tdchess_test src/main.cpp)
target_link_libraries(tdchess_test PRIVATE fathom)
#target_link_options(tdchess_test PRIVATE
#        -fsanitize=undefined
#)
target_compile_options(tdchess_test PRIVATE
        "-O3"
        "-march=native"
        "-Wall"
#        -fsanitize=undefined
#        -g
)

add_executable(tdchess_elo src/main.cpp)
target_link_libraries(tdchess_elo PRIVATE fathom)
target_compile_definitions(tdchess_elo PRIVATE TDCHESS_ELO)
target_include_directories(tdchess_elo PRIVATE ${Boost_INCLUDE_DIRS})
#target_link_options(tdchess_elo PRIVATE
#        -fsanitize=undefined
#)
target_compile_options(tdchess_elo PRIVATE
#                -fsanitize=undefined
#                -g
        "-O3"
        "-march=native"
        "-Wall"
        "-Wextra"
)

add_executable(tdchess_pgo src/main.cpp)
target_link_libraries(tdchess_pgo PRIVATE fathom)
target_compile_definitions(tdchess_pgo PRIVATE TDCHESS_PGO)
target_link_options(tdchess_pgo PRIVATE
        "-fprofile-generate=./pgo"
)
target_compile_options(tdchess_pgo PRIVATE
        "-O3"
        "-march=native"
        "-mtune=native"
        "-funroll-loops"
        "-fno-trapping-math"
        "-fipa-pta"
        "-fomit-frame-pointer"
        "-Wall"
        "-Wextra"
        "-fprofile-dir=./pgo"
        "-fprofile-generate=./pgo"
)