#pragma once

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace cppload {

class IoContextPool {
public:
    explicit IoContextPool(std::size_t pool_size = 0);
    ~IoContextPool();

    IoContextPool(const IoContextPool&) = delete;
    IoContextPool& operator=(const IoContextPool&) = delete;

    void start();
    void stop();

    boost::asio::io_context& get_context();

private:
    using IoContext = boost::asio::io_context;

    std::vector<std::unique_ptr<IoContext>> contexts_;
    std::vector<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>> work_guards_;
    std::vector<std::thread> threads_;
    std::atomic<std::size_t> next_{0};
    bool started_{false};
};

} // namespace cppload
