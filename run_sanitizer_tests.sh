#!/bin/bash

# Combined script to run tests with different sanitizers
# Supports ThreadSanitizer, AddressSanitizer, and LeakSanitizer

set -e

show_usage() {
    echo "Usage: $0 <sanitizer_type> <test_executable> [test_args...]"
    echo ""
    echo "Sanitizer types:"
    echo "  tsan    - ThreadSanitizer (data race detection)"
    echo "  asan    - AddressSanitizer (memory error detection)"
    echo "  lsan    - LeakSanitizer (memory leak detection)"
    echo "  leak    - Alias for lsan"
    echo ""
    echo "Examples:"
    echo "  $0 tsan containers_tests"
    echo "  $0 asan hazard_tests --gtest_filter=*CacheLine*"
    echo "  $0 leak containers_tests --gtest_list_tests"
    echo ""
    echo "Build requirements:"
    echo "  For tsan: cmake -DENABLE_TSAN=ON .."
    echo "  For asan: cmake -DENABLE_ASAN=ON .."
    echo "  For lsan: cmake -DENABLE_LSAN=ON .."
}

if [ $# -lt 2 ]; then
    show_usage
    exit 1
fi

SANITIZER_TYPE="$1"
TEST_EXECUTABLE="$2"
shift 2

# Check if executable exists
if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo "Error: Test executable '$TEST_EXECUTABLE' not found"
    echo "Make sure you've built with the appropriate sanitizer flags."
    exit 1
fi

case "$SANITIZER_TYPE" in
    "tsan")
        echo "🧵 Running with ThreadSanitizer..."
        export TSAN_OPTIONS="mmap_limit_mb=0:detect_thread_leaks=false"
        echo "   TSAN_OPTIONS: $TSAN_OPTIONS"
        echo "   Disabling ASLR for compatibility"
        echo
        setarch x86_64 -R "$TEST_EXECUTABLE" "$@"
        ;;
    "asan")
        echo "🔍 Running with AddressSanitizer..."
        export ASAN_OPTIONS="halt_on_error=1:abort_on_error=1:check_initialization_order=1:strict_init_order=1:detect_odr_violation=2:detect_stack_use_after_return=true:strict_string_checks=1"
        export MALLOC_CHECK_=3
        echo "   ASAN_OPTIONS: $ASAN_OPTIONS"
        echo "   MALLOC_CHECK_: $MALLOC_CHECK_"
        echo
        "$TEST_EXECUTABLE" "$@"
        ;;
    "lsan"|"leak")
        echo "💧 Running with LeakSanitizer..."
        export LSAN_OPTIONS="halt_on_error=1:print_stats=1:print_suppressions=false:report_objects=1"
        export MALLOC_CHECK_=3
        echo "   LSAN_OPTIONS: $LSAN_OPTIONS"
        echo "   MALLOC_CHECK_: $MALLOC_CHECK_"
        echo
        "$TEST_EXECUTABLE" "$@"
        ;;
    *)
        echo "Error: Unknown sanitizer type '$SANITIZER_TYPE'"
        echo ""
        show_usage
        exit 1
        ;;
esac

echo
echo "✅ Sanitizer test completed successfully!"