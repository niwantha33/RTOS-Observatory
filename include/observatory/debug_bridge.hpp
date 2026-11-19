// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/engine.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace observatory {

// W14: PASSIVE metadata ONLY, published by Eclipse's existing DSF owner.
// This library NEVER creates GDB/OpenOCD sockets, issues commands, reads
// target memory/registers, alters target state or authenticates a debugger.
enum class BridgeState {
    Attached, Running, Suspended, Disconnected, Terminated, Unknown
};
inline const char* bridge_state_name(BridgeState s) {
    switch(s) {
        case BridgeState::Attached: return "ATTACHED";
        case BridgeState::Running: return "RUNNING";
        case BridgeState::Suspended: return "SUSPENDED";
        case BridgeState::Disconnected: return "DISCONNECTED";
        case BridgeState::Terminated: return "TERMINATED";
        case BridgeState::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}
struct BridgeRecord {
    std::string session_nonce;      // opaque ID; not a shared secret
    std::string launch_label;       // reporter-supplied, not authenticated
    std::string dsf_session_label;  // reporter-supplied, not authenticated
    std::uint64_t epoch{};
    std::uint64_t sequence{};
    std::uint64_t writer_unix_ms{};
    BridgeState state{BridgeState::Unknown};
    bool metadata_only{true};
};
struct BridgeObservation {
    BridgeRecord record;
    bool accepted{false};
    bool fresh{false};
    bool transition_known{false};
    std::string status; // neutral: "observed", "stale", "out-of-order", etc.
    std::vector<std::string> warnings;
};

namespace bridge_detail {
inline std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t from=0;
    do {
        const auto end=line.find('\t',from);
        fields.push_back(line.substr(from,end==std::string::npos?end:end-from));
        if(end==std::string::npos)break;
        from=end+1;
    } while(true);
    return fields;
}
inline void token(const std::string& t) {
    if(t.empty() || t.size()>128)throw Error("Invalid W14 bridge metadata token");
    for(const unsigned char c:t)
        if(c<33 || c>126 || c=='\\' || c=='"' || c=='/')
            throw Error("Unsafe W14 bridge metadata token");
}
inline std::uint64_t number(const std::string& s) {
    std::uint64_t n{};
    const auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(s.empty() || ec!=std::errc{} || end!=s.data()+s.size())
        throw Error("Invalid W14 bridge unsigned integer");
    return n;
}
inline BridgeState state(const std::string& s) {
    if(s=="ATTACHED")return BridgeState::Attached;
    if(s=="RUNNING")return BridgeState::Running;
    if(s=="SUSPENDED")return BridgeState::Suspended;
    if(s=="DISCONNECTED")return BridgeState::Disconnected;
    if(s=="TERMINATED")return BridgeState::Terminated;
    if(s=="UNKNOWN")return BridgeState::Unknown;
    throw Error("Unsupported W14 bridge state");
}
inline bool transition(BridgeState before,BridgeState after) {
    if(before==BridgeState::Terminated || before==BridgeState::Disconnected)
        return false; // same session must not resurrect
    if(after==BridgeState::Attached)
        return before==BridgeState::Attached || before==BridgeState::Unknown;
    if(after==BridgeState::Running || after==BridgeState::Suspended ||
       after==BridgeState::Unknown || after==BridgeState::Terminated ||
       after==BridgeState::Disconnected)return true;
    return false;
}
} // namespace bridge_detail

