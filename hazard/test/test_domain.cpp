#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>
#include <set>
#include "domain.hpp"

namespace conc::test {

// Simple test object for hazard pointer testing
struct TestNode {
    std::atomic<int> value{0};
    TestNode* next{nullptr};
    
    TestNode(int val = 0) : value(val) {}
};

class HazardPointerDomainTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset any static state if needed
    }
    
    void TearDown() override {
        // Cleanup any test data
    }
};

// Test basic acquire functionality
TEST_F(HazardPointerDomainTest, BasicAcquire) {
    hazard_pointer_domain<TestNode> domain;
    
    // Should be able to acquire a hazard pointer
    auto& hp = domain.acquire();
    
    // Should not be null pointer
    EXPECT_NO_THROW(hp.store(nullptr));
    EXPECT_EQ(hp.load(), nullptr);
}

// Test multiple acquire calls from same thread
TEST_F(HazardPointerDomainTest, MultipleAcquireFromSameThread) {
    hazard_pointer_domain<TestNode> domain;
    
    // First acquire should succeed
    auto& hp1 = domain.acquire();
    hp1.store(nullptr);
    
    // Second acquire should also succeed (different slot)
    auto& hp2 = domain.acquire();
    hp2.store(nullptr);
    
    // They should be different objects
    EXPECT_NE(&hp1, &hp2);
}

// Test acquire from multiple threads
TEST_F(HazardPointerDomainTest, MultiThreadAcquire) {
    hazard_pointer_domain<TestNode, 4> domain; // Small limit for testing
    constexpr int num_threads = 4;
    std::vector<std::thread> threads;
    std::vector<typename hazard_pointer_domain<TestNode>::hazard_pointer_t*> acquired_ptrs(num_threads);
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&domain, &acquired_ptrs, &success_count, i]() {
            try {
                auto& hp = domain.acquire();
                acquired_ptrs[i] = &hp;
                hp.store(reinterpret_cast<TestNode*>(i + 1)); // Store unique value
                success_count++;
                
                // Hold for a bit to ensure concurrent access
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } catch (...) {
                // Expected if we exceed max_threads
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count.load(), num_threads);
    
    // Verify all acquired pointers are unique
    std::set<typename hazard_pointer_domain<TestNode>::hazard_pointer_t*> unique_ptrs;
    for (auto* ptr : acquired_ptrs) {
        if (ptr != nullptr) {
            unique_ptrs.insert(ptr);
        }
    }
    EXPECT_EQ(unique_ptrs.size(), num_threads);
}

// Test retire functionality
TEST_F(HazardPointerDomainTest, BasicRetire) {
    hazard_pointer_domain<TestNode> domain;
    
    // Create a test node
    TestNode* node = new TestNode(42);
    
    // Retire should not throw
    EXPECT_NO_THROW(domain.retire(node));
    
    // Note: We can't easily test if the node was actually deleted
    // without exposing internal state, but we can test the interface
}

// Test protection mechanism - node should not be deleted while protected
TEST_F(HazardPointerDomainTest, ProtectionMechanism) {
    hazard_pointer_domain<TestNode> domain;
    
    // Create a test node
    TestNode* node = new TestNode(99);
    
    // Acquire hazard pointer and protect the node
    auto& hp = domain.acquire();
    hp.store(node);
    
    // Retire the node
    domain.retire(node);
    
    // Force cleanup by retiring many more nodes to trigger delete_hazards()
    for (int i = 0; i < 300; ++i) {
        TestNode* temp_node = new TestNode(i);
        domain.retire(temp_node);
    }
    
    // The original node should still be accessible (not deleted)
    // because it's protected by the hazard pointer
    EXPECT_EQ(node->value.load(), 99);
    
    // Clear the hazard pointer
    hp.store(nullptr);
    
    // Clean up - the node might be deleted in the next cleanup cycle
    // We can't reliably test deletion without exposing internals,
    // but the test verifies the protection worked
}

