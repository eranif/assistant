project(assistant_tests)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Include GoogleTest module for gtest_discover_tests
include(GoogleTest)
enable_testing()

# Include directories
include_directories(${CMAKE_SOURCE_DIR})

# Function to add a gtest executable
function (add_gtest TEST_NAME)
  # Create the test executable with provided source files
  add_executable(${TEST_NAME} ${ARGN})

  # Link required libraries
  target_link_libraries(
    ${TEST_NAME}
    PUBLIC assistantlib
           gtest_main
           Threads::Threads)

  # Discover tests
  gtest_discover_tests(${TEST_NAME})
endfunction ()

# Add all test executables
add_gtest(test_claude_response_parser test_claude_response_parser.cpp)
add_gtest(test_openai_client test_openai_client.cpp)
add_gtest(test_openai_response_parser test_openai_response_parser.cpp)
add_gtest(test_openai_response_format test_openai_response_format.cpp)
add_gtest(test_config_file test_config_file.cpp)
add_gtest(test_config test_config.cpp)
add_gtest(test_env_expander test_env_expander.cpp)
add_gtest(test_process test_process.cpp)
add_gtest(test_history test_history.cpp)
