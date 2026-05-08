#include <gtest/gtest.h>
#include "cppload/core/token_bucket.hpp"
#include <thread>

TEST(TokenBucketTest, ConsumeBlocksAtRate) {
    cppload::TokenBucket bucket(100.0); // 100 RPS
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) {
        bucket.consume();
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    // 10 tokens at 100 RPS = ~100ms (10 * 10ms = 100ms)
    EXPECT_GE(elapsed.count(), 50); // Allow some tolerance
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
