#include <gtest/gtest.h>
#include <vector>
#include <list>
#include <type_traits>
#include <cstring>
#include "allocator.hpp"
#include <deque>

namespace conc::test {

// Test basic allocation and cache alignment
TEST(AllocatorTest, BasicAllocation) {
    cache_aligned_alloc<int> alloc;
    
    // Test single allocation
    int* ptr = alloc.allocate(1);
    ASSERT_NE(ptr, nullptr);
    
    // Check cache line alignment
    constexpr auto cacheline_size = std::hardware_destructive_interference_size;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % cacheline_size, 0)
        << "Allocation should be cache-line aligned";
    
    // Use the memory
    *ptr = 42;
    EXPECT_EQ(*ptr, 42);
    
    // Deallocate
    alloc.deallocate(ptr, 1);
}

// Test allocation of multiple elements with alignment
TEST(AllocatorTest, MultipleElementAllocation) {
    cache_aligned_alloc<double> alloc;
    
    // Allocate array of 10 doubles
    double* ptr = alloc.allocate(10);
    ASSERT_NE(ptr, nullptr);
    
    // Check cache line alignment
    constexpr auto cacheline_size = std::hardware_destructive_interference_size;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % cacheline_size, 0)
        << "Allocation should be cache-line aligned";
    
    // Initialize and verify
    for (int i = 0; i < 10; ++i) {
        ptr[i] = i * 2.5;
    }
    
    for (int i = 0; i < 10; ++i) {
        EXPECT_DOUBLE_EQ(ptr[i], i * 2.5);
    }
    
    alloc.deallocate(ptr, 10);
}

// Test allocator traits and properties
TEST(AllocatorTest, AllocatorTraits) {
    using Alloc = cache_aligned_alloc<int>;
    
    // Test type traits
    static_assert(std::is_same_v<Alloc::value_type, int>);
    static_assert(std::is_same_v<Alloc::size_type, std::size_t>);
    static_assert(std::is_same_v<Alloc::difference_type, std::ptrdiff_t>);
    static_assert(std::is_same_v<Alloc::pointer, int*>);
    static_assert(std::is_same_v<Alloc::const_pointer, const int*>);
    
    // Test propagation traits for stateless allocator
    static_assert(Alloc::propagate_on_container_copy_assignment::value, 
                  "Stateless allocator should propagate on copy assignment");
    static_assert(Alloc::propagate_on_container_move_assignment::value,
                  "Stateless allocator should propagate on move assignment");
    static_assert(Alloc::propagate_on_container_swap::value,
                  "Stateless allocator should propagate on swap");
    static_assert(Alloc::is_always_equal::value,
                  "Stateless allocator instances should always be equal");
}

// Test allocator equality
TEST(AllocatorTest, AllocatorEquality) {
    cache_aligned_alloc<int> alloc1;
    cache_aligned_alloc<int> alloc2;
    
    EXPECT_TRUE(alloc1 == alloc2);
    EXPECT_FALSE(alloc1 != alloc2);
}

// Test rebind functionality
TEST(AllocatorTest, RebindAllocator) {
    cache_aligned_alloc<int> int_alloc;
    cache_aligned_alloc<double> double_alloc(int_alloc);  // Should compile due to converting constructor
    
    double* ptr = double_alloc.allocate(5);
    ASSERT_NE(ptr, nullptr);
    
    // Check alignment
    constexpr auto cacheline_size = std::hardware_destructive_interference_size;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % cacheline_size, 0);
    
    for (int i = 0; i < 5; ++i) {
        ptr[i] = i * 1.5;
    }
    
    for (int i = 0; i < 5; ++i) {
        EXPECT_DOUBLE_EQ(ptr[i], i * 1.5);
    }
    
    double_alloc.deallocate(ptr, 5);
}

// Test select_on_container_copy_construction
TEST(AllocatorTest, SelectOnCopy) {
    cache_aligned_alloc<int> alloc1;
    auto alloc2 = alloc1.select_on_container_copy_construction();
    
    // Should return a default-constructed allocator (they're all equal anyway)
    EXPECT_TRUE(alloc1 == alloc2);
}

// Test with STL containers
TEST(AllocatorTest, VectorIntegration) {
    using AllocVector = std::vector<int, cache_aligned_alloc<int>>;
    
    AllocVector vec;
    
    // Add some elements
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }
    
    EXPECT_EQ(vec.size(), 100);
    
    // Verify contents
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(vec[i], i);
    }
    
    // Check that data is cache-line aligned
    constexpr auto cacheline_size = std::hardware_destructive_interference_size;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(vec.data()) % cacheline_size, 0)
        << "Vector data should be cache-line aligned";
}

