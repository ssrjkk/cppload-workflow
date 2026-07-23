#include <gtest/gtest.h>
#include "cppload/core/io_context_pool.hpp"
#include <atomic>
#include <thread>
#include <vector>

TEST(IoContextPoolTest, ConstructDefault) {
    ASSERT_NO_THROW({
        cppload::IoContextPool pool;
    });
}

TEST(IoContextPoolTest, ConstructCustomSize) {
    ASSERT_NO_THROW({
        cppload::IoContextPool pool(4);
    });
}

TEST(IoContextPoolTest, StartAndStop) {
    cppload::IoContextPool pool(2);
    ASSERT_NO_THROW(pool.start());
    ASSERT_NO_THROW(pool.stop());
}

TEST(IoContextPoolTest, DoubleStartIsNoop) {
    cppload::IoContextPool pool(2);
    pool.start();
    ASSERT_NO_THROW(pool.start());
    pool.stop();
}

TEST(IoContextPoolTest, GetContextReturnsValidReference) {
    cppload::IoContextPool pool(2);
    auto& ctx = pool.get_context();
    (void)ctx;
}

TEST(IoContextPoolTest, RoundRobinDistribution) {
    cppload::IoContextPool pool(4);
    std::vector<std::reference_wrapper<boost::asio::io_context>> contexts;
    for (int i = 0; i < 8; ++i) {
        contexts.push_back(std::ref(pool.get_context()));
    }
    EXPECT_EQ(&contexts[0].get(), &contexts[4].get());
    EXPECT_EQ(&contexts[1].get(), &contexts[5].get());
    EXPECT_EQ(&contexts[2].get(), &contexts[6].get());
    EXPECT_EQ(&contexts[3].get(), &contexts[7].get());
}

TEST(IoContextPoolTest, RunWorkOnPool) {
    cppload::IoContextPool pool(2);
    pool.start();

    std::atomic<int> counter{0};
    auto& ctx = pool.get_context();
    boost::asio::post(ctx, [&counter]() { counter.fetch_add(1); });
    boost::asio::post(ctx, [&counter]() { counter.fetch_add(1); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pool.stop();

    EXPECT_EQ(counter.load(), 2);
}

TEST(IoContextPoolTest, ConcurrentGetContext) {
    cppload::IoContextPool pool(4);
    std::atomic<int> call_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&pool, &call_count]() {
            for (int j = 0; j < 100; ++j) {
                pool.get_context();
                call_count.fetch_add(1);
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(call_count.load(), 800);
}
