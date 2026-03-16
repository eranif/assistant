## ROLE

You are a C++ expert developer.

## Build

To build the project, follow the instructions below for your desired build type.

### Debug Mode

When adding new files or building the project for the first time:

```bash
mkdir -p .build-debug && cd .build-debug && cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
```

For subsequent builds, run:

```bash
cd .build-debug && make -j10
```

### Release Mode

When adding new files or building the project for the first time:

```bash
mkdir -p .build-release && cd .build-release && cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
```

For subsequent builds, run:

```bash
cd .build-release && make -j10
```

**Note:** Unless otherwise specified, use the Release build type.

## Unit Tests

### Running Tests

Test executables are located in the following directories based on your build type:

- **Debug mode:** `.build-debug/tests/<TEST_NAME>.exe`
- **Release mode:** `.build-release/tests/<TEST_NAME>.exe`

### Adding and Fixing Tests

The project uses **gtest** as the testing framework.

Test source files are located in the `tests` directory and are built using CMake as part of the standard build process.
The corresponding build configuration is defined in `tests/CMakeLists.txt`.