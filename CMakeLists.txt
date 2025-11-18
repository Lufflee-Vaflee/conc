cmake_minimum_required(VERSION 3.11)
project(Concurrency)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optional ThreadSanitizer support
option(ENABLE_TSAN "Enable ThreadSanitizer (TSan) for detecting data races" OFF)
if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    set(TSAN_FLAGS "-fsanitize=thread -fno-omit-frame-pointer -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TSAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TSAN_FLAGS}")
    
    # Set environment variables for ThreadSanitizer
    set(ENV{TSAN_OPTIONS} "halt_on_error=1:abort_on_error=1:detect_thread_leaks=false")
endif()

# Header-only library
add_library(${PROJECT_NAME} INTERFACE)

target_compile_options(${PROJECT_NAME} INTERFACE
    -Wno-interference-size
    -fstrict-aliasing
)

target_include_directories(${PROJECT_NAME}
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/containers>"
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

# Add test executable for stack
add_executable(containers_tests
    containers/test/test_stack.cpp
    containers/test/test_queue.cpp
)

# Link test executable with Google Test and the stack library
target_link_libraries(containers_tests
    PRIVATE
        ${PROJECT_NAME}
        gtest
        gtest_main
        pthread
)

# Add static memory test executable for performance profiling
add_executable(stack_static_tests
    containers/test/test_stack_static_memory.cpp
)

# Link static test executable with Google Test and the library
target_link_libraries(stack_static_tests
    PRIVATE
        ${PROJECT_NAME}
        gtest
        gtest_main
        pthread
)

# Add hazard tests executable (includes allocator and deque tests)
add_executable(hazard_tests
    hazard/test/test_cache_aligned.cpp
    hazard/test/test_domain.cpp
    hazard/test/test_hazard_pointer.cpp
    hazard/test/stress_test_hazard_pointer.cpp
)

# Link hazard tests executable with Google Test and the library
target_link_libraries(hazard_tests
    PRIVATE
        ${PROJECT_NAME}
        gtest
        gtest_main
        pthread
)

# Add tests to CTest
include(GoogleTest)
if(NOT ENABLE_TSAN)
    # Only discover tests when not using ThreadSanitizer to avoid build-time execution issues
    gtest_discover_tests(containers_tests)
    gtest_discover_tests(hazard_tests)
    gtest_discover_tests(stack_static_tests)
else()
    # When using TSan, add tests manually without discovery
    add_test(NAME containers_tests COMMAND containers_tests)
    add_test(NAME hazard_tests COMMAND hazard_tests)
    add_test(NAME stack_static_tests COMMAND stack_static_tests)
endif()
