#include <gtest/gtest.h>
#include "stack.hpp"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <memory>
#include <array>

using namespace conc;

class StackStaticMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize our static memory pool
        for (int i = 0; i < POOL_SIZE; ++i) {
            memory_pool[i] = i;
        }
    }

    void TearDown() override {
        // Cleanup hazard domains after each test to avoid false positives with sanitizers
        stack<int>::hazard_domain{}.delete_all();
        
        // Reset pool for next test
        for (int i = 0; i < POOL_SIZE; ++i) {
            memory_pool[i] = i;
        }
    }

    static constexpr int POOL_SIZE = 100000;
    static inline std::array<int, POOL_SIZE> memory_pool;
};

// Static memory torture test focused on hazard pointer performance
TEST_F(StackStaticMemoryTest, HazardPointerTortureTest) {
    stack<int> s;
    const int duration_seconds = 5;
    const int num_producer_threads = 8;
    const int num_consumer_threads = 8;
    const int num_mixed_threads = 4;
    
    std::atomic<bool> stop{false};
    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};
    std::atomic<int> errors{0};
    std::atomic<int> pool_index{0};
    
    std::vector<std::thread> threads;
    
    // Producer threads - use static memory pool
    for (int i = 0; i < num_producer_threads; ++i) {
        threads.emplace_back([&s, &stop, &total_produced, &pool_index, i]() {
            while (!stop.load()) {
                try {
                    // Get value from static memory pool (circular)
                    int idx = pool_index.fetch_add(1) % POOL_SIZE;
                    int value = memory_pool[idx];
                    
                    s.push(std::move(value));
                    total_produced.fetch_add(1);
                    
                    if (total_produced.load() % 10 == 0) {
                        std::this_thread::yield();
                    }
                } catch (...) {
                    // Continue on any exception
                }
            }
        });
    }

    // Consumer threads - validate data from static pool
    for (int i = 0; i < num_consumer_threads; ++i) {
        threads.emplace_back([&s, &stop, &total_consumed, &errors, i]() {
            while (!stop.load()) {
                try {
                    auto result = s.pop();
                    if (result.has_value()) {
                        int value = result.value();
                        
                        // Validate that the value is from our static pool
                        if (value < 0 || value >= POOL_SIZE) {
                            errors.fetch_add(1);
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
    
    // Mixed threads (both push and pop) - all using static memory
    for (int i = 0; i < num_mixed_threads; ++i) {
        threads.emplace_back([&s, &stop, &total_produced, &total_consumed, &errors, &pool_index, i]() {
            int local_ops = 0;
            while (!stop.load()) {
                try {
                    if (local_ops % 2 == 0) {
                        // Push from static pool
                        int idx = (pool_index.fetch_add(1) + i * 1000) % POOL_SIZE;
                        int value = memory_pool[idx];
                        s.push(std::move(value));
                        total_produced.fetch_add(1);
                    } else {
                        // Pop and validate
                        auto result = s.pop();
                        if (result.has_value()) {
                            int value = result.value();
                            if (value < 0 || value >= POOL_SIZE) {
                                errors.fetch_add(1);
                            }
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
    
    // Drain remaining elements and validate
    int remaining = 0;
    while (true) {
        auto result = s.pop();
        if (!result.has_value()) break;
        
        int value = result.value();
        if (value < 0 || value >= POOL_SIZE) {
            errors.fetch_add(1);
        }
        remaining++;
    }
    total_consumed.fetch_add(remaining);
    
    std::cout << "Hazard Pointer Static Memory Torture Test Results:\n";
    std::cout << "  Produced: " << total_produced.load() << std::endl;
    std::cout << "  Consumed: " << total_consumed.load() << std::endl;
    std::cout << "  Errors: " << errors.load() << std::endl;
    std::cout << "  Operations/sec: " << (total_produced.load() + total_consumed.load()) / duration_seconds << std::endl;
    
    EXPECT_EQ(total_produced.load(), total_consumed.load()) 
        << "Production/consumption mismatch";
    EXPECT_EQ(errors.load(), 0) << "Data integrity errors detected";
}

// High-contention test focusing on hazard pointer acquire/release cycles
TEST_F(StackStaticMemoryTest, HazardPointerContentionTest) {
    stack<int> s;
    const int num_threads = 32; // High contention
    const int operations_per_thread = 50000;
    std::atomic<bool> start_flag{false};
    std::atomic<int> total_ops{0};
    
    std::vector<std::thread> threads;
    
    // Pre-populate stack with static values
    for (int i = 0; i < 1000; ++i) {
        int value = memory_pool[i % POOL_SIZE];
        s.push(std::move(value));
    }
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, &start_flag, &total_ops, operations_per_thread, i]() {
            // Wait for all threads to be ready
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            for (int j = 0; j < operations_per_thread; ++j) {
                // Rapid push/pop cycles to stress hazard pointer machinery
                int value = memory_pool[(i * operations_per_thread + j) % POOL_SIZE];
                s.push(std::move(value));
                
                [[maybe_unused]] auto result1 = s.pop();
                [[maybe_unused]] auto result2 = s.pop();
                
                total_ops.fetch_add(3); // 1 push + 2 pops
            }
        });
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    start_flag.store(true, std::memory_order_release);
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "Hazard Pointer Contention Test Results:\n";
    std::cout << "  Total operations: " << total_ops.load() << std::endl;
    std::cout << "  Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "  Operations/sec: " << (total_ops.load() * 1000) / duration.count() << std::endl;
    std::cout << "  Operations/thread/sec: " << ((total_ops.load() * 1000) / duration.count()) / num_threads << std::endl;
}

// Test focusing on hazard pointer protection/scan cycles
TEST_F(StackStaticMemoryTest, HazardPointerScanningStressTest) {
    stack<int> s;
    const int duration_seconds = 3;
    const int num_scan_intensive_threads = 16;
    
    std::atomic<bool> stop{false};
    std::atomic<long> protection_cycles{0};
    std::atomic<long> scan_cycles{0};
    
    std::vector<std::thread> threads;
    
    // Pre-populate to ensure there are always elements to pop
    for (int i = 0; i < 10000; ++i) {
        int value = memory_pool[i % POOL_SIZE];
        s.push(std::move(value));
    }
    
    for (int i = 0; i < num_scan_intensive_threads; ++i) {
        threads.emplace_back([&s, &stop, &protection_cycles, &scan_cycles]() {
            while (!stop.load()) {
                // Rapid pop operations trigger hazard pointer protect() calls
                auto result = s.pop();
                protection_cycles.fetch_add(1);
                
                if (result.has_value()) {
                    // Push back to maintain stack size and trigger retire() calls
                    s.push(std::move(result.value()));
                    scan_cycles.fetch_add(1);
                }
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    stop.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "Hazard Pointer Scanning Stress Test Results:\n";
    std::cout << "  Protection cycles: " << protection_cycles.load() << std::endl;
    std::cout << "  Scan cycles: " << scan_cycles.load() << std::endl;
    std::cout << "  Protection cycles/sec: " << protection_cycles.load() / duration_seconds << std::endl;
    std::cout << "  Scan cycles/sec: " << scan_cycles.load() / duration_seconds << std::endl;
}