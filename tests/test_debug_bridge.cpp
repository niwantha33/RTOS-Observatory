// SPDX-License-Identifier: Apache-2.0
#include "observatory/debug_bridge.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
using namespace observatory;
namespace fs=std::filesystem;

static std::string encode(const std::string& state="ATTACHED",
                          std::uint64_t seq=1,std::uint64_t stamp=1700000000000ull,
                          const std::string& capability="metadata-only",
                          const std::string& nonce="A1") {
    return "RTOS-OBSERVATORY-DSF\t1\t"+nonce+
        "\tlaunch-1\tdsf-1\t123\t"+std::to_string(seq)+"\t"+
        std::to_string(stamp)+"\t"+state+"\t"+capability+"\n";
}
static BridgeRecord parse(const std::string& data) {
    std::istringstream stream(data);
    return parse_bridge_record(stream);
}
static bool rejects(const std::string& data) {
    try {(void)parse(data);}catch(const Error&){return true;}return false;
}
int main(int argc,char** argv) {
    const auto first=parse(encode());
    assert(first.session_nonce=="A1");
    assert(first.launch_label=="launch-1");
    assert(first.dsf_session_label=="dsf-1");
    assert(first.sequence==1 && first.epoch==123);
    assert(first.state==BridgeState::Attached && first.metadata_only);
    BridgeMonitor monitor;
    auto a=monitor.observe(first,1700000000500ull);
    assert(a.accepted && a.fresh && a.transition_known);
    assert(a.status=="reported-recent");
    const auto same=monitor.observe(first,1700000000600ull);
    assert(same.accepted && same.fresh && !same.transition_known &&
           same.status=="unchanged-recent");
    assert(monitor.last() && monitor.last()->state==BridgeState::Attached);
    auto running=monitor.observe(parse(encode("RUNNING",2,1700000000900ull)),1700000001100ull);
    assert(running.accepted && running.fresh && running.transition_known);
    assert(monitor.last()->state==BridgeState::Running);
    auto stopped=monitor.observe(parse(encode("SUSPENDED",3,1700000002000ull)),1700000002200ull);
    assert(stopped.accepted && stopped.fresh);
    auto terminated=monitor.observe(parse(encode("TERMINATED",4,1700000003000ull)),1700000003100ull);
    assert(terminated.accepted && terminated.transition_known);
    const auto revived=monitor.observe(parse(encode("RUNNING",5,1700000004000ull)),1700000004100ull);
    assert(!revived.accepted && revived.status=="invalid-transition");
    assert(monitor.last()->state==BridgeState::Terminated);
    const auto replay=monitor.observe(parse(encode("SUSPENDED",3,1700000002000ull)),1700000004100ull);
    assert(!replay.accepted && replay.status=="out-of-order");
    const auto foreign=monitor.observe(parse(encode("ATTACHED",1,1700000006000ull,"metadata-only","other")),
                                       1700000006100ull);
    assert(!foreign.accepted && foreign.status=="session-changed");
    monitor.clear();
    const auto new_session=monitor.observe(parse(encode("ATTACHED",1,1700000006000ull,"metadata-only","other")),
                                           1700000006100ull);
    assert(new_session.accepted);

    BridgeMonitor gaps;
    assert(gaps.observe(first,1700000000100ull).accepted);
    const auto gap=gaps.observe(parse(encode("RUNNING",4,1700000003000ull)),1700000003100ull);
    assert(gap.accepted && !gap.transition_known && gap.status=="sequence-gap");
    assert(gaps.last()->sequence==4);
    const auto stale=gaps.observe(parse(encode("SUSPENDED",5,1700000006000ull)),1700000013001ull);
    assert(stale.accepted && !stale.fresh && stale.status=="reported-stale");
    assert(!stale.transition_known); // earlier gap remains unknown, even after new samples
    const auto future=gaps.observe(parse(encode("SUSPENDED",6,1700000020000ull)),1700000015000ull);
    assert(future.accepted && !future.fresh);
    const auto rollback=gaps.observe(parse(encode("RUNNING",7,1700000000001ull)),1700000025000ull);
    assert(!rollback.accepted && rollback.status=="out-of-order");

    BridgeMonitor initial;
    const auto late=initial.observe(parse(encode("RUNNING",5)),1700000000500ull);
    assert(late.accepted && !late.transition_known);
    const auto later=initial.observe(parse(encode("SUSPENDED",6,1700000002000ull)),1700000002100ull);
    assert(later.accepted && !later.transition_known);
    assert(initial.last()->sequence==6);
    BridgeMonitor skipped;
    const auto missed=skipped.observe(parse(encode("ATTACHED",3)),1700000000500ull);
    assert(missed.accepted && !missed.transition_known);

    assert(rejects(""));
    assert(rejects(encode().substr(0,encode().size()-1)));
    assert(rejects(encode()+"MORE\n"));
    assert(rejects("RTOS-OBSERVATORY-DSF\t2\tA1\tlaunch-1\tdsf-1\t123\t1\t1700000000000\tATTACHED\tmetadata-only\n"));
    assert(rejects(encode("RUNNING",1,1700000000000ull,"gdb-commands")));
    assert(rejects(encode("HARD_RESET")));
    assert(rejects(encode("ATTACHED",0)));
    assert(rejects(encode("ATTACHED",1,0)));
    assert(rejects(encode("ATTACHED",1,1700000000000ull,"metadata-only","../../bad")));
    assert(rejects(encode().substr(0,5)+std::string(5000,'x')+encode().substr(5)));
    auto unknown=parse(encode("UNKNOWN"));
    assert(unknown.state==BridgeState::Unknown);

    BridgeMonitor short_age(100);
    assert(short_age.observe(first,1700000000200ull).accepted);
    assert(!short_age.observe(parse(encode("SUSPENDED",2,1700000001000ull)),
                              1700000001400ull).fresh);
    bool threw=false;
    try{BridgeMonitor bad(0);(void)bad;}catch(const Error&){threw=true;}
    assert(threw);

    if(argc>1) {
        const fs::path output=argv[1];
        fs::create_directories(output.parent_path());
        {
            std::ofstream out(output,std::ios::binary|std::ios::trunc);
            out<<encode();out.close();assert(out.good());
        }
        assert(read_bridge_file(output).sequence==1);
        {
            std::ofstream out(output,std::ios::binary|std::ios::trunc);
            out<<encode().substr(0,12);out.close();
        }
        threw=false;
        try{(void)read_bridge_file(output);}catch(const Error&){threw=true;}
        assert(threw);
        fs::remove(output);
    }
    std::cout<<"W14 lifecycle sequence, monotonicity, stale and capability-denial checks passed\n";
}
