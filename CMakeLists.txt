cmake_minimum_required(VERSION 3.10)
project(OllamaLib)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(OLLAMLIB_ROOT ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(ollama)
option(
  OLLAMALIB_BUILD_EXAMPLE
  "Build the example program."
  ON)

if (OLLAMALIB_BUILD_EXAMPLE)
  add_executable(assistant main.cpp)
  target_link_libraries(assistant PRIVATE ollamalib)
endif ()
