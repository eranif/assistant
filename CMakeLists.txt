cmake_minimum_required(VERSION 3.10)
project(HelloWorld)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(OpenSSL COMPONENTS SSL Crypto REQUIRED)

add_executable(helloworld main.cpp)
target_compile_options(helloworld PRIVATE -DCPPHTTPLIB_OPENSSL_SUPPORT=1)
target_link_libraries(helloworld PRIVATE OpenSSL::SSL OpenSSL::Crypto)
