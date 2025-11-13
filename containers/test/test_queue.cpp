#include <gtest/gtest.h>
#include "queue.hpp"

using namespace conc;

class QueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code for each test
    }

    void TearDown() override {
        // Cleanup code for each test
    }
};

// Basic functionality tests
TEST_F(QueueTest, Playground) {
    queue s;

    // Very rapid operations to trigger race condition
    std::vector<std::thread> threads;
    const int num_threads = 8;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, i]() {
            for (int j = 0; j < 1000000; ++j) {
                s.push(i * 1000 + j);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
}

