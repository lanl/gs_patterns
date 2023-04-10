cmake_minimum_required(VERSION 3.1...3.25)

project(
  gs_patterns
  VERSION 1.0
  LANGUAGES C)

add_executable(gs_patterns gs_patterns.c)
set (CMAKE_C_FLAGS "-lz -lm")
