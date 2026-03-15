project(assistant_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})

# Create the test executable
add_executable(test_claude_response_parser test_claude_response_parser.cpp)
add_executable(test_history test_history.cpp)
add_executable(test_env_expander test_env_expander.cpp)
add_executable(test_config test_config.cpp)
add_executable(test_config_file test_config_file.cpp)
add_executable(test_process test_process.cpp)
add_executable(test_openai_client test_openai_client.cpp)
add_executable(test_openai_response_parser test_openai_response_parser.cpp)
add_executable(test_openai_response_format test_openai_response_format.cpp)

# Link libraries
target_link_libraries(
  test_claude_response_parser
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)

target_link_libraries(
  test_openai_client
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)

target_link_libraries(
  test_openai_response_parser
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)

target_link_libraries(
  test_openai_response_format
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)

target_link_libraries(
  test_config_file
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)

target_link_libraries(
  test_config
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)

target_link_libraries(
  test_env_expander
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)

target_link_libraries(
  test_process
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)

target_link_libraries(
  test_history
  PUBLIC assistantlib
         gtest_main
         Threads::Threads)
