cmake_minimum_required(VERSION 3.1...3.25)

project( gs_patterns VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED On)
#set(CMAKE_CXX_EXTENSIONS Off)

add_library(gs_patterns_core SHARED
        utils.h
        utils.cpp
        gs_patterns.h
        gs_patterns_core.h
        gs_patterns_core.cpp
        gsnv_patterns.h
        gsnv_patterns.cpp
        gspin_patterns.h
        gspin_patterns.cpp
)

add_executable( gs_patterns
        gs_patterns_main.cpp
)

target_link_libraries(gs_patterns gs_patterns_core)

set(CMAKE_CXX_STANDARD_LIBRARIES "-lm -lz ${CMAKE_CXX_STANDARD_LIBRARIES}")
