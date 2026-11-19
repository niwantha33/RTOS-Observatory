// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/engine.hpp"
#include "obs_wire_v1.h"
#include <asio.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace observatory {
// Finite, explicitly owned W17 binary TCP receiving endpoint.
// This uses a NEW ordinary TCP socket ONLY, never GDB/OpenOCD/RTT.
struct FramedTcpOptions {
    std::string address{"127.0.0.1"}; // numeric IP only, no DNS
    unsigned short port{};
    std::size_t max_bytes{1024u*1024u};
    std::size_t max_frames{100000u};
    std::chrono::milliseconds idle_timeout{5000};
    std::chrono::milliseconds total_timeout{30000};
    // Optional strand-serialized exact raw bytes, e.g. user-owned local spool.
    // Throws on sink I/O failure; a capture can no longer claim clean success.
    std::function<void(const std::uint8_t*,std::size_t)> on_bytes;
};
struct FramedTcpResult {
    std::size_t bytes{};
    std::size_t frames{};
    std::size_t events{};
    std::size_t stats{};
    std::uint32_t crc_errors{};
    std::uint32_t invalid_frames{};
    std::uint32_t replayed_frames{};
    std::uint32_t missing_frames{};
    std::uint32_t missing_attempts{};
    std::uint32_t epoch_changes{};
    bool loss_saturated{};
    bool clean_eof{};
    std::string error;
};
class FramedTcpCapture final : public std::enable_shared_from_this<FramedTcpCapture> {
public:
    using Completion=std::function<void(FramedTcpResult)>;
    static std::shared_ptr<FramedTcpCapture> create(
        asio::io_context&,FramedTcpOptions,Completion);
    void start(); // strand serialized, idempotent
    void stop();  // cancels; never reports clean success
private:
    FramedTcpCapture(asio::io_context&,FramedTcpOptions,Completion);
    void begin();
    void read();
    void arm_idle();
    void finish(std::string error,bool eof=false);
    asio::strand<asio::io_context::executor_type> strand_;
    asio::ip::tcp::socket socket_;
    asio::steady_timer idle_,deadline_;
    FramedTcpOptions options_;
    Completion complete_;
    std::array<std::uint8_t,2048> buffer_{};
    obs_wire_decoder_t decoder_{};
    FramedTcpResult result_;
    std::uint64_t idle_generation_{};
    bool started_{},done_{};
};
} // namespace observatory
