project(ollamalib)

set(CMAKE_CXX_STANDARD 17)
if (OLLAMALIB_WITH_OPENSSL)
  find_package(
    OpenSSL
    COMPONENTS SSL Crypto
    REQUIRED)
endif ()

add_subdirectory(cpp-mcp)

add_library(
  ollamalib STATIC
  ${CMAKE_CURRENT_LIST_DIR}/ollama.cpp
  ${CMAKE_CURRENT_LIST_DIR}/ollama.hpp
  ${CMAKE_CURRENT_LIST_DIR}/mcp_local_process.cpp
  ${CMAKE_CURRENT_LIST_DIR}/mcp_local_process.hpp
  ${CMAKE_CURRENT_LIST_DIR}/function.cpp
  ${CMAKE_CURRENT_LIST_DIR}/function.hpp
  ${CMAKE_CURRENT_LIST_DIR}/function_base.cpp
  ${CMAKE_CURRENT_LIST_DIR}/function_base.hpp
  ${CMAKE_CURRENT_LIST_DIR}/config.cpp
  ${CMAKE_CURRENT_LIST_DIR}/config.hpp)

target_include_directories(ollamalib PUBLIC ${OLLAMLIB_ROOT})
target_include_directories(ollamalib PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(ollamalib PUBLIC mcp-cpp)

if (OLLAMALIB_WITH_OPENSSL)
  target_compile_options(ollamalib PUBLIC -DCPPHTTPLIB_OPENSSL_SUPPORT=1)
  target_link_libraries(ollamalib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
  if (WIN32)
    target_link_libraries(ollamalib PUBLIC Crypt32)
  endif ()
endif ()

if (WIN32)
  target_link_libraries(ollamalib PUBLIC Ws2_32)
endif ()

if (CMAKE_CXX_COMPILER_ID
    MATCHES
    "Clang"
    OR CMAKE_CXX_COMPILER_ID
       MATCHES
       "AppleClang")
  target_compile_options(ollamalib PUBLIC -Wno-deprecated-literal-operator)
  target_compile_options(ollamalib PUBLIC -Wno-string-plus-int)
  target_compile_options(ollamalib PUBLIC -Wno-deprecated-literal-operator)
endif ()
