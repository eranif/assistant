add_library(
  mcp-cpp STATIC
  mcp_message.cpp
  mcp_resource.cpp
  mcp_server.cpp
  mcp_tool.cpp
  mcp_stdio_client.cpp
  mcp_sse_client.cpp)
target_compile_options(mcp-cpp PRIVATE -fPIC)

target_compile_options(mcp-cpp PUBLIC -Wall)
target_link_libraries(mcp-cpp PUBLIC ${CMAKE_THREAD_LIBS_INIT})
target_include_directories(mcp-cpp PUBLIC ${OLLAMLIB_ROOT})
target_include_directories(mcp-cpp PRIVATE ${CMAKE_CURRENT_LIST_DIR})

if (OLLAMALIB_WITH_OPENSSL)
  target_compile_options(mcp-cpp PUBLIC -DCPPHTTPLIB_OPENSSL_SUPPORT=1)
  target_link_libraries(mcp-cpp PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif ()

if (CMAKE_CXX_COMPILER_ID
    MATCHES
    "Clang"
    OR CMAKE_CXX_COMPILER_ID
       MATCHES
       "AppleClang")
  target_compile_options(mcp-cpp PUBLIC -Wno-deprecated-literal-operator)
  target_compile_options(mcp-cpp PUBLIC -Wno-string-plus-int)
  target_compile_options(mcp-cpp PUBLIC -Wno-deprecated-literal-operator)
endif ()
