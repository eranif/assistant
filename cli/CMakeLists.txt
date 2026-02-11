set(CLI "code-assist")

add_executable(${CLI} main.cpp utils.hpp)
if (ASSISTANTLIB_WITH_OPENSSL)
  target_link_libraries(${CLI} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif ()
target_link_libraries(${CLI} PRIVATE assistantlib)

set_target_properties(${CLI} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                        ${CMAKE_BINARY_DIR})
