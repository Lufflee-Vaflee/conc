#!/bin/bash

# Script to run tests with ThreadSanitizer
# Disables ASLR to avoid memory mapping conflicts

set -e

if [ $# -eq 0 ]; then
    echo "Usage: $0 <test_executable> [test_args...]"
    echo "Examples:"
    echo "  $0 containers_tests"
    echo "  $0 hazard_tests --gtest_filter=*CacheLine*"
    echo "  $0 containers_tests --gtest_list_tests"
    exit 1
fi

TEST_EXECUTABLE="$1"
shift

# Check if executable exists
if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo "Error: Test executable '$TEST_EXECUTABLE' not found"
    exit 1
fi

# Set ThreadSanitizer options
export TSAN_OPTIONS="mmap_limit_mb=0:detect_thread_leaks=false"

echo "🧵 Running $TEST_EXECUTABLE with ThreadSanitizer..."
echo "   TSAN_OPTIONS: $TSAN_OPTIONS"
echo "   Disabling ASLR for compatibility"
echo

# Run with ASLR disabled to avoid memory mapping conflicts
setarch x86_64 -R "$TEST_EXECUTABLE" "$@"
