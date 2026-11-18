// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/engine.hpp"
#include <filesystem>
#include <functional>
#include <memory>
namespace observatory {
struct Limits {
    std::size_t max_bytes = 64 * 1024 * 1024;
    std::size_t max_events = 1000000;
    std::size_t max_line_bytes = 4096;
};
struct Identity {
    std::string firmware;
    std::string rtos;
    std::string clock;
    std::uint64_t ticks_per_second{};
};
struct Evidence {
    std::uint64_t first_sequence{}, last_sequence{}, missing_events{}, declared_drops{};
    bool sequence_available = false;
};
struct DecodedTrace {
    Capture capture;
    Identity identity;
    Evidence evidence;
    std::string decoder;
};
struct StreamProgress {
    std::size_t bytes{}, events{}, pending_line_bytes{};
    std::uint64_t last_timestamp_us{}, missing_events{}, declared_drops{};
    bool header_valid = false;
    bool finalized = false;
};
// Single-owner incremental universal-v1 parser, independent of Asio.
// Any error poisons the instance. No recovery or reuse across reset epochs.
class UniversalStream {
public:
    explicit UniversalStream(Limits limits = {});
    void feed(std::string_view bytes);
    DecodedTrace finish(bool require_final_newline = false);
    StreamProgress progress() const;
private:
    void row();
    Limits limits_;
    DecodedTrace trace_;
    std::string pending_;
    std::size_t bytes_{}, line_{};
    bool header_{}, failed_{}, finalized_{};
};
class Decoder {
public:
    virtual ~Decoder() = default;
    virtual DecodedTrace decode(std::istream&, const Limits&) const = 0;
};
class DecoderRegistry {
    std::map<std::string, std::shared_ptr<const Decoder>> decoders_;
public:
    void add(std::string id, std::shared_ptr<const Decoder> decoder);
    DecodedTrace decode(const std::string& id, std::istream&, const Limits& = {}) const;
};
DecoderRegistry default_decoders();
// Immutable directory: raw.trace, metadata.tsv, index.tsv. Never overwrites.
void store_session(const std::filesystem::path& directory, const std::string& decoder,
                   std::istream& raw, const Limits& = {});
DecodedTrace replay_session(const std::filesystem::path& directory, const Limits& = {});
} // namespace observatory
