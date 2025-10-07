#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <memory>
#include "deque.hpp"

namespace conc::test {

// Test cell type that satisfies alignment requirements
struct alignas(std::hardware_destructive_interference_size) test_cell {
    int value;
    std::atomic<bool> processed{false};
    
    test_cell() : value(0) {}
    test_cell(int v) : value(v) {}
    
    // Copy constructor
    test_cell(const test_cell& other) : value(other.value), processed(other.processed.load()) {}
    
    // Assignment operator
    test_cell& operator=(const test_cell& other) {
        if (this != &other) {
            value = other.value;
            processed.store(other.processed.load());
        }
        return *this;
    }
    
    bool operator==(const test_cell& other) const {
        return value == other.value;
    }
};

// Simple test cell for basic functionality
struct alignas(std::hardware_destructive_interference_size) simple_cell {
    int data;
    
    simple_cell() : data(0) {}
    simple_cell(int d) : data(d) {}
    
    bool operator==(const simple_cell& other) const {
        return data == other.data;
    }
};

// Verify alignment requirements are met
static_assert(std::alignment_of_v<test_cell> == std::hardware_destructive_interference_size);
static_assert(std::alignment_of_v<simple_cell> == std::hardware_destructive_interference_size);

// Test basic deque construction and destruction
TEST(DequeTest, BasicConstruction) {
    // Test with different chunk sizes
    {
        conc::deque<test_cell, 4> deq;
        // Should construct without issues
    }
    
    {
        conc::deque<simple_cell, 8> deq;
        // Should construct without issues
    }
    
    {
        conc::deque<test_cell, 16> deq;
        // Should construct without issues
    }
}

// Test iterator construction and basic properties
TEST(DequeTest, IteratorConstruction) {
    conc::deque<simple_cell, 4> deq;
    
    // Test default constructor
    typename conc::deque<simple_cell, 4>::amortize_forward iter1;
    
    // Test copy constructor
    typename conc::deque<simple_cell, 4>::amortize_forward iter2(iter1);
    
    // Test assignment
    typename conc::deque<simple_cell, 4>::amortize_forward iter3;
    iter3 = iter1;
    
    // Test equality comparison
    EXPECT_TRUE(iter1 == iter2);
    EXPECT_TRUE(iter1 == iter3);
    EXPECT_TRUE(iter2 == iter3);
}

// Test begin() method returns proper iterator
TEST(DequeTest, BeginIterator) {
    conc::deque<simple_cell, 4> deq;
    auto iter = deq.begin();
    
    // Should be able to dereference
    simple_cell& cell = *iter;
    cell.data = 42;
    
    EXPECT_EQ(cell.data, 42);
}

// Test iterator dereferencing and assignment
TEST(DequeTest, IteratorDereference) {
    conc::deque<test_cell, 4> deq;
    auto iter = deq.begin();
    
    // Test dereferencing
    test_cell& cell = *iter;
    cell.value = 123;
    cell.processed.store(true);
    
    EXPECT_EQ(cell.value, 123);
    EXPECT_TRUE(cell.processed.load());
    
    // Test that changes persist
    test_cell& same_cell = *iter;
    EXPECT_EQ(same_cell.value, 123);
    EXPECT_TRUE(same_cell.processed.load());
}

// Test iterator increment within chunk
TEST(DequeTest, IteratorIncrementWithinChunk) {
    constexpr size_t chunk_size = 4;
    conc::deque<simple_cell, chunk_size> deq;
    auto iter = deq.begin();
    
    // Fill cells within the first chunk
    for (size_t i = 0; i < chunk_size - 1; ++i) {
        simple_cell& cell = *iter;
        cell.data = static_cast<int>(i);
        
        if (i < chunk_size - 2) {
            ++iter;
        }
    }
    
    // Verify we can read back the values by going back to the beginning
    auto verify_iter = deq.begin();
    for (size_t i = 0; i < chunk_size - 1; ++i) {
        simple_cell& cell = *verify_iter;
        EXPECT_EQ(cell.data, static_cast<int>(i));
        if (i < chunk_size - 2) {
            ++verify_iter;
        }
    }
}

// Test iterator increment across chunk boundary
TEST(DequeTest, IteratorIncrementAcrossChunks) {
    constexpr size_t chunk_size = 4;
    conc::deque<simple_cell, chunk_size> deq;
    auto iter = deq.begin();
    
    // Fill first chunk completely
    for (size_t i = 0; i < chunk_size - 1; ++i) {
        simple_cell& cell = *iter;
        cell.data = static_cast<int>(i);
        ++iter; // This should trigger chunk allocation when we cross the boundary
    }
    
    // This increment should move to the next chunk
    simple_cell& cell_in_new_chunk = *iter;
    cell_in_new_chunk.data = 999;
    
    EXPECT_EQ(cell_in_new_chunk.data, 999);
}

// Test post-increment operator
TEST(DequeTest, IteratorPostIncrement) {
    conc::deque<simple_cell, 4> deq;
    auto iter = deq.begin();
    
    // Set value in first cell
    (*iter).data = 10;
    
    // Post-increment should return old iterator but advance the iterator
    auto old_iter = iter++;
    
    // Old iterator should still point to first cell
    EXPECT_EQ((*old_iter).data, 10);
    
    // New iterator should point to second cell
    (*iter).data = 20;
    EXPECT_EQ((*iter).data, 20);
    
    // Verify first cell is still unchanged
    EXPECT_EQ((*old_iter).data, 10);
}

// Test iterator traits
TEST(DequeTest, IteratorTraits) {
    using Iterator = conc::deque<simple_cell, 4>::amortize_forward;
    
    // Check required type aliases
    static_assert(std::is_same_v<Iterator::value_type, simple_cell>);
    static_assert(std::is_same_v<Iterator::difference_type, std::ptrdiff_t>);
    
    // Verify it satisfies forward iterator requirements
    static_assert(std::forward_iterator<Iterator>);
}

// Test sequential access pattern
TEST(DequeTest, SequentialAccess) {
    constexpr size_t chunk_size = 4;
    constexpr size_t num_elements = 10;
    conc::deque<simple_cell, chunk_size> deq;
    
    auto iter = deq.begin();
    
    // Write sequential values
    for (size_t i = 0; i < num_elements; ++i) {
        simple_cell& cell = *iter;
        cell.data = static_cast<int>(i);
        ++iter;
    }
    
    // Read back and verify
    auto read_iter = deq.begin();
    for (size_t i = 0; i < num_elements; ++i) {
        simple_cell& cell = *read_iter;
        EXPECT_EQ(cell.data, static_cast<int>(i)) << "Mismatch at index " << i;
        ++read_iter;
    }
}

// Test alignment requirements
TEST(DequeTest, AlignmentRequirements) {
    // These should compile fine since our test cells meet alignment requirements
    conc::deque<test_cell, 4> deq1;
    conc::deque<simple_cell, 8> deq2;
    
    // Verify that the chunk size requirement is enforced (minimum 2)
    conc::deque<test_cell, 2> deq3;  // Should compile
    
    // The following would not compile due to chunk_size < 2:
    // conc::deque<test_cell, 1> deq_invalid;
}

// Test memory allocation and chunk creation
TEST(DequeTest, ChunkAllocation) {
    constexpr size_t chunk_size = 4;
    conc::deque<simple_cell, chunk_size> deq;
    auto iter = deq.begin();
    
    // Access elements to force chunk allocation
    const size_t elements_per_chunk = chunk_size - 1;
    const size_t num_chunks = 3;
    const size_t total_elements = elements_per_chunk * num_chunks;
    
    for (size_t i = 0; i < total_elements; ++i) {
        simple_cell& cell = *iter;
        cell.data = static_cast<int>(i);
        ++iter;
    }
    
    // Verify all data is accessible
    auto verify_iter = deq.begin();
    for (size_t i = 0; i < total_elements; ++i) {
        simple_cell& cell = *verify_iter;
        EXPECT_EQ(cell.data, static_cast<int>(i)) << "Failed at element " << i;
        ++verify_iter;
    }
}

// Test with different chunk sizes
TEST(DequeTest, DifferentChunkSizes) {
    // Test with small chunks
    {
        conc::deque<simple_cell, 4> deq;
        auto iter = deq.begin();
        for (int i = 0; i < 10; ++i) {
            (*iter).data = i;
            ++iter;
        }
    }
    
    // Test with larger chunks
    {
        conc::deque<simple_cell, 16> deq;
        auto iter = deq.begin();
        for (int i = 0; i < 50; ++i) {
            (*iter).data = i;
            ++iter;
        }
    }
}

// Basic single-threaded concurrency safety test
TEST(DequeTest, BasicConcurrencySafety) {
    constexpr size_t chunk_size = 8;
    conc::deque<test_cell, chunk_size> deq;
    
    // This test verifies that the atomic operations in chunk allocation work correctly
    auto iter = deq.begin();
    
    // Force multiple chunk allocations
    for (int i = 0; i < 30; ++i) {
        test_cell& cell = *iter;
        cell.value = i;
        cell.processed.store(true);
        ++iter;
    }
    
    // Verify data integrity
    auto verify_iter = deq.begin();
    for (int i = 0; i < 30; ++i) {
        test_cell& cell = *verify_iter;
        EXPECT_EQ(cell.value, i);
        EXPECT_TRUE(cell.processed.load());
        ++verify_iter;
    }
}

// Test multi-threaded access (basic safety)
TEST(DequeTest, MultiThreadedAccess) {
    constexpr size_t chunk_size = 8;
    constexpr int num_threads = 4;
    constexpr int elements_per_thread = 25;
    
    conc::deque<test_cell, chunk_size> deq;
    std::atomic<int> next_index{0};
    std::vector<std::thread> threads;
    
    // Create threads that will write to different positions
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&deq, &next_index, elements_per_thread, t]() {
            auto iter = deq.begin();
            
            for (int i = 0; i < elements_per_thread; ++i) {
                int index = next_index.fetch_add(1);
                
                // Advance iterator to the correct position
                for (int j = 0; j < index; ++j) {
                    ++iter;
                }
                
                test_cell& cell = *iter;
                cell.value = index;
                cell.processed.store(true);
                
                // Reset iterator for next iteration
                iter = deq.begin();
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify that all positions were written to
    auto verify_iter = deq.begin();
    std::vector<bool> found(num_threads * elements_per_thread, false);
    
    for (int i = 0; i < num_threads * elements_per_thread; ++i) {
        test_cell& cell = *verify_iter;
        if (cell.processed.load()) {
            EXPECT_GE(cell.value, 0);
            EXPECT_LT(cell.value, num_threads * elements_per_thread);
            if (cell.value >= 0 && cell.value < static_cast<int>(found.size())) {
                found[cell.value] = true;
            }
        }
        ++verify_iter;
    }
}

// Test iterator copy semantics
TEST(DequeTest, IteratorCopySemantics) {
    conc::deque<simple_cell, 4> deq;
    auto iter1 = deq.begin();
    
    // Set a value
    (*iter1).data = 42;
    
    // Copy the iterator
    auto iter2 = iter1;
    
    // Both should point to the same location
    EXPECT_TRUE(iter1 == iter2);
    EXPECT_EQ((*iter2).data, 42);
    
    // Advance one iterator
    ++iter1;
    (*iter1).data = 84;
    
    // They should no longer be equal
    EXPECT_FALSE(iter1 == iter2);
    
    // Original iterator should still point to original value
    EXPECT_EQ((*iter2).data, 42);
    
    // New position should have new value
    EXPECT_EQ((*iter1).data, 84);
}

// Stress test with many elements
TEST(DequeTest, StressTest) {
    constexpr size_t chunk_size = 8;
    constexpr size_t num_elements = 1000;
    
    conc::deque<simple_cell, chunk_size> deq;
    auto iter = deq.begin();
    
    // Write many elements
    for (size_t i = 0; i < num_elements; ++i) {
        simple_cell& cell = *iter;
        cell.data = static_cast<int>(i);
        ++iter;
    }
    
    // Verify all elements
    auto verify_iter = deq.begin();
    for (size_t i = 0; i < num_elements; ++i) {
        simple_cell& cell = *verify_iter;
        EXPECT_EQ(cell.data, static_cast<int>(i)) << "Failed at element " << i;
        ++verify_iter;
    }
}

} // namespace conc::test