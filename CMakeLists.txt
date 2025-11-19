cmake_minimum_required(VERSION 3.11)
project(Concurrency)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optional sanitizer support
option(ENABLE_TSAN "Enable ThreadSanitizer (TSan) for detecting data races" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer (ASan) for detecting memory errors" OFF)
option(ENABLE_LSAN "Enable LeakSanitizer (LSan) for detecting memory leaks" OFF)

# Validate sanitizer combinations (TSan is incompatible with ASan/LSan)
if(ENABLE_TSAN AND (ENABLE_ASAN OR ENABLE_LSAN))
    message(FATAL_ERROR "ThreadSanitizer is incompatible with AddressSanitizer/LeakSanitizer")
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    set(TSAN_FLAGS "-fsanitize=thread -fno-omit-frame-pointer -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TSAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TSAN_FLAGS}")
    
    # Set environment variables for ThreadSanitizer
    set(ENV{TSAN_OPTIONS} "halt_on_error=1:abort_on_error=1:detect_thread_leaks=false")
endif()

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    set(ASAN_FLAGS "-fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ASAN_FLAGS}")
    
    # Set environment variables for AddressSanitizer
    set(ENV{ASAN_OPTIONS} "halt_on_error=1:abort_on_error=1:check_initialization_order=1:strict_init_order=1")
endif()

if(ENABLE_LSAN)
    message(STATUS "LeakSanitizer enabled")
    if(NOT ENABLE_ASAN)
        # LSan can be used standalone
        set(LSAN_FLAGS "-fsanitize=leak -fno-omit-frame-pointer -g")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LSAN_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LSAN_FLAGS}")
    endif()
    
    # Set environment variables for LeakSanitizer
    set(ENV{LSAN_OPTIONS} "halt_on_error=1:abort_on_error=1:print_stats=1")
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

# Add dynamic only test executable
add_executable(dynamic_only_test
    hazard/test/dynamic_only_test.cpp
)

# Link dynamic only test executable with Google Test and the library
target_link_libraries(dynamic_only_test
    PRIVATE
        ${PROJECT_NAME}
        gtest
        gtest_main
        pthread
)

# Add tests to CTest
include(GoogleTest)
# Always discover tests regardless of sanitizer settings
gtest_discover_tests(containers_tests)
gtest_discover_tests(hazard_tests)
gtest_discover_tests(stack_static_tests)
