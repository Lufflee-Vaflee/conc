#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include "hazard_pointer.hpp"

namespace conc::test {

// Test node for hazard pointer testing
struct TestNode : public hazard_pointer_obj_base<TestNode> {
    std::atomic<int> value{0};
    std::atomic<TestNode*> next{nullptr};
    
    TestNode(int val = 0) : value(val) {}
    
    // Custom constructor with domain
    TestNode(int val, hazard_pointer_domain<TestNode>& domain) 
        : hazard_pointer_obj_base<TestNode>(domain), value(val) {}
};

class HazardPointerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean slate for each test
    }
    
    void TearDown() override {
        // Cleanup
    }
};

// Test basic hazard pointer construction and destruction
TEST_F(HazardPointerTest, BasicConstruction) {
    // Should construct without throwing
    EXPECT_NO_THROW({
        hazard_pointer<TestNode> hp;
        EXPECT_TRUE(hp); // Should be valid
    });
}

// Test move constructor and assignment
TEST_F(HazardPointerTest, MoveSemantics) {
    hazard_pointer<TestNode> hp1;
    EXPECT_TRUE(hp1);
    
    // Move constructor
    hazard_pointer<TestNode> hp2 = std::move(hp1);
    EXPECT_TRUE(hp2);
    EXPECT_FALSE(hp1); // hp1 should be empty after move
    
    // Move assignment
    hazard_pointer<TestNode> hp3;
    hp3 = std::move(hp2);
    EXPECT_TRUE(hp3);
    EXPECT_FALSE(hp2); // hp2 should be empty after move
}

// Test basic protection mechanism
TEST_F(HazardPointerTest, BasicProtection) {
    hazard_pointer<TestNode> hp;
    
    // Create a test node
    TestNode* node = new TestNode(42);
    std::atomic<TestNode*> atomic_ptr{node};
    
    // Protect the pointer
    TestNode* protected_ptr = hp.protect(atomic_ptr);
    EXPECT_EQ(protected_ptr, node);
    EXPECT_EQ(protected_ptr->value.load(), 42);
    
    // Check that we can get the protected pointer
    EXPECT_EQ(hp.get(), node);
    
    // Clean up
    hp.reset();
    delete node;
}

// Test try_protect functionality
TEST_F(HazardPointerTest, TryProtect) {
    hazard_pointer<TestNode> hp;
    
    TestNode* node = new TestNode(99);
    std::atomic<TestNode*> atomic_ptr{node};
    
    TestNode* ptr = node;
    bool result = hp.try_protect(ptr, atomic_ptr);
    
    EXPECT_TRUE(result);
    EXPECT_EQ(ptr, node);
    EXPECT_EQ(hp.get(), node);
    
    // Clean up
    hp.reset();
    delete node;
}

// Test try_protect with changing pointer
TEST_F(HazardPointerTest, TryProtectWithChange) {
    hazard_pointer<TestNode> hp;
    
    TestNode* node1 = new TestNode(1);
    TestNode* node2 = new TestNode(2);
    std::atomic<TestNode*> atomic_ptr{node1};
    
    // Start with node1
    TestNode* ptr = node1;
    bool result = hp.try_protect(ptr, atomic_ptr);
    EXPECT_TRUE(result);
    EXPECT_EQ(ptr, node1);
    
    // Change the atomic pointer
    atomic_ptr.store(node2);
    
    // Try to protect again - should fail and update ptr
    result = hp.try_protect(ptr, atomic_ptr);
    EXPECT_FALSE(result);
    EXPECT_EQ(ptr, node2); // ptr should be updated to node2
    
    // Clean up
    hp.reset();
    delete node1;
    delete node2;
}

// Test reset functionality
TEST_F(HazardPointerTest, Reset) {
    hazard_pointer<TestNode> hp;
    
    TestNode* node = new TestNode(123);
    hp.reset(node);
    
    EXPECT_EQ(hp.get(), node);
    
    // Reset to nullptr
    hp.reset();
    EXPECT_EQ(hp.get(), nullptr);
    
    delete node;
}

// Test swap functionality
TEST_F(HazardPointerTest, Swap) {
    hazard_pointer<TestNode> hp1;
    hazard_pointer<TestNode> hp2;
    
    TestNode* node1 = new TestNode(1);
    TestNode* node2 = new TestNode(2);
    
    hp1.reset(node1);
    hp2.reset(node2);
    
    EXPECT_EQ(hp1.get(), node1);
    EXPECT_EQ(hp2.get(), node2);
    
    // Swap using member function
    hp1.swap(hp2);
    
    EXPECT_EQ(hp1.get(), node2);
    EXPECT_EQ(hp2.get(), node1);
    
    // Swap using free function
    swap(hp1, hp2);
    
    EXPECT_EQ(hp1.get(), node1);
    EXPECT_EQ(hp2.get(), node2);
    
    hp1.reset();
    hp2.reset();
    delete node1;
    delete node2;
}

// Test hazard_pointer_obj_base retirement
TEST_F(HazardPointerTest, ObjectRetirement) {
    hazard_pointer<TestNode> hp;
    
    // Create a node that can retire itself
    TestNode* node = new TestNode(456);
    std::atomic<TestNode*> atomic_ptr{node};
    
    // Protect the node
    TestNode* protected_ptr = hp.protect(atomic_ptr);
    EXPECT_EQ(protected_ptr->value.load(), 456);
    
    // Retire the node (it should not be deleted immediately because it's protected)
    node->retire();
    
    // The node should still be accessible since it's protected
    EXPECT_EQ(protected_ptr->value.load(), 456);
    
    // Clear protection - now it might be deleted in cleanup
    hp.reset();
}

