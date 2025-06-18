cmake_minimum_required(VERSION 3.11)
project(Concurrency)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#message(STATUS ${CMAKE_CURRENT_SOURCE_DIR})
#
#message(STATUS ${CMAKE_CURRENT_BINARY_DIR})
#
#message(STATUS ${CMAKE_INSTALL_INCLUDEDIR})

# Header-only library
add_library(${PROJECT_NAME} INTERFACE)

target_compile_options(${PROJECT_NAME} INTERFACE
)

target_include_directories( ${PROJECT_NAME}
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stack/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/hazard>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

# Include FetchContent for downloading dependencies
include(FetchContent)

# Fetch Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Add test executable
add_executable(stack_tests
    stack/test/test_stack.cpp
)

# Link test executable with Google Test and the stack library
target_link_libraries(stack_tests
    PRIVATE
        ${PROJECT_NAME}
        gtest
        gtest_main
        pthread
)

# Add test to CTest
include(GoogleTest)
gtest_discover_tests(stack_tests)
