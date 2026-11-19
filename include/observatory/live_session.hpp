// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/engine.hpp"
#include "obs_wire_v1.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace observatory {

// W18: one publisher/one reader. Incremental W17 framing and *observed*
// chronology only. Numeric W16 event IDs are NOT interpreted as task/ISR
// switch semantics. CPU percentages, waits and global time are UNKNOWN.
struct LivePoint {
    std::uint32_t epoch{};
    std::uint32_t frame_sequence{};
    obs_recorder_record_t event{};
    bool gap_before{};        // W17 frame gap, not necessarily local event gap
};
struct LiveEpoch {
    std::uint32_t id{};
    std::uint64_t frames{},events{},stats{};
    bool gap_seen{};
    bool bad_evidence{};      // corruption, replay etc. can straddle epoch
    std::optional<std::uint64_t> first_tick,last_tick;
};
struct LiveClockMapping {
    std::uint32_t epoch{};
    std::uint64_t origin_tick{};
    std::uint64_t origin_us{};
    std::uint32_t ticks_per_second{};
    // User-supplied calibration only, never attested to a physical clock.
};
struct LiveStatus {
    std::uint64_t consumed_bytes{};
    std::uint64_t accepted_frames{};
    std::uint64_t total_events{};
    std::uint64_t total_stats{};
    std::uint64_t history_pruned{};
    bool waiting_for_frame{};
    bool incomplete{};
    bool poisoned{};
    std::string error;
};
struct LiveLimits {
    std::uint64_t max_input_bytes{64u*1024u*1024u};
    std::uint64_t max_frames{1000000u};
    std::size_t max_recent_events{4096u};
    std::size_t max_epochs{16u};
};
class LiveSession final {
    LiveLimits limits_;
    obs_wire_decoder_t decoder_{};
    LiveStatus status_;
    std::vector<LiveEpoch> epochs_;
    std::deque<LivePoint> recent_;
    std::array<std::optional<obs_wire_stats_t>,OBS_WIRE_MAX_LANES> stats_{};
    std::vector<LiveClockMapping> clocks_;
    void poison(const std::string& error) {
        status_.poisoned=true;status_.incomplete=true;status_.error=error;
        throw Error(error);
    }
public:
    explicit LiveSession(LiveLimits limits={}) : limits_(limits) {
        if(!limits_.max_input_bytes || !limits_.max_frames ||
           !limits_.max_recent_events || !limits_.max_epochs ||
           limits_.max_recent_events>1000000u || limits_.max_epochs>256u)
            throw Error("W18 invalid live session limits");
        obs_wire_decoder_init(&decoder_);
    }
    const LiveStatus& status() const noexcept {return status_;}
    const std::vector<LiveEpoch>& epochs() const noexcept {return epochs_;}
    const std::deque<LivePoint>& recent() const noexcept {return recent_;}
    const obs_wire_decoder_t& wire() const noexcept {return decoder_;}
    const std::array<std::optional<obs_wire_stats_t>,OBS_WIRE_MAX_LANES>&
        lane_stats() const noexcept {return stats_;}
    void feed(const std::uint8_t* bytes,std::size_t length) {
        if(status_.poisoned)throw Error("W18 poisoned live session");
        if(!length)return;
        if(!bytes)poison("W18 null input");
        if(length>limits_.max_input_bytes-status_.consumed_bytes)
            poison("W18 input byte budget exhausted; capture incomplete");
        for(std::size_t i=0;i<length;++i) {
            obs_wire_frame_t frame{};
            const auto outcome=obs_wire_decoder_push(&decoder_,bytes[i],&frame);
            ++status_.consumed_bytes;
            if(outcome!=OBS_WIRE_FRAME_READY)continue;
            if(status_.accepted_frames>=limits_.max_frames)
                poison("W18 frame budget exhausted; capture incomplete");
            if(frame.new_epoch) {
                if(epochs_.size()>=limits_.max_epochs)
                    poison("W18 epoch budget exhausted; previous epochs retained");
                epochs_.push_back({frame.epoch});
            }
            if(epochs_.empty() || epochs_.back().id!=frame.epoch)
                poison("W18 untracked session epoch");
            LiveEpoch& epoch=epochs_.back();
            ++epoch.frames; ++status_.accepted_frames;
            epoch.gap_seen |= frame.gap_before;
            if(frame.kind==OBS_WIRE_EVENT) {
                ++epoch.events; ++status_.total_events;
                const auto tick=frame.event.event.timestamp;
                if(!epoch.first_tick)epoch.first_tick=tick;
                epoch.last_tick=tick; // per-lane clocks may not be ordered
                if(recent_.size()>=limits_.max_recent_events) {
                    recent_.pop_front();
                    ++status_.history_pruned;
                }
                recent_.push_back({frame.epoch,frame.sequence,frame.event,
                                   frame.gap_before});
            } else if(frame.kind==OBS_WIRE_STATS) {
                ++epoch.stats; ++status_.total_stats;
                stats_[frame.stats.lane]=frame.stats;
            }
        }
        // Any frame corruption, late join, epoch change or missing/loss
        // MUST make confidence incomplete. No '100% complete' inference.
        status_.waiting_for_frame=decoder_.used!=0u;
        status_.incomplete=status_.incomplete || decoder_.crc_errors!=0u ||
            decoder_.invalid_frames!=0u || decoder_.replayed_frames!=0u ||
            decoder_.missing_frames!=0u || decoder_.missing_attempts!=0u ||
            decoder_.epoch_changes!=0u || decoder_.loss_saturated ||
            status_.history_pruned!=0u;
        if(!epochs_.empty() && status_.incomplete)
            epochs_.back().bad_evidence=true;
    }
    void feed(std::string_view bytes) {
        feed(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size());
    }
    // This marks a finite transport EOF; does not fabricate a trailing event.
    // A clean EOF cannot establish that no tail frames were dropped.
    void finish() {
        if(status_.poisoned)throw Error("W18 poisoned live session");
        if(decoder_.used!=0u)poison("W18 truncated binary frame at EOF");
        if(epochs_.empty())poison("W18 no valid framed observations");
    }
    bool set_clock(LiveClockMapping mapping) {
        if(!mapping.epoch || !mapping.ticks_per_second ||
           mapping.ticks_per_second>1000000000u)return false;
        bool found=false;
        for(const auto& e:epochs_)
            if(e.id==mapping.epoch){found=true;break;}
        if(!found)return false; // no fabricated unobserved session
        for(auto& clock:clocks_) {
            if(clock.epoch==mapping.epoch) {clock=mapping;return true;}
        }
        clocks_.push_back(mapping);
        return true;
    }
    // Integer arithmetic for exact positive forward mapping, with overflow
    // protection. Backward/unknown time returns nullopt, never zero/idle.
    std::optional<std::uint64_t> tick_us(std::uint32_t epoch,
                                          std::uint64_t tick) const {
        for(const auto& clock:clocks_) {
            if(clock.epoch!=epoch)continue;
            if(tick<clock.origin_tick)return std::nullopt;
            const auto delta=tick-clock.origin_tick;
            const auto quotient=delta/clock.ticks_per_second;
            const auto rem=delta%clock.ticks_per_second;
            if(quotient>(std::numeric_limits<std::uint64_t>::max()-
                        clock.origin_us)/1000000u)return std::nullopt;
            const auto whole=quotient*1000000u+clock.origin_us;
            // remainder<1e9, so product<1e15 fits in uint64_t.
            const auto fractional=(rem*1000000u)/clock.ticks_per_second;
            if(fractional>std::numeric_limits<std::uint64_t>::max()-whole)
                return std::nullopt;
            return whole+fractional;
        }
        return std::nullopt;
    }
};
} // namespace observatory
