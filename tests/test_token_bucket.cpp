#include <gtest/gtest.h>
#include "cppload/core/token_bucket.hpp"
#include <thread>

TEST(TokenBucketTest, ConsumeBlocksAtRate) {
    cppload::TokenBucket bucket(100.0, 1.0); // 100 RPS, burst=1
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) {
        bucket.consume();
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    // 10 tokens at 100 RPS, burst=1 = ~90ms (9 intervals * 10ms)
    EXPECT_GE(elapsed.count(), 50);
}

TEST(TokenBucketTest, TryConsumeNonBlocking) {
    cppload::TokenBucket bucket(1000.0);
    // First consume should succeed immediately (tokens available)
    EXPECT_TRUE(bucket.try_consume());
}

TEST(TokenBucketTest, BurstCapacity) {
    // Burst of 200, rate of 100
    cppload::TokenBucket bucket(100.0, 200.0);
    for (int i = 0; i < 200; ++i) {
        EXPECT_TRUE(bucket.try_consume());
    }
    // Should be empty now
    EXPECT_FALSE(bucket.try_consume());
}

TEST(TokenBucketTest, SetRate) {
    cppload::TokenBucket bucket(10.0); // slow
    bucket.set_rate(1000.0); // fast
    EXPECT_TRUE(bucket.try_consume());
}

TEST(TokenBucketTest, SetBurst) {
    cppload::TokenBucket bucket(100.0, 1.0); // burst of 1
    EXPECT_TRUE(bucket.try_consume());
    EXPECT_FALSE(bucket.try_consume()); // empty
    bucket.set_burst(100.0);
    // After set_burst, refill happens on next consume
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(bucket.try_consume());
}

TEST(TokenBucketTest, ConcurrentConsume) {
    cppload::TokenBucket bucket(5000.0); // high rate
    std::atomic<int> consumed{0};
    const int num_threads = 4;
    const int per_thread = 100;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < per_thread; ++i) {
                if (bucket.try_consume()) {
                    consumed.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    // Should have consumed tokens from the bucket
    EXPECT_GT(consumed.load(), 0);
    EXPECT_LE(consumed.load(), num_threads * per_thread);
}

TEST(TokenBucketTest, InvalidRate) {
    EXPECT_THROW(cppload::TokenBucket(0.0), std::invalid_argument);
    EXPECT_THROW(cppload::TokenBucket(-1.0), std::invalid_argument);
    cppload::TokenBucket bucket(100.0);
    EXPECT_THROW(bucket.set_rate(0.0), std::invalid_argument);
    EXPECT_THROW(bucket.set_rate(-5.0), std::invalid_argument);
}
