#include <gtest/gtest.h>
#include "stack.hpp"
#include "domain.hpp"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <memory>

using namespace conc;

class StackTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code for each test
    }

    void TearDown() override {
        // Cleanup code for each test
    }
};

// Basic functionality tests
TEST_F(StackTest, EmptyStackPopReturnsEmpty) {
    stack<int> s;
    auto result = s.pop();
    EXPECT_FALSE(result.has_value());
}

// Simpler test to isolate the exact issue
TEST_F(StackTest, SimpleDoubleDeleteReproduction) {
    stack<int> s;
    
    // Very rapid operations to trigger race condition
    std::vector<std::thread> threads;
    const int num_threads = 4;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, i]() {
            for (int j = 0; j < 1000; ++j) {
                s.push(i * 1000 + j);
                [[maybe_unused]] auto result = s.pop();
                // Don't check result to keep it fast
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
}

// Minimal test to isolate the race condition
TEST_F(StackTest, MinimalRaceCondition) {
    stack<int> s;
    
    // Push one element
    s.push(42);
    
    // Two threads try to pop the same element
    std::thread t1([&s]() {
        [[maybe_unused]] auto result = s.pop();
    });
    
    std::thread t2([&s]() {
        [[maybe_unused]] auto result = s.pop();
    });
    
    t1.join();
    t2.join();
}

TEST_F(StackTest, PushAndPopSingleElement) {
    stack<int> s;
    int value = 42;
    s.push(std::move(value));
    
    auto result = s.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(StackTest, PushAndPopMultipleElements) {
    stack<int> s;
    
    // Push elements 1, 2, 3
    for (int i = 1; i <= 3; ++i) {
        s.push(std::move(i));
    }
    
    // Pop should return 3, 2, 1 (LIFO order)
    auto result1 = s.pop();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), 3);
    
    auto result2 = s.pop();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), 2);
    
    auto result3 = s.pop();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value(), 1);
    
    // Stack should now be empty
    auto result4 = s.pop();
    EXPECT_FALSE(result4.has_value());
}

// AGGRESSIVE STRESS TESTS - Designed to break the implementation

// Test for ABA problem
TEST_F(StackTest, ABAStressTest) {
    stack<int> s;
    const int num_threads = 16;
    const int operations_per_thread = 10000;
    std::atomic<bool> start_flag{false};
    std::atomic<int> errors{0};
    
    std::vector<std::thread> threads;
    
    // Pre-populate with some elements to increase chance of ABA
    for (int i = 0; i < 1000; ++i) {
        s.push(std::move(i));
    }
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, &start_flag, &errors, operations_per_thread, i]() {
            // Wait for all threads to be ready
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            for (int j = 0; j < operations_per_thread; ++j) {
                // Rapidly push and pop to trigger ABA scenarios
                int value = i * operations_per_thread + j;
                s.push(std::move(value));
                
                auto result1 = s.pop();
                auto result2 = s.pop();
                
                // Push back one of the popped values
                if (result1.has_value()) {
                    s.push(std::move(result1.value()));
                }
                
                // Verify we didn't get corrupted data
                if (result2.has_value() && result2.value() < 0) {
                    errors.fetch_add(1);
                }
            }
        });
    }
    
    start_flag.store(true, std::memory_order_release);
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(errors.load(), 0) << "Detected corrupted data, possible ABA problem";
}

