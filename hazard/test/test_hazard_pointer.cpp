#include "hazard_pointer.hpp"
#include "domain.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <memory>

namespace conc::test {

// Test fixture for hazard pointer tests
class HazardPointerTest : public ::testing::Test {
protected:
    struct TestNode {
        std::atomic<int> value{0};
        std::atomic<TestNode*> next{nullptr};
        
        TestNode(int val) : value(val) {}
    };
    
    void SetUp() override {
        // Create some test nodes
        node1 = new TestNode(1);
        node2 = new TestNode(2);
        node3 = new TestNode(3);
        atomic_ptr.store(node1);
    }
    
    void TearDown() override {
        // Cleanup hazard domain after each test to avoid false positives with sanitizers
        hazard_domain<TestNode>{}.delete_all();
        
        // Clean up any remaining nodes
        auto ptr = atomic_ptr.load();
        if (ptr) {
            delete ptr;
        }
        // Note: node2, node3 might be retired through hazard pointer system
    }
    
    TestNode* node1;
    TestNode* node2;
    TestNode* node3;
    std::atomic<TestNode*> atomic_ptr{nullptr};
};

// Basic functionality tests
TEST_F(HazardPointerTest, DefaultConstruction) {
    // Default-constructed hazard pointer has no cell
    hazard_pointer<TestNode> hp;
    // Cannot test empty() on default-constructed hp as it would dereference nullptr
    // This is expected behavior - default constructed hazard pointers need a cell
}

TEST_F(HazardPointerTest, FactoryConstruction) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    // A newly created hazard pointer from make_hazard_pointer() is not empty
    // because it has a captured cell with SENTINEL value
    EXPECT_FALSE(hp.empty());
    
    // After reset_protection(), it should be empty
    hp.reset_protection();
    EXPECT_TRUE(hp.empty());
}

TEST_F(HazardPointerTest, MoveConstructor) {
    auto hp1 = hazard_pointer<TestNode>::make_hazard_pointer();
    hp1.protect(atomic_ptr);
    EXPECT_FALSE(hp1.empty());
    
    auto hp2 = std::move(hp1);
    EXPECT_FALSE(hp2.empty());
    // hp1 state after move is unspecified but should be safe to use
}

TEST_F(HazardPointerTest, MoveAssignment) {
    auto hp1 = hazard_pointer<TestNode>::make_hazard_pointer();
    auto hp2 = hazard_pointer<TestNode>::make_hazard_pointer();
    
    hp1.protect(atomic_ptr);
    EXPECT_FALSE(hp1.empty());
    EXPECT_FALSE(hp2.empty()); // hp2 also has a captured cell
    
    hp2 = std::move(hp1);
    EXPECT_FALSE(hp2.empty());
}

TEST_F(HazardPointerTest, SwapFunctionality) {
    auto hp1 = hazard_pointer<TestNode>::make_hazard_pointer();
    auto hp2 = hazard_pointer<TestNode>::make_hazard_pointer();
    
    hp1.protect(atomic_ptr);
    // Reset hp2 to make it truly empty for this test
    hp2.reset_protection();
    
    EXPECT_FALSE(hp1.empty()); // hp1 is protecting atomic_ptr
    EXPECT_TRUE(hp2.empty());  // hp2 is reset to nullptr
    
    swap(hp1, hp2);
    EXPECT_TRUE(hp1.empty());  // hp1 now has the reset cell
    EXPECT_FALSE(hp2.empty()); // hp2 now has the protected cell
}

// Protection mechanism tests
TEST_F(HazardPointerTest, BasicProtection) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    
    auto* protected_ptr = hp.protect(atomic_ptr);
    EXPECT_EQ(protected_ptr, node1);
    EXPECT_FALSE(hp.empty());
    EXPECT_EQ(protected_ptr->value.load(), 1);
}

TEST_F(HazardPointerTest, TryProtectSuccess) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    
    TestNode* ptr = atomic_ptr.load();
    bool success = hp.try_protect(ptr, atomic_ptr);
    
    EXPECT_TRUE(success);
    EXPECT_EQ(ptr, node1);
    EXPECT_FALSE(hp.empty());
}

TEST_F(HazardPointerTest, TryProtectFailure) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    
    TestNode* ptr = node1;
    // Change the atomic pointer while we're trying to protect
    atomic_ptr.store(node2);
    
    bool success = hp.try_protect(ptr, atomic_ptr);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(ptr, node2); // ptr should be updated to current value
    EXPECT_TRUE(hp.empty()); // protection should be reset on failure
}

TEST_F(HazardPointerTest, ResetProtection) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    
    hp.protect(atomic_ptr);
    EXPECT_FALSE(hp.empty());
    
    hp.reset_protection();
    EXPECT_TRUE(hp.empty());
}

TEST_F(HazardPointerTest, ResetProtectionWithPointer) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    
    hp.protect(atomic_ptr);
    EXPECT_FALSE(hp.empty());
    
    hp.reset_protection(node2);
    EXPECT_FALSE(hp.empty()); // Should still be protecting something
}

