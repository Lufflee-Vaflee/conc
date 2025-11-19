#include "hazard_pointer.hpp"
#include "domain.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <random>

namespace conc::test {

// Test node for stress testing
struct DynamicTestNode {
    std::atomic<int> value{0};
    std::atomic<DynamicTestNode*> next{nullptr};
    std::atomic<int> reference_count{0};
    
    DynamicTestNode(int val) : value(val) {}
    
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

class DynamicOnlyHazardTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize with dynamically allocated objects
        for (size_t i = 0; i < shared_pointers.size(); ++i) {
            shared_pointers[i].store(new DynamicTestNode(i));
        }
    }
    
    void TearDown() override {
        // Cleanup hazard domain after each test to avoid false positives with sanitizers
        hazard_domain<DynamicTestNode>{}.delete_all();
        
        // Clean up any remaining objects
        for (auto& ptr : shared_pointers) {
            auto* obj = ptr.load();
            if (obj) {
                delete obj;
            }
        }
    }
    
    static constexpr size_t NUM_SHARED_PTRS = 5;
    std::array<std::atomic<DynamicTestNode*>, NUM_SHARED_PTRS> shared_pointers;
};

TEST_F(DynamicOnlyHazardTest, DynamicObjectsOnly) {
    const int num_threads = std::thread::hardware_concurrency();
    const int num_iterations = 10000;
    
    std::vector<std::thread> threads;
    std::atomic<long> total_protections{0};
    std::atomic<long> total_retirements{0};
    std::atomic<long> successful_try_protects{0};
    std::atomic<long> failed_try_protects{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto hp = hazard_pointer<DynamicTestNode>::make_hazard_pointer();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> ptr_dist(0, NUM_SHARED_PTRS - 1);
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
                        
                        // Use the protected data
                        [[maybe_unused]] auto val = ptr->value.load();
                        
                        ptr->decrement_ref();
                    }
                    hp.reset_protection();
                    
                } else if (operation < 80) {
                    // 40% chance: try_protect operation
                    DynamicTestNode* ptr = shared_pointers[ptr_idx].load();
                    if (hp.try_protect(ptr, shared_pointers[ptr_idx])) {
                        successful_try_protects.fetch_add(1);
                        if (ptr) {
                            ptr->increment_ref();
                            [[maybe_unused]] auto val = ptr->value.load();
                            ptr->decrement_ref();
                        }
                        hp.reset_protection();
                    } else {
                        failed_try_protects.fetch_add(1);
                    }
                    
                } else {
                    // 20% chance: pointer replacement
                    auto old_ptr = shared_pointers[ptr_idx].load();
                    if (old_ptr) { // More frequent replacement
                        auto* new_ptr = new DynamicTestNode(i * 10000 + t);
                        
                        if (shared_pointers[ptr_idx].compare_exchange_strong(old_ptr, new_ptr)) {
                            // Always retire since all objects are dynamically allocated
                            hazard_pointer<DynamicTestNode>::retire(old_ptr);
                            total_retirements.fetch_add(1);
                        } else {
                            // CAS failed, delete the new node we created
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
    
    EXPECT_GT(total_protections.load(), 0);
    EXPECT_GT(successful_try_protects.load(), 0);
    
    std::cout << "Dynamic objects only test results:\n"
              << "  Total protections: " << total_protections.load() << "\n"
              << "  Successful try_protects: " << successful_try_protects.load() << "\n"
              << "  Failed try_protects: " << failed_try_protects.load() << "\n"
              << "  Total retirements: " << total_retirements.load() << "\n";
}

} // namespace conc::test
