// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/session.hpp"
#include <asio.hpp>
#include <array>
#include <chrono>
#include <functional>
#include <memory>
namespace observatory {
struct TcpCaptureOptions {
    std::string address = "127.0.0.1"; // Numeric address; no DNS lookup.
    unsigned short port{};
    Limits limits{};
    std::function<void(const StreamProgress&)> progress; // Optional; runs on session strand.
    std::chrono::milliseconds idle_timeout{5000};
    std::chrono::milliseconds total_timeout{30000};
};
struct CaptureResult {
    std::string raw;
    std::string error;
    bool clean_eof = false;
};
// One finite stream per object. All mutable state is confined to its strand.
// Completion executes once on that strand, after the socket has been closed.
class TcpCapture : public std::enable_shared_from_this<TcpCapture> {
public:
    using Completion = std::function<void(CaptureResult)>;
    static std::shared_ptr<TcpCapture> create(asio::io_context&, TcpCaptureOptions, Completion);
    void start(); // Thread-safe, idempotent. Call on a shared-owned instance.
    void stop();  // Thread-safe cancellation; does not publish a successful session.
private:
    TcpCapture(asio::io_context&, TcpCaptureOptions, Completion);
    void begin();
    void read();
    void arm_idle();
    void finish(std::string error, bool eof = false);
    asio::strand<asio::io_context::executor_type> strand_;
    asio::ip::tcp::socket socket_;
    asio::steady_timer idle_, deadline_;
    TcpCaptureOptions options_;
    Completion completion_;
    std::array<char,4096> buffer_{};
    std::string raw_;
    UniversalStream decoder_;
    std::uint64_t idle_generation_{};
    bool started_{}, done_{};
};
} // namespace observatory