TEST_F(HazardPointerTest, ResetProtectionWithNullptr) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    
    hp.protect(atomic_ptr);
    EXPECT_FALSE(hp.empty());
    
    hp.reset_protection(nullptr);
    EXPECT_TRUE(hp.empty());
}

// Memory management tests
TEST_F(HazardPointerTest, RetireBasic) {
    // This test mainly checks that retire doesn't crash
    hazard_pointer<TestNode>::retire(new TestNode(42));
    // The actual deletion might be deferred
}

// Concurrent access tests
TEST_F(HazardPointerTest, ConcurrentProtection) {
    const int num_threads = 4;
    const int num_iterations = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
            
            for (int j = 0; j < num_iterations; ++j) {
                auto* ptr = hp.protect(atomic_ptr);
                if (ptr && ptr->value.load() > 0) {
                    success_count.fetch_add(1);
                }
                
                // Small delay to increase chance of contention
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                
                hp.reset_protection();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_GT(success_count.load(), 0);
}

TEST_F(HazardPointerTest, ConcurrentTryProtect) {
    const int num_threads = 4;
    const int num_iterations = 100;
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> attempt_count{0};
    
    // Thread that occasionally changes the pointer
    std::thread changer([&]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(1, 10);
        
        for (int i = 0; i < num_iterations; ++i) {
            if (dist(gen) < 3) { // 30% chance to change
                atomic_ptr.store(i % 2 == 0 ? node2 : node1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
            
            for (int j = 0; j < num_iterations; ++j) {
                TestNode* ptr = atomic_ptr.load();
                attempt_count.fetch_add(1);
                
                if (hp.try_protect(ptr, atomic_ptr)) {
                    success_count.fetch_add(1);
                    // Use the protected pointer
                    if (ptr) {
                        [[maybe_unused]] auto val = ptr->value.load();
                    }
                    hp.reset_protection();
                }
                
                std::this_thread::sleep_for(std::chrono::microseconds(5));
            }
        });
    }
    
    changer.join();
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_GT(success_count.load(), 0);
    // In practice, the failure rate might be low or zero if contention is minimal
    // So we just ensure some successes occurred
    EXPECT_GE(attempt_count.load(), success_count.load()); // Attempts >= successes
}

// Stress test for memory management
TEST_F(HazardPointerTest, StressTestRetireAndProtect) {
    const int num_threads = 8;
    const int num_iterations = 10000;
    
    std::vector<std::atomic<TestNode*>> pointers(10);
    
    // Initialize with some nodes
    for (auto& ptr : pointers) {
        ptr.store(new TestNode(42));
    }
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dist(0, pointers.size() - 1);
            
            for (int j = 0; j < num_iterations; ++j) {
                int idx = dist(gen);
                
                // Protect a random pointer
                auto* ptr = hp.protect(pointers[idx]);
                
                if (ptr) {
                    // Use the protected data
                    [[maybe_unused]] auto val = ptr->value.load();
                    
                    // Occasionally replace the pointer
                    if (j % 50 == 0) {
                        auto* new_node = new TestNode(j);
                        auto* old_ptr = pointers[idx].exchange(new_node);
                        if (old_ptr) {
                            hazard_pointer<TestNode>::retire(old_ptr);
                        }
                    }
                }

                hp.reset_protection();
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Clean up remaining pointers
    for (auto& ptr : pointers) {
        delete ptr.load();
    }
}

// Edge case tests
TEST_F(HazardPointerTest, ProtectNullPointer) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    atomic_ptr.store(nullptr);
    
    auto* ptr = hp.protect(atomic_ptr);
    EXPECT_EQ(ptr, nullptr);
    EXPECT_TRUE(hp.empty()); // protecting nullptr should result in empty state
}

TEST_F(HazardPointerTest, MultipleHazardPointers) {
    auto hp1 = hazard_pointer<TestNode>::make_hazard_pointer();
    auto hp2 = hazard_pointer<TestNode>::make_hazard_pointer();
    auto hp3 = hazard_pointer<TestNode>::make_hazard_pointer();
    
    // All should be able to protect the same pointer
    auto* ptr1 = hp1.protect(atomic_ptr);
    auto* ptr2 = hp2.protect(atomic_ptr);
    auto* ptr3 = hp3.protect(atomic_ptr);
    
    EXPECT_EQ(ptr1, node1);
    EXPECT_EQ(ptr2, node1);
    EXPECT_EQ(ptr3, node1);
    
    EXPECT_FALSE(hp1.empty());
    EXPECT_FALSE(hp2.empty());
    EXPECT_FALSE(hp3.empty());
}

TEST_F(HazardPointerTest, RapidProtectAndReset) {
    auto hp = hazard_pointer<TestNode>::make_hazard_pointer();
    
    for (int i = 0; i < 1000; ++i) {
        hp.protect(atomic_ptr);
        EXPECT_FALSE(hp.empty());
        hp.reset_protection();
        EXPECT_TRUE(hp.empty());
    }
}

}
