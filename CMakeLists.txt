cmake_minimum_required(VERSION 3.10)
project(OllamaLib)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(OLLAMLIB_ROOT ${CMAKE_CURRENT_LIST_DIR})

if (CMAKE_CXX_COMPILER_ID
    MATCHES
    "Clang"
    OR CMAKE_CXX_COMPILER_ID
       MATCHES
       "AppleClang")
  add_compile_options(-Wthread-safety -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS)
endif ()

add_subdirectory(ollama)
option(
  OLLAMALIB_BUILD_EXAMPLE
  "Build the example program."
  ON)

if (OLLAMALIB_BUILD_EXAMPLE)
  add_executable(assistant main.cpp)
  target_link_libraries(assistant PRIVATE ollamalib)
endif ()

file(
  CREATE_LINK
  "${CMAKE_BINARY_DIR}/compile_commands.json"
  "${CMAKE_SOURCE_DIR}/compile_commands.json"
  SYMBOLIC)