// Memory ordering stress test
TEST_F(StackTest, MemoryOrderingStressTest) {
    stack<std::unique_ptr<int>> s;
    const int num_producer_threads = 8;
    const int num_consumer_threads = 8;
    const int items_per_producer = 5000;
    
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    std::atomic<bool> producers_done{false};
    
    std::vector<std::thread> threads;
    
    // Producer threads
    for (int i = 0; i < num_producer_threads; ++i) {
        threads.emplace_back([&s, &produced_count, items_per_producer, i]() {
            for (int j = 0; j < items_per_producer; ++j) {
                auto ptr = std::make_unique<int>(i * items_per_producer + j);
                s.push(std::move(ptr));
                produced_count.fetch_add(1, std::memory_order_relaxed);
                
                // Add some randomness to scheduling
                if (j % 100 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Consumer threads
    for (int i = 0; i < num_consumer_threads; ++i) {
        threads.emplace_back([&s, &consumed_count, &producers_done]() {
            while (!producers_done.load(std::memory_order_acquire)) {
                auto result = s.pop();
                if (result.has_value()) {
                    // Verify the pointer is valid
                    ASSERT_NE(result.value(), nullptr);
                    int value = *result.value();
                    ASSERT_GE(value, 0);  // Should be a valid value
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::yield();
            }
            
            // Drain remaining items
            while (true) {
                auto result = s.pop();
                if (!result.has_value()) {
                    break;
                }
                ASSERT_NE(result.value(), nullptr);
                consumed_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    // Wait for producers to finish
    for (int i = 0; i < num_producer_threads; ++i) {
        threads[i].join();
    }
    producers_done.store(true, std::memory_order_release);
    
    // Wait for consumers to finish
    for (size_t i = num_producer_threads; i < threads.size(); ++i) {
        threads[i].join();
    }
    
    EXPECT_EQ(produced_count.load(), consumed_count.load());
}

// Race condition on push/pop with same elements
TEST_F(StackTest, RaceConditionStressTest) {
    stack<int> s;
    const int num_threads = 32;
    const int cycles = 1000;
    std::atomic<int> total_pushes{0};
    std::atomic<int> total_pops{0};
    std::atomic<bool> start{false};
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, &total_pushes, &total_pops, &start, cycles, i]() {
            while (!start.load()) {
                std::this_thread::yield();
            }
            
            for (int j = 0; j < cycles; ++j) {
                // Rapid fire push/pop operations
                s.push(i * cycles + j);
                total_pushes.fetch_add(1, std::memory_order_relaxed);
                
                // Immediately try to pop
                auto result = s.pop();
                if (result.has_value()) {
                    total_pops.fetch_add(1, std::memory_order_relaxed);
                    // Push it back to create more contention
                    s.push(std::move(result.value()));
                    total_pushes.fetch_add(1, std::memory_order_relaxed);
                }
                
                // Create scheduling pressure
                if (j % 10 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    start.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Drain the stack
    int final_pops = 0;
    while (true) {
        auto result = s.pop();
        if (!result.has_value()) break;
        final_pops++;
    }
    
    total_pops.fetch_add(final_pops, std::memory_order_relaxed);
    
    EXPECT_EQ(total_pushes.load(), total_pops.load()) 
        << "Push/pop count mismatch indicates data loss or corruption";
}

// Test for use-after-free scenarios
TEST_F(StackTest, UseAfterFreeStressTest) {
    stack<std::shared_ptr<std::vector<int>>> s;
    const int num_threads = 16;
    const int operations = 2000;
    std::atomic<int> allocation_errors{0};
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, &allocation_errors, operations, i]() {
            for (int j = 0; j < operations; ++j) {
                try {
                    // Create large objects to stress memory
                    auto vec = std::make_shared<std::vector<int>>(1000, i * operations + j);
                    s.push(std::move(vec));
                    
                    // Immediately try to pop and use the object
                    auto result = s.pop();
                    if (result.has_value()) {
                        auto& retrieved_vec = result.value();
                        if (retrieved_vec && !retrieved_vec->empty()) {
                            // Access the data to ensure it's valid
                            volatile int sum = 0;
                            for (int val : *retrieved_vec) {
                                sum += val;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    allocation_errors.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(allocation_errors.load(), 0) << "Memory allocation/access errors detected";
}

// High contention test with deliberate context switches
TEST_F(StackTest, HighContentionWithContextSwitches) {
    stack<int> s;
    const int num_threads = std::thread::hardware_concurrency() * 2;
    const int operations = 5000;
    std::atomic<int> successful_ops{0};
    std::atomic<bool> start{false};
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, &successful_ops, &start, operations, i]() {
            while (!start.load()) {
                std::this_thread::yield();
            }
            
            for (int j = 0; j < operations; ++j) {
                s.push(i * operations + j);
                
                // Force context switch to maximize contention
                std::this_thread::sleep_for(std::chrono::nanoseconds(1));
                
                auto result = s.pop();
                if (result.has_value()) {
                    successful_ops.fetch_add(1);
                }
                
                // Another context switch
                if (j % 50 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
        });
    }
    
    start.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    // The test passes if no crashes occur and we get reasonable results
    EXPECT_GT(successful_ops.load(), 0);
}

// Test with exception-throwing elements
class ThrowingInt {
public:
    int value;
    static std::atomic<int> copy_count;
    
    ThrowingInt(int v) : value(v) {}
    
    ThrowingInt(const ThrowingInt& other) : value(other.value) {
        int count = copy_count.fetch_add(1);
        if (count % 100 == 99) {  // Throw on every 100th copy
            throw std::runtime_error("Copy constructor exception");
        }
    }
    
    ThrowingInt(ThrowingInt&& other) noexcept : value(other.value) {}
    
    ThrowingInt& operator=(const ThrowingInt& other) {
        value = other.value;
        return *this;
    }
    
    ThrowingInt& operator=(ThrowingInt&& other) noexcept {
        value = other.value;
        return *this;
    }
    
    bool operator==(const ThrowingInt& other) const {
        return value == other.value;
    }
};

std::atomic<int> ThrowingInt::copy_count{0};

TEST_F(StackTest, ExceptionSafetyTest) {
    stack<ThrowingInt> s;
    const int num_threads = 8;
    const int operations = 1000;
    std::atomic<int> exceptions_caught{0};
    std::atomic<int> successful_operations{0};
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, &exceptions_caught, &successful_operations, operations, i]() {
            for (int j = 0; j < operations; ++j) {
                try {
                    ThrowingInt val(i * operations + j);
                    s.push(std::move(val));
                    
                    auto result = s.pop();
                    if (result.has_value()) {
                        successful_operations.fetch_add(1);
                    }
                } catch (const std::exception& e) {
                    exceptions_caught.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Test passes if we handle exceptions gracefully
    EXPECT_GT(successful_operations.load(), 0);
    std::cout << "Exceptions caught: " << exceptions_caught.load() << std::endl;
    std::cout << "Successful operations: " << successful_operations.load() << std::endl;
}

// Torture test - everything at once
TEST_F(StackTest, TortureTest) {
    stack<std::unique_ptr<std::vector<int>>> s;
    const int duration_seconds = 5;
    const int num_producer_threads = 8;
    const int num_consumer_threads = 8;
    const int num_mixed_threads = 4;
    
    std::atomic<bool> stop{false};
    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};
    std::atomic<int> errors{0};
    
    std::vector<std::thread> threads;
    
    // Producer threads
    for (int i = 0; i < num_producer_threads; ++i) {
        threads.emplace_back([&s, &stop, &total_produced, i]() {
            int local_count = 0;
            while (!stop.load()) {
                try {
                    auto vec = std::make_unique<std::vector<int>>(100, i * 10000 + local_count);
                    s.push(std::move(vec));
                    total_produced.fetch_add(1);
                    local_count++;
                    
                    if (local_count % 10 == 0) {
                        std::this_thread::yield();
                    }
                } catch (...) {
                    // Continue on any exception
                }
            }
        });
    }
    
    // Consumer threads
    for (int i = 0; i < num_consumer_threads; ++i) {
        threads.emplace_back([&s, &stop, &total_consumed, &errors, i]() {
            while (!stop.load()) {
                try {
                    auto result = s.pop();
                    if (result.has_value()) {
                        auto& vec = result.value();
                        if (!vec || vec->empty()) {
                            errors.fetch_add(1);
                        } else {
                            // Verify data integrity
                            int expected = (*vec)[0];
                            for (int val : *vec) {
                                if (val != expected) {
                                    errors.fetch_add(1);
                                    break;
                                }
                            }
                        }
                        total_consumed.fetch_add(1);
                    }
                    std::this_thread::yield();
                } catch (...) {
                    errors.fetch_add(1);
                }
            }
        });
    }
    
    // Mixed threads (both push and pop)
    for (int i = 0; i < num_mixed_threads; ++i) {
        threads.emplace_back([&s, &stop, &total_produced, &total_consumed, &errors, i]() {
            int local_ops = 0;
            while (!stop.load()) {
                try {
                    if (local_ops % 2 == 0) {
                        auto vec = std::make_unique<std::vector<int>>(50, i * 20000 + local_ops);
                        s.push(std::move(vec));
                        total_produced.fetch_add(1);
                    } else {
                        auto result = s.pop();
                        if (result.has_value()) {
                            total_consumed.fetch_add(1);
                        }
                    }
                    local_ops++;
                    
                    if (local_ops % 5 == 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(1));
                    }
                } catch (...) {
                    errors.fetch_add(1);
                }
            }
        });
    }
    
    // Let it run for the specified duration
    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    stop.store(true);
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Drain remaining elements
    int remaining = 0;
    while (true) {
        auto result = s.pop();
        if (!result.has_value()) break;
        remaining++;
    }
    total_consumed.fetch_add(remaining);
    
    std::cout << "Torture test results:\n";
    std::cout << "  Produced: " << total_produced.load() << std::endl;
    std::cout << "  Consumed: " << total_consumed.load() << std::endl;
    std::cout << "  Errors: " << errors.load() << std::endl;
    
    EXPECT_EQ(total_produced.load(), total_consumed.load()) 
        << "Production/consumption mismatch";
    EXPECT_EQ(errors.load(), 0) << "Data integrity errors detected";
}

// CRITICAL BUG DETECTION TESTS

// Test for potential memory leak from unpaired allocations
TEST_F(StackTest, MemoryLeakDetectionTest) {
    // This test specifically targets the potential memory leak in pop()
    // where we return acquire->element but never delete the node
    
    stack<std::unique_ptr<std::vector<int>>> s;
    const int iterations = 10000;
    
    for (int i = 0; i < iterations; ++i) {
        auto large_data = std::make_unique<std::vector<int>>(1000, i);
        s.push(std::move(large_data));
        
        auto result = s.pop();
        ASSERT_TRUE(result.has_value());
        
        // Verify data integrity
        EXPECT_EQ(result.value()->size(), 1000);
        EXPECT_EQ((*result.value())[0], i);
    }
    
    // If there are memory leaks, this test will show them in tools like valgrind
    // The test passes if no crashes occur, but the real test is memory analysis
}

// Test the specific bug: nodes are never freed in pop()
TEST_F(StackTest, NodeMemoryLeakStressTest) {
    stack<int> s;
    const int num_operations = 50000;
    
    // This should create a massive memory leak if nodes aren't freed
    for (int cycle = 0; cycle < 5; ++cycle) {
        // Push many elements
        for (int i = 0; i < num_operations; ++i) {
            s.push(std::move(i));
        }
        
        // Pop all elements - nodes should be freed here but aren't!
        for (int i = 0; i < num_operations; ++i) {
            auto result = s.pop();
            ASSERT_TRUE(result.has_value());
        }
        
        // Stack should be empty
        auto empty_result = s.pop();
        EXPECT_FALSE(empty_result.has_value());
    }
    
    // If your implementation doesn't free nodes, this test will consume
    // 250,000 * sizeof(node) bytes of memory that's never freed
}

// Test for double-deletion or use-after-free in concurrent scenarios
TEST_F(StackTest, ConcurrentUseAfterFreeTest) {
    stack<int> s;
    const int num_threads = 16;
    const int operations = 5000;
    std::atomic<int> errors{0};
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, &errors, operations, i]() {
            for (int j = 0; j < operations; ++j) {
                try {
                    // Push and immediately pop to create rapid allocation/deallocation
                    s.push(i * operations + j);
                    
                    auto result = s.pop();
                    if (result.has_value()) {
                        // Try to access the value - this might crash if there's UAF
                        volatile int val = result.value();
                        if (val < 0) {  // Shouldn't happen with valid data
                            errors.fetch_add(1);
                        }
                    }
                } catch (...) {
                    errors.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(errors.load(), 0) << "Detected errors that might indicate memory corruption";
}

// Test basic functionality to ensure our aggressive tests don't break simple cases
TEST_F(StackTest, BasicFunctionalityAfterStressTests) {
    stack<int> s;
    
    // Basic push/pop
    s.push(42);
    auto result = s.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
    
    // Empty stack
    auto empty_result = s.pop();
    EXPECT_FALSE(empty_result.has_value());
    
    // Multiple elements
    for (int i = 1; i <= 5; ++i) {
        s.push(std::move(i));
    }
    
    for (int i = 5; i >= 1; --i) {
        auto result = s.pop();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), i);
    }
}
