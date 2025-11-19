#!/bin/bash

# Script to run tests with AddressSanitizer/LeakSanitizer
# Optimized for leak detection and memory error detection

set -e

if [ $# -eq 0 ]; then
    echo "Usage: $0 <test_executable> [test_args...]"
    echo "Examples:"
    echo "  $0 containers_tests"
    echo "  $0 hazard_tests --gtest_filter=*CacheLine*"
    echo "  $0 containers_tests --gtest_list_tests"
    echo ""
    echo "Note: Build with -DENABLE_ASAN=ON and/or -DENABLE_LSAN=ON for leak detection"
    exit 1
fi

TEST_EXECUTABLE="$1"
shift

# Check if executable exists
if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo "Error: Test executable '$TEST_EXECUTABLE' not found"
    echo "Build with: cmake -DENABLE_ASAN=ON -DENABLE_LSAN=ON .."
    exit 1
fi

# Set AddressSanitizer options for comprehensive leak detection
export ASAN_OPTIONS="halt_on_error=1:abort_on_error=1:check_initialization_order=1:strict_init_order=1:detect_odr_violation=2:detect_stack_use_after_return=true:check_initialization_order=1:strict_string_checks=1"

# Set LeakSanitizer options
export LSAN_OPTIONS="halt_on_error=1:print_stats=1:print_suppressions=false:report_objects=1"

# Set malloc options for better debugging
export MALLOC_CHECK_=3

echo "🔍 Running $TEST_EXECUTABLE with leak detection..."
echo "   ASAN_OPTIONS: $ASAN_OPTIONS"
echo "   LSAN_OPTIONS: $LSAN_OPTIONS"
echo "   MALLOC_CHECK_: $MALLOC_CHECK_"
echo

# Run the test executable
"$TEST_EXECUTABLE" "$@"

echo
echo "✅ Leak detection completed successfully!"