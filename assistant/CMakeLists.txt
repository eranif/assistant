project(assistantlib)

add_subdirectory(cpp-mcp)

add_library(
  assistantlib STATIC
  ${CMAKE_CURRENT_LIST_DIR}/ollama_client.cpp
  ${CMAKE_CURRENT_LIST_DIR}/ollama_client.hpp
  ${CMAKE_CURRENT_LIST_DIR}/claude_client.cpp
  ${CMAKE_CURRENT_LIST_DIR}/claude_client.hpp
  ${CMAKE_CURRENT_LIST_DIR}/mcp.cpp
  ${CMAKE_CURRENT_LIST_DIR}/mcp.hpp
  ${CMAKE_CURRENT_LIST_DIR}/function.cpp
  ${CMAKE_CURRENT_LIST_DIR}/function.hpp
  ${CMAKE_CURRENT_LIST_DIR}/function_base.cpp
  ${CMAKE_CURRENT_LIST_DIR}/function_base.hpp
  ${CMAKE_CURRENT_LIST_DIR}/client_base.cpp
  ${CMAKE_CURRENT_LIST_DIR}/client_base.hpp
  ${CMAKE_CURRENT_LIST_DIR}/config.cpp
  ${CMAKE_CURRENT_LIST_DIR}/config.hpp
  ${CMAKE_CURRENT_LIST_DIR}/claude_response_parser.cpp
  ${CMAKE_CURRENT_LIST_DIR}/claude_response_parser.hpp)

target_include_directories(assistantlib PUBLIC ${OLLAMLIB_ROOT})
target_include_directories(assistantlib PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(assistantlib PUBLIC mcp-cpp)

target_compile_definitions(assistantlib PRIVATE JSON_USE_IMPLICIT_CONVERSIONS=0)

if (ASSISTANTLIB_WITH_OPENSSL)
  target_compile_definitions(assistantlib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT=1)
  target_link_libraries(assistantlib PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  if (WIN32)
    target_link_libraries(assistantlib PUBLIC Crypt32)
  endif ()
endif ()

if (UNIX OR APPLE)
  set_target_properties(assistantlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

if (WIN32)
  target_link_libraries(assistantlib PUBLIC Ws2_32)
endif ()

if (CMAKE_CXX_COMPILER_ID
    MATCHES
    "Clang"
    OR CMAKE_CXX_COMPILER_ID
       MATCHES
       "AppleClang")
  target_compile_options(assistantlib PUBLIC -Wno-deprecated-literal-operator)
  target_compile_options(assistantlib PUBLIC -Wno-string-plus-int)
  target_compile_options(assistantlib PUBLIC -Wno-deprecated-literal-operator)
endif ()
