cmake_minimum_required(VERSION 3.10)
project(tdchess)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 99)

if (POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif ()
# find_package(Boost REQUIRED)


# static link with fathom
set(FATHOM_SRC lib/Fathom/src/tbprobe.c)
set_source_files_properties(${FATHOM_SRC} PROPERTIES LANGUAGE CXX)
add_library(fathom STATIC ${FATHOM_SRC})
target_include_directories(fathom PUBLIC lib/Fathom/src)
set_target_properties(fathom PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
)
target_link_options(fathom PRIVATE
        "-flto"
)
target_compile_options(fathom PRIVATE
        "-O3"
        "-ffast-math"
        "-march=native"
        "-mtune=native"
        "-fomit-frame-pointer"
        "-Wall"
        "-Wextra"
        "-flto"
        -DNDEBUG
        -mcpu=native
        -fno-exceptions
)

add_executable(tdchess_uci src/main.cpp)
target_link_libraries(tdchess_uci PRIVATE fathom)
target_compile_definitions(tdchess_uci PRIVATE TDCHESS_UCI)
target_link_options(tdchess_uci PRIVATE
        "-flto"
)
target_compile_options(tdchess_uci PRIVATE
        "-O3"
        "-ffast-math"
        "-march=native"
        "-mtune=native"
        "-flto"
        "-fomit-frame-pointer"
        "-Wall"
        "-Wextra"
        -DNDEBUG
        -mcpu=native
        -fno-exceptions
)


add_executable(tdchess_uci_tune src/main.cpp)
target_link_libraries(tdchess_uci_tune PRIVATE fathom)
target_compile_definitions(tdchess_uci_tune PRIVATE TDCHESS_UCI)
target_compile_definitions(tdchess_uci_tune PRIVATE TDCHESS_TUNE)
target_link_options(tdchess_uci_tune PRIVATE
        "-flto"
)
target_compile_options(tdchess_uci_tune PRIVATE
        "-O3"
        "-ffast-math"
        "-march=native"
        "-mtune=native"
        "-flto"
        "-fomit-frame-pointer"
        "-Wall"
        "-Wextra"
        -DNDEBUG
        -mcpu=native
        -fno-exceptions
)


# if (CARE_PGO)
#     message("PGO STUFF")
#     if (PGOGEN)
#         message("generating profile")
#         target_link_options(tdchess_uci PRIVATE
#                 "-fprofile-generate"
#         )
#         target_compile_options(tdchess_uci PRIVATE
#                 "-fprofile-generate"
#         )
#     else ()
#         if (EXISTS "${CMAKE_BINARY_DIR}/tdchess_uci.profdata")
#             message("using profdata")
#             target_link_options(tdchess_uci PRIVATE
#                     "-fprofile-use=${CMAKE_BINARY_DIR}/tdchess_uci.profdata"
#             )
#             target_compile_options(tdchess_uci PRIVATE
#                     "-fprofile-use=${CMAKE_BINARY_DIR}/tdchess_uci.profdata"
#             )
#         else ()
#             message("warning, no profdata")
#         endif ()
#     endif ()
# endif ()


add_executable(tdchess_test src/main.cpp)
target_link_libraries(tdchess_test PRIVATE fathom)
target_link_options(tdchess_test PRIVATE
        -fsanitize=undefined
        -fsanitize=address
)
target_compile_options(tdchess_test PRIVATE
        -fsanitize=undefined
        -fsanitize=address
        -g
        -fwrapv
        -D_GLIBCXX_DEBUG
)
