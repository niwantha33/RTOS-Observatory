// SPDX-License-Identifier: Apache-2.0
#include "observatory/live_session.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>
using namespace observatory;

static std::vector<std::uint8_t> event_frame(std::uint32_t epoch,
                                             std::uint32_t frame,
                                             std::uint32_t attempt,
                                             std::uint64_t tick,
                                             std::uint16_t lane=0u) {
    obs_recorder_record_t record{};
    record.sequence=attempt;record.lane=lane;
    record.schema_version=OBS_RECORDER_SCHEMA_VERSION;
    record.event.timestamp=tick;
    record.event.task_id=6u;
    record.event.event_type=42u;
    std::vector<std::uint8_t> bytes(OBS_WIRE_MAX_FRAME);
    const auto n=obs_wire_pack_event(bytes.data(),epoch,frame,&record);
    assert(n==48u);
    bytes.resize(n);
    return bytes;
}
static void feed_chunks(LiveSession& session,const std::vector<std::uint8_t>& bytes,
                        std::size_t chunk) {
    for(std::size_t i=0;i<bytes.size();i+=chunk)
        session.feed(bytes.data()+i,std::min(chunk,bytes.size()-i));
}
static bool refuses(auto&& call) {
    try{call();}catch(const Error&){return true;}
    return false;
}
int main(int argc,char** argv) {
    LiveSession session(LiveLimits{2048,100,3,3});
    auto a=event_frame(1,1,1,100);
    auto b=event_frame(1,2,2,200);
    feed_chunks(session,a,1);
    assert(session.epochs().size()==1 && session.epochs()[0].id==1);
    assert(session.recent().size()==1 && session.status().accepted_frames==1);
    assert(session.status().total_events==1 && !session.status().incomplete);
    assert(!session.tick_us(1,100)); // never fabricate calibration
    assert(!session.set_clock({2,100,0,1000}));
    assert(session.set_clock({1,100,10000,1000}));
    assert(session.tick_us(1,100)==10000u);
    assert(session.tick_us(1,200)==110000u);
    assert(!session.tick_us(1,99));
    assert(!session.set_clock({1,100,0,0}));
    assert(!session.set_clock({1,100,0,1000000001u}));
    assert(session.set_clock({1,100,15000,1000000}));
    assert(session.tick_us(1,200)==15100u);
    assert(session.tick_us(1,100)==15000u);

    feed_chunks(session,b,7);
    assert(session.epochs()[0].events==2);
    assert(session.recent()[1].event.sequence==2u);

    // A new explicit epoch cannot be treated as contiguous time.
    const auto c=event_frame(2,1,1,10);
    feed_chunks(session,c,48);
    assert(session.epochs().size()==2 && session.epochs()[1].id==2);
    assert(session.wire().epoch_changes==1);
    assert(session.status().incomplete);
    assert(!session.tick_us(2,10));
    assert(session.tick_us(1,200)==15100u);

    // A later gap, history cap and lost records make evidence incomplete.
    const auto d=event_frame(2,4,4,40);
    feed_chunks(session,d,11);
    assert(session.wire().missing_frames==2);
    assert(session.wire().missing_attempts==2);
    assert(session.epochs()[1].gap_seen);
    assert(session.status().history_pruned==1);
    assert(session.recent().size()==3);
    assert(session.recent().front().epoch==1);
    assert(session.recent().back().gap_before);
    assert(!session.status().waiting_for_frame);
    session.finish();

    // Stats are retained without synthesizing scheduler/CPU measurements.
    obs_wire_stats_t stats{};
    stats.lane=3u;
    stats.counters.attempted=40u;
    stats.counters.full_drops=4u;
    std::uint8_t packed[OBS_WIRE_MAX_FRAME]{};
    assert(obs_wire_pack_stats(packed,2,5,&stats)==44u);
    session.feed(packed,44);
    assert(session.status().total_stats==1);
    assert(session.lane_stats()[3].has_value());
    assert(session.lane_stats()[3]->counters.full_drops==4u);

    // A fragmented final record is not claimed complete.
    LiveSession partial;
    partial.feed(a.data(),a.size()-1);
    assert(partial.status().waiting_for_frame);
    assert(refuses([&] {partial.finish();}));
    assert(partial.status().poisoned);
    assert(refuses([&] {partial.feed(a.data(),1);}));

    LiveSession byte_limit(LiveLimits{48,2,2,2});
    byte_limit.feed(a.data(),a.size());
    assert(refuses([&] {byte_limit.feed(b.data(),b.size());}));
    assert(byte_limit.status().poisoned);

    LiveSession frame_limit(LiveLimits{1024,1,2,2});
    frame_limit.feed(a.data(),a.size());
    assert(refuses([&] {frame_limit.feed(b.data(),b.size());}));
    assert(frame_limit.status().poisoned);

    LiveSession epoch_limit(LiveLimits{1024,10,2,1});
    epoch_limit.feed(a.data(),a.size());
    assert(refuses([&] {epoch_limit.feed(c.data(),c.size());}));
    assert(epoch_limit.status().poisoned);

    LiveSession delayed(LiveLimits{1024,10,3,3});
    assert(refuses([&] {delayed.finish();}));
    assert(refuses([] {LiveSession bad(LiveLimits{0,1,1,1});}));
    assert(refuses([] {LiveSession bad(LiveLimits{1,0,1,1});}));
    assert(refuses([] {LiveSession bad(LiveLimits{1,1,0,1});}));
    assert(refuses([] {LiveSession bad(LiveLimits{1,1,1,0});}));
    assert(refuses([&] {session.feed(nullptr,1);}));
    // Reproducible, synthetic binary GUI/file-follow fixture. No hardware.
    if(argc>1) {
        const std::filesystem::path file(argv[1]);
        std::ofstream out(file,std::ios::binary|std::ios::trunc);
        assert(out.is_open());
        const auto x=event_frame(1,1,1,100);
        const auto y=event_frame(1,2,2,200);
        const auto z=event_frame(2,1,1,15);
        for(const auto& bytes:{x,y,z})
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        out.close();
        assert(out.good());
    }
    std::cout<<"W18 epoch isolation, bounded incremental history, loss and explicit clock mapping passed\n";
}