// Test with STL containers
TEST(AllocatorTest, DequeIntegration) {
    using AllocDeq = std::deque<int, cache_aligned_alloc<int>>;
    
    AllocDeq deq;
    
    // Add some elements
    for (int i = 0; i < 100; ++i) {
        deq.push_back(i);
    }
    
    EXPECT_EQ(deq.size(), 100);
    
    // Verify contents
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(deq[i], i);
    }
    
    // Check that data is cache-line aligned
    constexpr auto cacheline_size = std::hardware_destructive_interference_size;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&deq[0]) % cacheline_size, 0)
        << "Vector data should be cache-line aligned";
}

// Test with std::list
TEST(AllocatorTest, ListIntegration) {
    using AllocList = std::list<std::string, cache_aligned_alloc<std::string>>;
    
    AllocList lst;
    
    lst.push_back("Hello");
    lst.push_back("World");
    lst.push_back("Cache");
    lst.push_back("Aligned");
    
    EXPECT_EQ(lst.size(), 4);
    
    auto it = lst.begin();
    EXPECT_EQ(*it++, "Hello");
    EXPECT_EQ(*it++, "World");
    EXPECT_EQ(*it++, "Cache");
    EXPECT_EQ(*it++, "Aligned");
}

// Test container copy with propagation
TEST(AllocatorTest, ContainerCopyPropagation) {
    using AllocVector = std::vector<int, cache_aligned_alloc<int>>;
    
    AllocVector vec1;
    for (int i = 0; i < 10; ++i) {
        vec1.push_back(i);
    }
    
    // Copy construction should use select_on_container_copy_construction
    AllocVector vec2 = vec1;
    EXPECT_EQ(vec2.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(vec2[i], i);
    }
    
    // Copy assignment should propagate allocator (since propagate_on_container_copy_assignment = true)
    AllocVector vec3;
    vec3 = vec1;
    EXPECT_EQ(vec3.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(vec3[i], i);
    }
}

// Test container move with propagation
TEST(AllocatorTest, ContainerMovePropagation) {
    using AllocVector = std::vector<int, cache_aligned_alloc<int>>;
    
    AllocVector vec1;
    for (int i = 0; i < 10; ++i) {
        vec1.push_back(i);
    }
    
    // Move construction
    AllocVector vec2 = std::move(vec1);
    EXPECT_EQ(vec2.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(vec2[i], i);
    }
    
    // Move assignment should propagate allocator (since propagate_on_container_move_assignment = true)
    AllocVector vec3;
    vec3.push_back(999);  // Put something in vec3
    vec3 = std::move(vec2);
    EXPECT_EQ(vec3.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(vec3[i], i);
    }
}

// Test container swap with propagation
TEST(AllocatorTest, ContainerSwapPropagation) {
    using AllocVector = std::vector<int, cache_aligned_alloc<int>>;
    
    AllocVector vec1;
    AllocVector vec2;
    
    for (int i = 0; i < 5; ++i) {
        vec1.push_back(i);
    }
    
    for (int i = 10; i < 15; ++i) {
        vec2.push_back(i);
    }
    
    // Swap should work correctly (since propagate_on_container_swap = true)
    vec1.swap(vec2);
    
    EXPECT_EQ(vec1.size(), 5);
    EXPECT_EQ(vec2.size(), 5);
    
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(vec1[i], i + 10);
        EXPECT_EQ(vec2[i], i);
    }
}

// Test zero allocation
TEST(AllocatorTest, ZeroAllocation) {
    cache_aligned_alloc<int> alloc;
    
    // Zero allocation should return nullptr (or valid empty allocation)
    int* ptr = alloc.allocate(0);
    // The behavior is implementation-defined, but deallocate should handle it gracefully
    alloc.deallocate(ptr, 0);
}

// Performance/stress test with cache alignment
TEST(AllocatorTest, CacheAlignmentStressTest) {
    cache_aligned_alloc<int> alloc;
    std::vector<int*> ptrs;
    constexpr auto cacheline_size = std::hardware_destructive_interference_size;
    
    // Allocate many blocks and verify they're all cache-aligned
    for (int i = 0; i < 100; ++i) {
        int* ptr = alloc.allocate(1 + i % 10);  // Allocate 1-10 elements
        ASSERT_NE(ptr, nullptr);
        
        // Verify cache alignment
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % cacheline_size, 0)
            << "Allocation " << i << " should be cache-line aligned";
        
        ptrs.push_back(ptr);
        
        // Write some data
        for (int j = 0; j < (1 + i % 10); ++j) {
            ptr[j] = i * j;
        }
    }
    
    // Deallocate all
    for (size_t i = 0; i < ptrs.size(); ++i) {
        alloc.deallocate(ptrs[i], 1 + i % 10);
    }
}

} // namespace conc::test