// Strict one-record, one-line publication. Partial writes are rejected.
inline BridgeRecord parse_bridge_record(std::istream& input) {
    std::string line;
    line.reserve(512);
    char ch=0;
    std::size_t bytes=0;
    while(input.get(ch)) {
        if(++bytes>4096)throw Error("W14 metadata file exceeds 4096-byte cap");
        if(ch=='\n')break;
        if(ch=='\r' || ch=='\0')throw Error("W14 metadata contains invalid control");
        line+=ch;
    }
    if(!input && !input.eof())throw Error("W14 metadata input read error");
    if(ch!='\n')throw Error("W14 incomplete metadata publication");
    // Exactly one record terminated by LF. Any trailing bytes are rejected.
    if(input.get(ch))throw Error("W14 metadata contains extra records");
    if(!input.eof())throw Error("W14 metadata read error");
    const auto f=bridge_detail::split(line);
    // marker, version, nonce, launch label, DSF session label, epoch,
    // sequence, writer time (unix milliseconds), state, capability
    if(f.size()!=10 || f[0]!="RTOS-OBSERVATORY-DSF" || f[1]!="1")
        throw Error("W14 unsupported bridge publication format");
    bridge_detail::token(f[2]);
    bridge_detail::token(f[3]);
    bridge_detail::token(f[4]);
    if(f[9]!="metadata-only")
        throw Error("W14 bridge MUST NOT request debugger commands");
    BridgeRecord r;
    r.session_nonce=f[2];r.launch_label=f[3];r.dsf_session_label=f[4];
    r.epoch=bridge_detail::number(f[5]);
    r.sequence=bridge_detail::number(f[6]);
    r.writer_unix_ms=bridge_detail::number(f[7]);
    r.state=bridge_detail::state(f[8]);
    if(!r.epoch || !r.sequence || !r.writer_unix_ms)
        throw Error("W14 bridge epoch, sequence and time must be positive");
    return r;
}
inline BridgeRecord read_bridge_file(const std::filesystem::path& path) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        throw Error("W14 status file must be a regular non-symlink file");
    if(std::filesystem::file_size(path)>4096)
        throw Error("W14 bridge file exceeds byte limit");
    std::ifstream input(path,std::ios::binary);
    if(!input)throw Error("Cannot read W14 bridge status");
    return parse_bridge_record(input);
}

// In-memory acceptance of successive manually refreshed status publications.
// A fresh record only means a recent reporter publication, NOT a live
// target-state verification, coherent snapshot or permission to query GDB.
class BridgeMonitor {
    std::optional<BridgeRecord> last_;
    std::uint64_t freshness_ms_{5000};
public:
    explicit BridgeMonitor(std::uint64_t freshness_ms=5000):
        freshness_ms_(freshness_ms) {
        if(!freshness_ms_ || freshness_ms_>60000)
            throw Error("W14 freshness window must be 1..60000 milliseconds");
    }
    const std::optional<BridgeRecord>& last() const noexcept {return last_;}
    void clear() noexcept {last_.reset();}
    BridgeObservation observe(const BridgeRecord& input,
                              std::uint64_t now_unix_ms) {
        BridgeObservation out;
        out.record=input;
        out.warnings.emplace_back(
            "Eclipse-reported metadata only; cannot attest live debugger, kernel or probe state");
        out.fresh=now_unix_ms>=input.writer_unix_ms &&
                  now_unix_ms-input.writer_unix_ms<=freshness_ms_;
        if(!out.fresh)
            out.warnings.emplace_back("Stale/future publisher timestamp: current target state unknown");
        if(!last_) {
            if(input.sequence!=1 || input.state!=BridgeState::Attached) {
                out.status="no-initial-attach";
                out.warnings.emplace_back("Initial W14 record must be ATTACHED, sequence 1");
                return out;
            }
            out.accepted=true;
            out.transition_known=true;
        } else {
            const auto& previous=*last_;
            if(input.session_nonce!=previous.session_nonce ||
               input.launch_label!=previous.launch_label ||
               input.dsf_session_label!=previous.dsf_session_label ||
               input.epoch!=previous.epoch) {
                // No automatic cross-launch handoff; explicit reset required.
                out.status="session-changed";
                out.warnings.emplace_back(
                    "Session/epoch identity changed: clear old bridge before accepting new ownership");
                return out;
            }
            if(input.sequence<=previous.sequence ||
               input.writer_unix_ms<previous.writer_unix_ms) {
                out.status="out-of-order";
                out.warnings.emplace_back("Replay/rollback detected: older publication refused");
                return out;
            }
            if(input.sequence-previous.sequence>1) {
                out.status="sequence-gap";
                out.warnings.emplace_back(
                    "Missing lifecycle transitions: reported state is not continuous");
                // Gap is accepted as a reported sample but never verified.
                out.accepted=true;
                out.transition_known=false;
            } else if(!bridge_detail::transition(previous.state,input.state)) {
                out.status="invalid-transition";
                out.warnings.emplace_back(
                    "Unexpected lifecycle transition (including terminated session resurrection)");
                return out;
            } else {
                out.accepted=true;
                out.transition_known=true;
            }
        }
        last_=input;
        if(out.status.empty())out.status=out.fresh ? "reported-recent" : "reported-stale";
        return out;
    }
};

} // namespace observatory
