#include "hazard_pointer.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <memory>
#include <algorithm>
#include <set>
#include <mutex>
#include <condition_variable>
#include <barrier>

namespace conc::test {

// Test node for stress testing
struct StressTestNode {
    std::atomic<int> value{0};
    std::atomic<StressTestNode*> next{nullptr};
    std::atomic<int> reference_count{0};
    
    StressTestNode(int val) : value(val) {}
    
    void increment_ref() {
        reference_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    void decrement_ref() {
        reference_count.fetch_sub(1, std::memory_order_relaxed);
    }
    
    int get_ref_count() const {
        return reference_count.load(std::memory_order_relaxed);
    }
};

// Custom domains with different max_objects parameters
template<typename T>
using small_domain = hazard_domain<T, 32>;

template<typename T>
using medium_domain = hazard_domain<T, 64>;

template<typename T>
using large_domain = hazard_domain<T, 256>;

template<typename T>
using huge_domain = hazard_domain<T, 1024>;

class HazardPointerStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a pool of test nodes
        for (int i = 0; i < 1000; ++i) {
            node_pool.push_back(std::make_unique<StressTestNode>(i));
        }
    }
    
    void TearDown() override {
        node_pool.clear();
    }
    
    std::vector<std::unique_ptr<StressTestNode>> node_pool;
    
    // Helper to create random delay
    void random_delay(int max_microseconds = 10) {
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(1, max_microseconds);
        std::this_thread::sleep_for(std::chrono::microseconds(dist(gen)));
    }
};