// Test concurrent retire operations
TEST_F(HazardPointerDomainTest, ConcurrentRetire) {
    hazard_pointer_domain<TestNode> domain;
    constexpr int num_threads = 8;
    constexpr int nodes_per_thread = 50;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&domain, i, nodes_per_thread]() {
            for (int j = 0; j < nodes_per_thread; ++j) {
                TestNode* node = new TestNode(i * nodes_per_thread + j);
                domain.retire(node);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Test passes if no crashes or exceptions occurred
    SUCCEED();
}

// Test acquire limit enforcement
TEST_F(HazardPointerDomainTest, AcquireLimitEnforcement) {
    hazard_pointer_domain<TestNode, 2> domain; // Very small limit
    
    // Create threads that will try to acquire more hazard pointers than available
    std::atomic<int> exception_count{0};
    std::vector<std::thread> threads;
    constexpr int num_threads = 5; // More than the limit of 2
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&domain, &exception_count]() {
            try {
                auto& hp = domain.acquire();
                hp.store(nullptr);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } catch (...) {
                exception_count++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Some threads should have thrown exceptions due to limit
    EXPECT_GT(exception_count.load(), 0);
    EXPECT_LE(exception_count.load(), num_threads);
}

// Test hazard pointer reuse after thread exit
TEST_F(HazardPointerDomainTest, HazardPointerReuse) {
    hazard_pointer_domain<TestNode> domain;
    typename hazard_pointer_domain<TestNode>::hazard_pointer_t* first_hp = nullptr;
    
    // Acquire in a thread, then let thread exit
    std::thread t1([&domain, &first_hp]() {
        auto& hp = domain.acquire();
        first_hp = &hp;
        hp.store(reinterpret_cast<TestNode*>(0x12345));
    });
    t1.join();
    
    // Give some time for cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Now acquire from a new thread - should be able to reuse the slot
    typename hazard_pointer_domain<TestNode>::hazard_pointer_t* second_hp = nullptr;
    std::thread t2([&domain, &second_hp]() {
        auto& hp = domain.acquire();
        second_hp = &hp;
        hp.store(reinterpret_cast<TestNode*>(0x54321));
    });
    t2.join();
    
    // The hazard pointers might be the same object (reused slot)
    // but this depends on implementation details
    EXPECT_NE(second_hp, nullptr);
}

// Test thread-local retire list behavior
TEST_F(HazardPointerDomainTest, ThreadLocalRetireList) {
    hazard_pointer_domain<TestNode, 4> domain;
    std::atomic<int> total_retires{0};
    
    // Multiple threads retiring different numbers of objects
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&domain, &total_retires, i]() {
            int retire_count = (i + 1) * 10;
            for (int j = 0; j < retire_count; ++j) {
                TestNode* node = new TestNode(i * 100 + j);
                domain.retire(node);
                total_retires++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(total_retires.load(), 10 + 20 + 30 + 40); // 100 total
}

// Test cleanup trigger threshold
TEST_F(HazardPointerDomainTest, CleanupTriggerThreshold) {
    hazard_pointer_domain<TestNode, 4> domain; // max_threads = 4, threshold = 8
    
    // Retire exactly enough to trigger cleanup (more than max_threads * 2)
    for (int i = 0; i < 10; ++i) {
        TestNode* node = new TestNode(i);
        domain.retire(node);
    }
    
    // Test passes if no crashes occur during cleanup
    SUCCEED();
}

// Stress test with many concurrent operations
TEST_F(HazardPointerDomainTest, StressTest) {
    hazard_pointer_domain<TestNode, 16> domain;
    constexpr int num_threads = 8;
    constexpr int operations_per_thread = 100;
    std::atomic<int> completed_operations{0};
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&domain, &completed_operations, i, operations_per_thread]() {
            try {
                // Acquire hazard pointer
                auto& hp = domain.acquire();
                
                for (int j = 0; j < operations_per_thread; ++j) {
                    // Create and protect a node
                    TestNode* node = new TestNode(i * operations_per_thread + j);
                    hp.store(node);
                    
                    // Do some work with the node
                    node->value.store(j);
                    EXPECT_EQ(node->value.load(), j);
                    
                    // Clear protection and retire
                    hp.store(nullptr);
                    domain.retire(node);
                    
                    completed_operations++;
                }
            } catch (...) {
                // Some operations might fail due to resource limits
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Should have completed a significant number of operations
    EXPECT_GT(completed_operations.load(), 0);
}

// Test edge case: retire nullptr
TEST_F(HazardPointerDomainTest, RetireNullptr) {
    hazard_pointer_domain<TestNode> domain;
    
    // Retiring nullptr should not crash
    EXPECT_NO_THROW(domain.retire(nullptr));
}

// Test edge case: multiple acquires and retires in sequence
TEST_F(HazardPointerDomainTest, SequentialOperations) {
    hazard_pointer_domain<TestNode> domain;
    
    for (int i = 0; i < 50; ++i) {
        // Acquire
        auto& hp = domain.acquire();
        
        // Create and protect node
        TestNode* node = new TestNode(i);
        hp.store(node);
        
        // Verify node is accessible
        EXPECT_EQ(node->value.load(), i);
        
        // Clear protection and retire
        hp.store(nullptr);
        domain.retire(node);
    }
    
    SUCCEED();
}

} // namespace conc::test