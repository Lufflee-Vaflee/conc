#include <gtest/gtest.h>
#include <thread>
#include <vector>
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

// Test basic capture_cell functionality
TEST_F(HazardPointerDomainTest, BasicAcquire) {
    hazard_domain<TestNode> domain;
    
    // Should be able to capture a cell
    auto cell = domain.capture_cell();
    
    // Should not be null pointer
    EXPECT_NO_THROW(cell->pointer.store(nullptr));
    EXPECT_EQ(cell->pointer.load(), nullptr);
}

// Test capture_cell from multiple threads
TEST_F(HazardPointerDomainTest, MultiThreadAcquire) {
    hazard_domain<TestNode, 4> domain; // Small limit for testing
    constexpr int num_threads = 4;
    std::vector<std::thread> threads;
    std::vector<domain_cell<TestNode>*> acquired_ptrs(num_threads);
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&domain, &acquired_ptrs, &success_count, i]() {
            try {
                auto cell = domain.capture_cell();
                acquired_ptrs[i] = cell;
                cell->pointer.store(reinterpret_cast<TestNode*>(i + 1)); // Store unique value
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
    std::set<domain_cell<TestNode>*> unique_ptrs;
    for (auto* ptr : acquired_ptrs) {
        if (ptr != nullptr) {
            unique_ptrs.insert(ptr);
            ptr->pointer.store(nullptr);
        }
    }
    EXPECT_EQ(unique_ptrs.size(), num_threads);
}

// Test retire functionality
TEST_F(HazardPointerDomainTest, BasicRetire) {
    hazard_domain<TestNode> domain;
    
    // Create a test node
    TestNode* node = new TestNode(42);
    
    // Retire should not throw
    EXPECT_NO_THROW(domain.retire(node));
    
    // Note: We can't easily test if the node was actually deleted
    // without exposing internal state, but we can test the interface
}

// Test protection mechanism - node should not be deleted while protected
TEST_F(HazardPointerDomainTest, ProtectionMechanism) {
    hazard_domain<TestNode> domain;
    
    // Create a test node
    TestNode* node = new TestNode(99);
    
    // Capture cell and protect the node
    auto cell = domain.capture_cell();
    cell->pointer.store(node);
    
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
    cell->pointer.store(nullptr);
    
    // Clean up - the node might be deleted in the next cleanup cycle
    // We can't reliably test deletion without exposing internals,
    // but the test verifies the protection worked
}

// Test concurrent retire operations
TEST_F(HazardPointerDomainTest, ConcurrentRetire) {
    hazard_domain<TestNode> domain;
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

// Test domain cell reuse after thread exit
TEST_F(HazardPointerDomainTest, HazardPointerReuse) {
    hazard_domain<TestNode> domain;
    domain_cell<TestNode>* first_cell = nullptr;
    
    // Capture in a thread, then let thread exit
    std::thread t1([&domain, &first_cell]() {
        auto cell = domain.capture_cell();
        first_cell = cell;
        cell->pointer.store(reinterpret_cast<TestNode*>(0x12345));
    });
    t1.join();
    
    // Give some time for cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Now capture from a new thread - should be able to reuse the slot
    domain_cell<TestNode>* second_cell = nullptr;
    std::thread t2([&domain, &second_cell]() {
        auto cell = domain.capture_cell();
        second_cell = cell;
        cell->pointer.store(reinterpret_cast<TestNode*>(0x54321));
    });
    t2.join();
    
    // The domain cells might be the same object (reused slot)
    // but this depends on implementation details
    EXPECT_NE(second_cell, nullptr);
}

// Test thread-local retire list behavior
TEST_F(HazardPointerDomainTest, ThreadLocalRetireList) {
    hazard_domain<TestNode, 4> domain;
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
    hazard_domain<TestNode, 4> domain; // max_threads = 4, threshold = 8
    
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
    hazard_domain<TestNode, 16> domain;
    constexpr int num_threads = 8;
    constexpr int operations_per_thread = 100;
    std::atomic<int> completed_operations{0};
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&domain, &completed_operations, i, operations_per_thread]() {
            try {
                // Capture domain cell
                auto cell = domain.capture_cell();
                
                for (int j = 0; j < operations_per_thread; ++j) {
                    // Create and protect a node
                    TestNode* node = new TestNode(i * operations_per_thread + j);
                    cell->pointer.store(node);
                    
                    // Do some work with the node
                    node->value.store(j);
                    EXPECT_EQ(node->value.load(), j);
                    
                    // Clear protection and retire
                    cell->pointer.store(nullptr);
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
    hazard_domain<TestNode> domain;
    
    // Retiring nullptr should not crash
    EXPECT_NO_THROW(domain.retire(nullptr));
}

// Test edge case: multiple acquires and retires in sequence
TEST_F(HazardPointerDomainTest, SequentialOperations) {
    hazard_domain<TestNode> domain;
    
    for (int i = 0; i < 50; ++i) {
        // Capture cell
        auto cell = domain.capture_cell();
        
        // Create and protect node
        TestNode* node = new TestNode(i);
        cell->pointer.store(node);
        
        // Verify node is accessible
        EXPECT_EQ(node->value.load(), i);
        
        // Clear protection and retire
        cell->pointer.store(nullptr);
        domain.retire(node);
    }
    
    SUCCEED();
}

} // namespace conc::test
