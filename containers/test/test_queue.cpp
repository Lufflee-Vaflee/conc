#include <gtest/gtest.h>
#include "queue.hpp"
#include <thread>
#include <vector>
#include <chrono>
#include <unordered_set>
#include <atomic>
#include <algorithm>

using namespace conc;

class QueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code for each test
    }

    void TearDown() override {
        // Cleanup hazard domains after each test to avoid false positives with sanitizers
        queue<int>::hazard_domain{}.delete_all();
    }
};

// Basic functionality tests
TEST_F(QueueTest, EmptyQueuePop) {
    queue<int> q;
    auto result = q.dequeue();
    EXPECT_FALSE(result.has_value());
}

TEST_F(QueueTest, SinglePushPop) {
    queue<int> q;
    q.enqueue(42);
    auto result = q.dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
    
    // Queue should be empty now
    auto empty_result = q.dequeue();
    EXPECT_FALSE(empty_result.has_value());
}

TEST_F(QueueTest, MultiplePushPop) {
    queue<int> q;
    std::vector<int> values = {1, 2, 3, 4, 5};
    
    // Push all values
    for (int val : values) {
        q.enqueue(std::move(val));
    }
    
    // Pop all values and verify FIFO order
    for (int expected : values) {
        auto result = q.dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, expected);
    }
    
    // Queue should be empty
    auto empty_result = q.dequeue();
    EXPECT_FALSE(empty_result.has_value());
}

TEST_F(QueueTest, InterleavedPushPop) {
    queue<int> q;
    
    q.enqueue(1);
    auto result1 = q.dequeue();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(*result1, 1);
    
    q.enqueue(2);
    q.enqueue(3);
    
    auto result2 = q.dequeue();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(*result2, 2);
    
    q.enqueue(4);
    
    auto result3 = q.dequeue();
    auto result4 = q.dequeue();
    ASSERT_TRUE(result3.has_value());
    ASSERT_TRUE(result4.has_value());
    EXPECT_EQ(*result3, 3);
    EXPECT_EQ(*result4, 4);
    
    auto empty_result = q.dequeue();
    EXPECT_FALSE(empty_result.has_value());
}

TEST_F(QueueTest, LargeSequentialOperations) {
    queue<int> q;
    const int count = 10000;
    
    // Push many values
    for (int i = 0; i < count; ++i) {
        q.enqueue(std::move(i));
    }
    
    // Pop all values and verify order
    for (int i = 0; i < count; ++i) {
        auto result = q.dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, i);
    }
    
    // Queue should be empty
    auto empty_result = q.dequeue();
    EXPECT_FALSE(empty_result.has_value());
}

