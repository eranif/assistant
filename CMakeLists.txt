cmake_minimum_required(VERSION 3.10)
project(OllamaLib)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(ollama)

if (OLLAMALIB_BUILD_EXAMPLE)
  add_executable(assistant main.cpp)
  target_link_libraries(assistant PRIVATE ollamalib)
endif ()