// Test multiple hazard pointers protecting the same object
TEST_F(HazardPointerTest, MultipleProtection) {
    hazard_pointer<TestNode> hp1;
    hazard_pointer<TestNode> hp2;
    
    TestNode* node = new TestNode(789);
    std::atomic<TestNode*> atomic_ptr{node};
    
    // Both hazard pointers protect the same object
    TestNode* ptr1 = hp1.protect(atomic_ptr);
    TestNode* ptr2 = hp2.protect(atomic_ptr);
    
    EXPECT_EQ(ptr1, node);
    EXPECT_EQ(ptr2, node);
    EXPECT_EQ(hp1.get(), node);
    EXPECT_EQ(hp2.get(), node);
    
    // Clean up
    hp1.reset();
    hp2.reset();
    delete node;
}

// Test concurrent protection from multiple threads
TEST_F(HazardPointerTest, ConcurrentProtection) {
    constexpr int num_threads = 4;
    constexpr int num_nodes = 10;
    
    // Create multiple nodes
    std::vector<std::unique_ptr<TestNode>> nodes;
    std::vector<std::atomic<TestNode*>> atomic_ptrs;
    
    for (int i = 0; i < num_nodes; ++i) {
        nodes.emplace_back(std::make_unique<TestNode>(i));
        atomic_ptrs.emplace_back(nodes[i].get());
    }
    
    std::atomic<int> protection_count{0};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            hazard_pointer<TestNode> hp;
            
            // Each thread protects different nodes
            for (int i = 0; i < num_nodes; ++i) {
                int node_idx = (t + i) % num_nodes;
                TestNode* protected_ptr = hp.protect(atomic_ptrs[node_idx]);
                
                if (protected_ptr && protected_ptr->value.load() == node_idx) {
                    protection_count++;
                }
                
                // Brief hold
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(protection_count.load(), num_threads * num_nodes);
}

// Test make_hazard_pointer convenience function
TEST_F(HazardPointerTest, MakeHazardPointer) {
    auto hp = make_hazard_pointer<TestNode>();
    EXPECT_TRUE(hp);
    
    TestNode* node = new TestNode(321);
    hp.reset(node);
    EXPECT_EQ(hp.get(), node);
    
    hp.reset();
    delete node;
}

// Test hazard_pointer_retire convenience function
TEST_F(HazardPointerTest, HazardPointerRetire) {
    hazard_pointer<TestNode> hp;
    
    TestNode* node = new TestNode(654);
    std::atomic<TestNode*> atomic_ptr{node};
    
    // Protect the node
    TestNode* protected_ptr = hp.protect(atomic_ptr);
    EXPECT_EQ(protected_ptr->value.load(), 654);
    
    // Retire using convenience function
    hazard_pointer_retire(node);
    
    // Node should still be accessible
    EXPECT_EQ(protected_ptr->value.load(), 654);
    
    // Clear protection
    hp.reset();
}

// Test with custom domain
TEST_F(HazardPointerTest, CustomDomain) {
    hazard_pointer_domain<TestNode> custom_domain;
    hazard_pointer<TestNode> hp(custom_domain);
    
    EXPECT_TRUE(hp);
    
    TestNode* node = new TestNode(987, custom_domain);
    std::atomic<TestNode*> atomic_ptr{node};
    
    TestNode* protected_ptr = hp.protect(atomic_ptr);
    EXPECT_EQ(protected_ptr->value.load(), 987);
    
    // Retire through the object
    node->retire();
    
    // Clear protection
    hp.reset();
}

// Test exception safety during construction
TEST_F(HazardPointerTest, ConstructionExceptionSafety) {
    // Create a domain with very small capacity
    hazard_pointer_domain<TestNode, 2> small_domain;
    
    // Fill up the domain
    hazard_pointer<TestNode> hp1(small_domain);
    hazard_pointer<TestNode> hp2(small_domain);
    
    EXPECT_TRUE(hp1);
    EXPECT_TRUE(hp2);
    
    // This should throw because domain is full
    EXPECT_THROW({
        hazard_pointer<TestNode> hp3(small_domain);
    }, const char*);
}

// Stress test with many operations
TEST_F(HazardPointerTest, StressTest) {
    constexpr int num_operations = 100;
    constexpr int num_nodes = 20;
    
    // Create nodes
    std::vector<std::unique_ptr<TestNode>> nodes;
    std::vector<std::atomic<TestNode*>> atomic_ptrs;
    
    for (int i = 0; i < num_nodes; ++i) {
        nodes.emplace_back(std::make_unique<TestNode>(i));
        atomic_ptrs.emplace_back(nodes[i].get());
    }
    
    std::atomic<int> successful_protections{0};
    
    auto worker = [&]() {
        hazard_pointer<TestNode> hp;
        
        for (int i = 0; i < num_operations; ++i) {
            int node_idx = i % num_nodes;
            TestNode* protected_ptr = hp.protect(atomic_ptrs[node_idx]);
            
            if (protected_ptr && protected_ptr->value.load() == node_idx) {
                successful_protections++;
            }
            
            // Occasionally reset
            if (i % 10 == 0) {
                hp.reset();
            }
        }
    };
    
    std::thread t1(worker);
    std::thread t2(worker);
    
    t1.join();
    t2.join();
    
    EXPECT_GT(successful_protections.load(), 0);
}

} // namespace conc::test