// Concurrent tests
TEST_F(QueueTest, ConcurrentPush) {
    queue<int> q;
    const int num_threads = 8;
    const int items_per_thread = 1000;
    std::vector<std::thread> threads;
    
    // Each thread pushes unique values
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&q, t, items_per_thread]() {
            int base = t * items_per_thread;
            for (int i = 0; i < items_per_thread; ++i) {
                q.enqueue(base + i);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Collect all values
    std::unordered_set<int> collected;
    auto result = q.dequeue();
    while (result.has_value()) {
        collected.insert(*result);
        result = q.dequeue();
    }
    
    // Verify all expected values are present
    EXPECT_EQ(collected.size(), num_threads * items_per_thread);
    for (int t = 0; t < num_threads; ++t) {
        int base = t * items_per_thread;
        for (int i = 0; i < items_per_thread; ++i) {
            EXPECT_TRUE(collected.count(base + i) == 1);
        }
    }
}

TEST_F(QueueTest, ConcurrentPop) {
    queue<int> q;
    const int count = 10000;
    
    // Pre-populate queue
    for (int i = 0; i < count; ++i) {
        q.enqueue(std::move(i));
    }
    
    const int num_threads = 4;
    std::vector<std::thread> threads;
    std::vector<std::vector<int>> thread_results(num_threads);
    
    // Multiple threads pop concurrently
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&q, &thread_results, t]() {
            auto result = q.dequeue();
            while (result.has_value()) {
                thread_results[t].push_back(*result);
                result = q.dequeue();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Collect all popped values
    std::unordered_set<int> all_popped;
    for (const auto& thread_result : thread_results) {
        for (int val : thread_result) {
            EXPECT_TRUE(all_popped.insert(val).second); // Should be unique
        }
    }
    
    // Verify all values were popped exactly once
    EXPECT_EQ(all_popped.size(), count);
    for (int i = 0; i < count; ++i) {
        EXPECT_TRUE(all_popped.count(i) == 1);
    }
    
    // Queue should be empty
    auto empty_result = q.dequeue();
    EXPECT_FALSE(empty_result.has_value());
}

TEST_F(QueueTest, ConcurrentPushPop) {
    queue<int> q;
    const int num_producer_threads = 4;
    const int num_consumer_threads = 4;
    const int items_per_producer = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<int> total_consumed{0};
    std::vector<std::vector<int>> consumer_results(num_consumer_threads);
    
    // Start consumer threads
    for (int c = 0; c < num_consumer_threads; ++c) {
        threads.emplace_back([&q, &consumer_results, &total_consumed, c]() {
            auto start = std::chrono::steady_clock::now();
            auto timeout = std::chrono::milliseconds(5000); // 5 second timeout
            
            while (true) {
                auto result = q.dequeue();
                if (result.has_value()) {
                    consumer_results[c].push_back(*result);
                    total_consumed.fetch_add(1);
                } else {
                    // Check if we've been running too long without finding anything
                    auto now = std::chrono::steady_clock::now();
                    if (now - start > timeout) {
                        break;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Start producer threads
    for (int p = 0; p < num_producer_threads; ++p) {
        threads.emplace_back([&q, p, items_per_producer]() {
            int base = p * items_per_producer;
            for (int i = 0; i < items_per_producer; ++i) {
                q.enqueue(base + i);
                // Add small delay to interleave with consumers
                if (i % 100 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Collect remaining items from queue
    auto result = q.dequeue();
    while (result.has_value()) {
        consumer_results[0].push_back(*result);
        total_consumed.fetch_add(1);
        result = q.dequeue();
    }
    
    // Verify all items were consumed exactly once
    std::unordered_set<int> all_consumed;
    for (const auto& consumer_result : consumer_results) {
        for (int val : consumer_result) {
            EXPECT_TRUE(all_consumed.insert(val).second); // Should be unique
        }
    }
    
    const int expected_total = num_producer_threads * items_per_producer;
    EXPECT_EQ(all_consumed.size(), expected_total);
    EXPECT_EQ(total_consumed.load(), expected_total);
    
    // Verify all expected values are present
    for (int p = 0; p < num_producer_threads; ++p) {
        int base = p * items_per_producer;
        for (int i = 0; i < items_per_producer; ++i) {
            EXPECT_TRUE(all_consumed.count(base + i) == 1);
        }
    }
}

TEST_F(QueueTest, StressTest) {
    queue<int> q;
    const int num_threads = 8;
    const int operations_per_thread = 10000;
    
    std::vector<std::thread> threads;
    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};
    
    // Each thread does both push and pop operations
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&q, &push_count, &pop_count, t, operations_per_thread]() {
            int base = t * operations_per_thread;
            
            for (int i = 0; i < operations_per_thread; ++i) {
                // Push
                q.enqueue(base + i);
                push_count.fetch_add(1);
                
                // Try to pop (might not always succeed)
                auto result = q.dequeue();
                if (result.has_value()) {
                    pop_count.fetch_add(1);
                }
                
                // Yield occasionally to increase contention
                if (i % 1000 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Pop remaining items
    auto result = q.dequeue();
    while (result.has_value()) {
        pop_count.fetch_add(1);
        result = q.dequeue();
    }
    
    // Verify conservation: everything pushed was eventually popped
    EXPECT_EQ(push_count.load(), num_threads * operations_per_thread);
    EXPECT_EQ(pop_count.load(), push_count.load());
    
    // Queue should be empty
    auto empty_result = q.dequeue();
    EXPECT_FALSE(empty_result.has_value());
}

TEST_F(QueueTest, MemoryConsistency) {
    queue<int> q;
    const int count = 1000;
    std::atomic<bool> start{false};
    
    // Producer thread
    std::thread producer([&q, &start, count]() {
        while (!start.load()) {
            std::this_thread::yield();
        }
        
        for (int i = 0; i < count; ++i) {
            q.enqueue(std::move(i));
        }
    });
    
    // Consumer thread
    std::vector<int> consumed;
    std::thread consumer([&q, &start, &consumed, count]() {
        while (!start.load()) {
            std::this_thread::yield();
        }
        
        int received = 0;
        while (received < count) {
            auto result = q.dequeue();
            if (result.has_value()) {
                consumed.push_back(*result);
                received++;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    // Start both threads simultaneously
    start.store(true);
    
    producer.join();
    consumer.join();
    
    // Verify FIFO order
    EXPECT_EQ(consumed.size(), count);
    for (int i = 0; i < count; ++i) {
        EXPECT_EQ(consumed[i], i);
    }
}

// Edge case tests
TEST_F(QueueTest, RapidPushPopCycles) {
    queue<int> q;
    const int cycles = 1000;
    
    for (int cycle = 0; cycle < cycles; ++cycle) {
        // Push some items
        for (int i = 0; i < 10; ++i) {
            q.enqueue(cycle * 10 + i);
        }
        
        // Pop all items
        for (int i = 0; i < 10; ++i) {
            auto result = q.dequeue();
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(*result, cycle * 10 + i);
        }
        
        // Queue should be empty
        auto empty_result = q.dequeue();
        EXPECT_FALSE(empty_result.has_value());
    }
}

TEST_F(QueueTest, ProducerConsumerReclamationStress) {
    queue<int> q;
    const int num_producers = 4;
    const int num_consumers = 8;
    const int items_per_producer = 5000;
    
    std::vector<std::thread> threads;
    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};
    std::atomic<bool> producers_done{false};
    
    // Start consumer threads first
    for (int c = 0; c < num_consumers; ++c) {
        threads.emplace_back([&q, &total_consumed, &producers_done, c]() {
            std::vector<int> consumed_items;
            
            while (!producers_done.load() || true) {
                auto result = q.dequeue();
                if (result.has_value()) {
                    consumed_items.push_back(*result);
                    total_consumed.fetch_add(1);
                } else if (producers_done.load()) {
                    // Check one more time if there are remaining items
                    result = q.dequeue();
                    if (!result.has_value()) {
                        break;
                    }
                    consumed_items.push_back(*result);
                    total_consumed.fetch_add(1);
                } else {
                    // Brief yield to reduce busy waiting when queue is empty
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Start producer threads
    for (int p = 0; p < num_producers; ++p) {
        threads.emplace_back([&q, &total_produced, p, items_per_producer]() {
            int base = p * items_per_producer;
            for (int i = 0; i < items_per_producer; ++i) {
                q.enqueue(base + i);
                total_produced.fetch_add(1);
                
                // Add small delays to allow consumers to process
                if (i % 500 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
        });
    }
    
    // Wait for all producer threads to complete
    for (int p = num_consumers; p < threads.size(); ++p) {
        threads[p].join();
    }
    
    // Signal that production is complete
    producers_done.store(true);
    
    // Wait for all consumer threads to complete
    for (int c = 0; c < num_consumers; ++c) {
        threads[c].join();
    }
    
    // Clean up any remaining items
    auto remaining_result = q.dequeue();
    while (remaining_result.has_value()) {
        total_consumed.fetch_add(1);
        remaining_result = q.dequeue();
    }
    
    // Verify that all produced items were consumed
    const int expected_total = num_producers * items_per_producer;
    EXPECT_EQ(total_produced.load(), expected_total);
    EXPECT_EQ(total_consumed.load(), expected_total);
    
    // This test is primarily designed to stress the hazard pointer reclamation
    // by having more consumers than producers, causing frequent node retirement
}

TEST_F(QueueTest, DeepDiagnosticConcurrentPop) {
    queue<int> q;
    const int count = 10000;
    
    // Pre-populate queue with known values
    std::vector<int> expected_values;
    for (int i = 0; i < count; ++i) {
        q.enqueue(std::move(i));
        expected_values.push_back(i);
    }
    
    std::cout << "\n=== INITIAL QUEUE STATE ===" << std::endl;
    std::cout << "Expected items: " << count << std::endl;
    
    const int num_threads = 4;
    std::vector<std::thread> threads;
    std::vector<std::vector<int>> thread_results(num_threads);
    std::atomic<int> total_popped{0};
    std::atomic<int> null_pops{0};
    
    // Multiple threads pop concurrently with detailed logging
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&q, &thread_results, &total_popped, &null_pops, t]() {
            int local_count = 0;
            while (true) {
                auto result = q.dequeue();
                if (!result.has_value()) {
                    null_pops.fetch_add(1);
                    break;  // Queue appears empty
                }
                thread_results[t].push_back(*result);
                local_count++;
                total_popped.fetch_add(1);
            }
            // Debug output from each thread
            // std::cout << "Thread " << t << " popped " << local_count << " items" << std::endl;
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "\n=== FINAL QUEUE STATE ===" << std::endl;
    std::cout << "Null pops: " << null_pops.load() << std::endl;

    // Collect and analyze popped values
    std::unordered_map<int, int> value_counts;
    std::unordered_set<int> all_popped;
    std::vector<int> duplicates;
    
    for (const auto& thread_result : thread_results) {
        for (int val : thread_result) {
            value_counts[val]++;
            if (value_counts[val] > 1 && std::find(duplicates.begin(), duplicates.end(), val) == duplicates.end()) {
                duplicates.push_back(val);
            }
            all_popped.insert(val);  // Set will handle uniqueness
        }
    }
    
    // Find missing values
    std::vector<int> missing_values;
    for (int val : expected_values) {
        if (all_popped.count(val) == 0) {
            missing_values.push_back(val);
        }
    }
    
    std::cout << "\n=== ANALYSIS ===" << std::endl;
    std::cout << "Expected: " << count << std::endl;
    std::cout << "Collected: " << all_popped.size() << std::endl;
    std::cout << "Total pops: " << total_popped.load() << std::endl;
    std::cout << "Missing: " << missing_values.size() << std::endl;
    std::cout << "Duplicates: " << duplicates.size() << std::endl;
    
    if (!missing_values.empty() && missing_values.size() <= 20) {
        std::cout << "Missing values: ";
        for (int val : missing_values) {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
    
    if (!duplicates.empty()) {
        std::cout << "Duplicate values (with counts): ";
        for (int val : duplicates) {
            std::cout << val << "(" << value_counts[val] << ") ";
        }
        std::cout << std::endl;
    }
    
    // Show total operation count
    int total_operations = 0;
    for (const auto& thread_result : thread_results) {
        total_operations += thread_result.size();
    }
    std::cout << "Total operations performed: " << total_operations << std::endl;
    
    // Try to manually pop remaining items
    std::cout << "\n=== MANUAL POP ATTEMPT ===" << std::endl;
    int manual_pops = 0;
    auto remaining = q.dequeue();
    while (remaining.has_value() && manual_pops < 10) {
        std::cout << "Manually popped: " << *remaining << std::endl;
        manual_pops++;
        remaining = q.dequeue();
    }
    
    // The test should fail to expose the issue
    if (all_popped.size() != count) {
        std::cout << "\n!!! TEST FAILURE DETECTED - ANALYZING !!!" << std::endl;
    }
    
    EXPECT_EQ(all_popped.size(), count);
    EXPECT_EQ(total_popped.load(), count);
}

TEST_F(QueueTest, HighContentionScenario) {
    queue<int> q;
    const int num_threads = 16;
    const int iterations = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<int> successful_ops{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&q, &successful_ops, t, iterations]() {
            for (int i = 0; i < iterations; ++i) {
                // Alternate between push and pop to maximize contention
                if ((t + i) % 2 == 0) {
                    q.enqueue(t * iterations + i);
                    successful_ops.fetch_add(1);
                } else {
                    auto result = q.dequeue();
                    if (result.has_value()) {
                        successful_ops.fetch_add(1);
                    }
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // The test mainly checks that the queue doesn't crash under high contention
    // The exact number of successful operations is less predictable due to timing
    EXPECT_GT(successful_ops.load(), 0);
}

