cmake_minimum_required(VERSION 3.10)
project(Assistant)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(OLLAMLIB_ROOT ${CMAKE_CURRENT_LIST_DIR})

if (CMAKE_CXX_COMPILER_ID
    MATCHES
    "Clang"
    OR CMAKE_CXX_COMPILER_ID
       MATCHES
       "AppleClang")
  add_compile_options(-Wthread-safety
                      -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS)
endif ()

option(
  ASSISTANTLIB_BUILD_EXAMPLE
  "Build the example program."
  ON)

option(
  ASSISTANTLIB_WITH_OPENSSL
  "Enable TLS support"
  ON)

if (ASSISTANTLIB_WITH_OPENSSL)
  message(STATUS "TLS support is enabled")
endif ()

add_subdirectory(assistant)
if (ASSISTANTLIB_BUILD_EXAMPLE)
  add_executable(assistant-demo main.cpp)
  target_link_libraries(assistant-demo PRIVATE assistantlib)
endif ()

file(
  CREATE_LINK
  "${CMAKE_BINARY_DIR}/compile_commands.json"
  "${CMAKE_SOURCE_DIR}/compile_commands.json"
  SYMBOLIC)