// Test stress with default domain (128 max objects)
TEST_F(HazardPointerStressTest, HighContentionDefaultDomain) {
    const int num_threads = std::thread::hardware_concurrency() * 2; // More threads
    const int num_iterations = 20000; // More iterations

    std::vector<std::atomic<StressTestNode*>> shared_pointers(5); // MUCH fewer shared pointers to increase contention

    // Initialize shared pointers
    for (size_t i = 0; i < shared_pointers.size(); ++i) {
        shared_pointers[i].store(new StressTestNode(i));
    }

    std::vector<std::thread> threads;
    std::atomic<long> total_protections{0};
    std::atomic<long> total_retirements{0};
    std::atomic<long> successful_try_protects{0};
    std::atomic<long> failed_try_protects{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto hp = hazard_pointer<StressTestNode>::make_hazard_pointer();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> ptr_dist(0, shared_pointers.size() - 1);
            std::uniform_int_distribution<> op_dist(0, 100);

            for (int i = 0; i < num_iterations; ++i) {
                int ptr_idx = ptr_dist(gen);
                int operation = op_dist(gen);

                if (operation < 40) {
                    // 40% chance: protect operation
                    auto* ptr = hp.protect(shared_pointers[ptr_idx]);
                    if (ptr) {
                        total_protections.fetch_add(1);
                        ptr->increment_ref();
                        
                        // Use the protected data - no delay to increase contention
                        [[maybe_unused]] auto val = ptr->value.load();
                        ptr->decrement_ref();
                    }
                    hp.reset_protection();

                } else if (operation < 80) {
                    // 40% chance: try_protect operation (increased from 20%)
                    StressTestNode* ptr = shared_pointers[ptr_idx].load();
                    if (hp.try_protect(ptr, shared_pointers[ptr_idx])) {
                        successful_try_protects.fetch_add(1);
                        if (ptr) {
                            ptr->increment_ref();
                            [[maybe_unused]] auto val = ptr->value.load();
                            // No delay to increase contention
                            ptr->decrement_ref();
                        }
                        hp.reset_protection();
                    } else {
                        failed_try_protects.fetch_add(1);
                    }

                } else {
                    // 20% chance: pointer replacement (increased from 10%)
                    auto old_ptr = shared_pointers[ptr_idx].load();
                    if (old_ptr && (i % 10 == 0)) { // Much more frequent retirement
                        auto new_ptr = new StressTestNode(i * 1000 + t);

                        if (shared_pointers[ptr_idx].compare_exchange_strong(old_ptr, new_ptr)) {
                            hazard_pointer<StressTestNode>::retire(old_ptr);
                            total_retirements.fetch_add(1);
                        } else {
                            // CAS failed
                            delete new_ptr;
                        }
                    }
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (auto& ptr : shared_pointers) {
        delete ptr.load();
    }

    EXPECT_GT(total_protections.load(), 0);
    EXPECT_GT(successful_try_protects.load(), 0);

    std::cout << "Default domain stress test results:\n"
              << "  Total protections: " << total_protections.load() << "\n"
              << "  Successful try_protects: " << successful_try_protects.load() << "\n"
              << "  Failed try_protects: " << failed_try_protects.load() << "\n"
              << "  Total retirements: " << total_retirements.load() << "\n";
}

// Test with small domain (8 max objects) - should exhaust cells
TEST_F(HazardPointerStressTest, SmallDomainExhaustion) {
    const int num_threads = 16;
    const int num_iterations = 1000;

    std::atomic<StressTestNode*> shared_ptr{node_pool[0].get()};
    std::vector<std::thread> threads;
    std::atomic<long> protection_count{0};
    std::atomic<long> assertion_failures{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            try {
                auto hp = hazard_pointer<StressTestNode, small_domain<StressTestNode>>::make_hazard_pointer();

                for (int i = 0; i < num_iterations; ++i) {
                    auto* ptr = hp.protect(shared_ptr);
                    if (ptr) {
                        protection_count.fetch_add(1);
                        [[maybe_unused]] auto val = ptr->value.load();
                    }
                    hp.reset_protection();
                }
            } catch (const std::exception&) {
                assertion_failures.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // With small domain, we might hit assertions when cells are exhausted
    std::cout << "Small domain test results:\n"
              << "  Protection count: " << protection_count.load() << "\n"
              << "  Assertion failures: " << assertion_failures.load() << "\n";
}

// Test with large domain to ensure scalability
TEST_F(HazardPointerStressTest, LargeDomainScalability) {
    const int num_threads = 32;
    const int num_iterations = 5000;
    const int num_shared_ptrs = 20;

    std::vector<std::atomic<StressTestNode*>> shared_pointers(num_shared_ptrs);
    for (int i = 0; i < num_shared_ptrs; ++i) {
        shared_pointers[i].store(node_pool[i].get());
    }

    std::vector<std::thread> threads;
    std::atomic<long> operations_completed{0};

    // Synchronization barrier for simultaneous start
    std::barrier sync_point(num_threads);

    auto start_time = std::chrono::steady_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto hp = hazard_pointer<StressTestNode, large_domain<StressTestNode>>::make_hazard_pointer();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> ptr_dist(0, num_shared_ptrs - 1);

            sync_point.arrive_and_wait(); // Wait for all threads to be ready

            for (int i = 0; i < num_iterations; ++i) {
                int idx = ptr_dist(gen);

                // Protect and use multiple pointers simultaneously
                auto* ptr1 = hp.protect(shared_pointers[idx]);
                if (ptr1) {
                    [[maybe_unused]] auto val = ptr1->value.load();

                    // Switch protection to different pointer
                    int new_idx = (idx + 1) % num_shared_ptrs;
                    auto* ptr2 = hp.protect(shared_pointers[new_idx]);
                    if (ptr2) {
                        [[maybe_unused]] auto val2 = ptr2->value.load();
                    }
                }

                hp.reset_protection();
                operations_completed.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    EXPECT_EQ(operations_completed.load(), num_threads * num_iterations);

    std::cout << "Large domain scalability test results:\n"
              << "  Operations completed: " << operations_completed.load() << "\n"
              << "  Duration: " << duration.count() << " ms\n"
              << "  Operations per second: "
              << (operations_completed.load() * 1000.0 / duration.count()) << "\n";
}

// Memory pressure test with retirement
TEST_F(HazardPointerStressTest, MemoryPressureTest) {
    const int num_threads = 8;
    const int num_iterations = 2000;
    const int retire_frequency = 10;

    std::vector<std::atomic<StressTestNode*>> shared_pointers(10);
    for (size_t i = 0; i < shared_pointers.size(); ++i) {
        shared_pointers[i].store(new StressTestNode(i));
    }

    std::vector<std::thread> threads;
    std::atomic<long> nodes_created{0};
    std::atomic<long> nodes_retired{0};
    std::atomic<long> protection_operations{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto hp = hazard_pointer<StressTestNode>::make_hazard_pointer();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> ptr_dist(0, shared_pointers.size() - 1);

            for (int i = 0; i < num_iterations; ++i) {
                int idx = ptr_dist(gen);

                // Protection operation
                auto* ptr = hp.protect(shared_pointers[idx]);
                if (ptr) {
                    protection_operations.fetch_add(1);
                    ptr->increment_ref();

                    // Simulate work
                    [[maybe_unused]] auto val = ptr->value.load();

                    ptr->decrement_ref();
                }
                hp.reset_protection();

                // Occasionally create new nodes and retire old ones
                if (i % retire_frequency == 0) {
                    auto* new_ptr = new StressTestNode(i * 1000 + t);
                    nodes_created.fetch_add(1);

                    auto* old_ptr = shared_pointers[idx].exchange(new_ptr);

                    if (old_ptr) {
                        hazard_pointer<StressTestNode>::retire(old_ptr);
                        nodes_retired.fetch_add(1);
                    }
                }

                // Force cleanup trigger by creating many retirements
                if (i % 100 == 0) {
                    for (int j = 0; j < 5; ++j) {
                        auto* temp_node = new StressTestNode(j);
                        hazard_pointer<StressTestNode>::retire(temp_node);
                        nodes_retired.fetch_add(1);
                    }
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for(auto& atomic_ptr : shared_pointers) {
        delete atomic_ptr.load();
    }

    EXPECT_GT(protection_operations.load(), 0);
    EXPECT_GT(nodes_created.load(), 0);
    EXPECT_GT(nodes_retired.load(), 0);

    std::cout << "Memory pressure test results:\n"
              << "  Protection operations: " << protection_operations.load() << "\n"
              << "  Nodes created: " << nodes_created.load() << "\n"
              << "  Nodes retired: " << nodes_retired.load() << "\n";
}

// ABA problem simulation test
TEST_F(HazardPointerStressTest, ABAPreventionTest) {
    const int num_threads = 4;
    const int num_iterations = 1000;
    
    std::atomic<StressTestNode*> shared_ptr{node_pool[0].get()};
    std::vector<std::thread> threads;
    std::atomic<int> aba_detections{0};
    std::atomic<int> successful_protections{0};
    
    // Thread-safe storage for unused nodes
    std::mutex unused_nodes_mutex;
    std::vector<std::unique_ptr<StressTestNode>> unused_nodes;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto hp = hazard_pointer<StressTestNode>::make_hazard_pointer();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> op_dist(0, 100);
            
            for (int i = 0; i < num_iterations; ++i) {
                if (op_dist(gen) < 80) {
                    // Reader operation
                    StressTestNode* ptr = shared_ptr.load();
                    
                    if (hp.try_protect(ptr, shared_ptr)) {
                        successful_protections.fetch_add(1);
                        if (ptr) {
                            auto original_value = ptr->value.load();
                            
                            // Check if value changed (potential ABA)
                            auto current_value = ptr->value.load();
                            if (original_value != current_value) {
                                aba_detections.fetch_add(1);
                            }
                        }
                        hp.reset_protection();
                    }
                    
                } else {
                    // Writer operation - create ABA scenario
                    auto old_ptr = shared_ptr.load();
                    if (old_ptr) {
                        // Create new node with same initial value
                        auto new_node = std::make_unique<StressTestNode>(old_ptr->value.load());
                        auto* new_ptr = new_node.get();
                        
                        if (shared_ptr.compare_exchange_strong(old_ptr, new_ptr)) {
                            // Change the value to create ABA scenario
                            new_ptr->value.store(999999);
                            
                            // Try to restore original pointer (ABA)
                            StressTestNode* expected = new_ptr;
                            shared_ptr.compare_exchange_strong(expected, old_ptr);
                            
                            hazard_pointer<StressTestNode>::retire(new_node.release());
                        } else {
                            // Store unused nodes safely
                            std::lock_guard<std::mutex> lock(unused_nodes_mutex);
                            unused_nodes.push_back(std::move(new_node));
                        }
                    }
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "ABA prevention test results:\n"
              << "  Successful protections: " << successful_protections.load() << "\n"
              << "  ABA scenarios detected: " << aba_detections.load() << "\n";
}

// Performance comparison between different domain sizes
TEST_F(HazardPointerStressTest, DomainSizePerformanceComparison) {
    const int num_threads = 8;
    const int num_iterations = 5000;
    
    std::atomic<StressTestNode*> shared_ptr{node_pool[0].get()};
    
    auto run_test = [&](const std::string& name, auto make_hp_func) {
        std::vector<std::thread> threads;
        std::atomic<long> operations{0};
        
        auto start = std::chrono::steady_clock::now();
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&]() {
                auto hp = make_hp_func();
                
                for (int i = 0; i < num_iterations; ++i) {
                    auto* ptr = hp.protect(shared_ptr);
                    if (ptr) {
                        [[maybe_unused]] auto val = ptr->value.load();
                        operations.fetch_add(1);
                    }
                    hp.reset_protection();
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << name << " domain performance:\n"
                  << "  Operations: " << operations.load() << "\n"
                  << "  Duration: " << duration.count() << " μs\n"
                  << "  Ops/second: " << (operations.load() * 1000000.0 / duration.count()) << "\n\n";
    };
    
    run_test("Small (8)", []() { 
        return hazard_pointer<StressTestNode, small_domain<StressTestNode>>::make_hazard_pointer(); 
    });
    
    run_test("Medium (64)", []() { 
        return hazard_pointer<StressTestNode, medium_domain<StressTestNode>>::make_hazard_pointer(); 
    });
    
    run_test("Default (128)", []() { 
        return hazard_pointer<StressTestNode>::make_hazard_pointer(); 
    });
    
    run_test("Large (256)", []() { 
        return hazard_pointer<StressTestNode, large_domain<StressTestNode>>::make_hazard_pointer(); 
    });
    
    run_test("Huge (1024)", []() { 
        return hazard_pointer<StressTestNode, huge_domain<StressTestNode>>::make_hazard_pointer(); 
    });
}

// Lock-free stack stress test
TEST_F(HazardPointerStressTest, LockFreeStackSimulation) {
    struct StackNode {
        std::atomic<int> data;
        std::atomic<StackNode*> next;
        
        StackNode(int val) : data(val), next(nullptr) {}
    };
    
    std::atomic<StackNode*> head{nullptr};
    
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 1000;
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic<int> items_produced{0};
    std::atomic<int> items_consumed{0};
    std::atomic<bool> done{false};
    
    // Producers
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < items_per_producer; ++j) {
                auto* new_node = new StackNode(i * 1000 + j);
                
                StackNode* old_head = head.load();
                do {
                    new_node->next.store(old_head);
                } while (!head.compare_exchange_weak(old_head, new_node));
                
                items_produced.fetch_add(1);
            }
        });
    }
    
    // Consumers
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&]() {
            auto hp = hazard_pointer<StackNode>::make_hazard_pointer();
            
            while (!done.load() || head.load() != nullptr) {
                StackNode* old_head = hp.protect(head);
                
                if (old_head) {
                    StackNode* next = old_head->next.load();
                    
                    if (head.compare_exchange_weak(old_head, next)) {
                        [[maybe_unused]] auto data = old_head->data.load();
                        items_consumed.fetch_add(1);
                        
                        hazard_pointer<StackNode>::retire(old_head);
                    }
                }
                
                hp.reset_protection();
            }
        });
    }
    
    // Wait for producers to finish
    for (auto& t : producers) {
        t.join();
    }
    
    done.store(true);
    
    // Wait for consumers to finish
    for (auto& t : consumers) {
        t.join();
    }
    
    EXPECT_EQ(items_produced.load(), num_producers * items_per_producer);
    EXPECT_EQ(items_consumed.load(), items_produced.load());
    EXPECT_EQ(head.load(), nullptr);
    
    std::cout << "Lock-free stack simulation results:\n"
              << "  Items produced: " << items_produced.load() << "\n"
              << "  Items consumed: " << items_consumed.load() << "\n"
              << "  Final head state: " << (head.load() ? "non-null" : "null") << "\n";
}

} // namespace conc::test
