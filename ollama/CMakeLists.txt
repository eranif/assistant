project(ollamalib)

set(CMAKE_CXX_STANDARD 17)

add_library(ollamalib STATIC ${CMAKE_CURRENT_LIST_DIR}/ollama.cpp)
target_include_directories(ollamalib PUBLIC ${CMAKE_SOURCE_DIR})

if (OLLAMALIB_WITH_OPENSSL)
  find_package(
    OpenSSL
    COMPONENTS SSL Crypto
    REQUIRED)
  target_compile_options(ollamalib PUBLIC -DCPPHTTPLIB_OPENSSL_SUPPORT=1)
  target_link_libraries(ollamalib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif ()
