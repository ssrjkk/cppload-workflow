// @author ssrjkk | cppload
#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/error.hpp>

namespace cppload::net {

// Runs an asynchronous beast operation to completion and returns its error.
//
// beast::tcp_stream applies expires_after()/expires_at() deadlines only to the
// *asynchronous* algorithms (async_connect, async_write, async_read, ...). The
// synchronous equivalents are thin passthroughs to the underlying socket and
// silently ignore the deadline: a connect to an unreachable host then blocks
// for the OS connect timeout (~20s or more), and a read from a silent peer
// blocks forever. This helper drives the async operation on the supplied
// io_context so the deadline is honored, while keeping the caller's
// synchronous control flow.
//
// The io_context is restarted before each run, so the same context can be
// reused for the connect/write/read phases of a single request.
//
// Example:
//   stream.expires_after(sec);
//   auto ec = run_async(ioc, [&](auto h) { stream.async_connect(results, h); });
template <class Init>
inline boost::beast::error_code run_async(boost::asio::io_context& ioc,
                                          Init&& init) {
    boost::beast::error_code op_ec;
    init([&op_ec](boost::beast::error_code ec, auto&&...) { op_ec = ec; });
    ioc.restart();
    ioc.run();
    return op_ec;
}

} // namespace cppload::net
