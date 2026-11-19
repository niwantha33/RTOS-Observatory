// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/debug_bridge.hpp"
#include "observatory/fault_evidence.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace observatory {

// W15 is an explicit user-approved, single-shot acquisition POLICY.
// The C++ core never connects to GDB/OpenOCD and cannot authorize actual
// target reads. The owning Eclipse DSF plugin must independently re-check
// every condition at the point of each read.
enum class SnapshotKind { Memory, Register, Tcb };
struct SafeMemoryRange {
    std::uint64_t address{};
    std::uint64_t length{};
    bool ram{false};
    bool verified_read_safe{false}; // source policy, NOT hardware attestation
    std::string label;
};
struct SnapshotItem {
    SnapshotKind kind{SnapshotKind::Memory};
    std::string label; // used for resource, register, task ID
    std::uint64_t address{};
    std::uint32_t bytes{}; // register reads have 0; no memory view for registers
};
struct SnapshotRequest {
    std::string session_nonce;
    std::uint64_t epoch{};
    std::string user_approval_id;
    std::uint64_t expires_unix_ms{};
    unsigned address_bits{32};
    std::vector<SafeMemoryRange> allowlist;
    std::vector<SnapshotItem> items;
};
struct SnapshotContext {
    BridgeRecord owner;
    bool dsf_service_owner{false};
    bool debugger_suspended{false};
    bool all_cores_confirmed_suspended{false};
    bool memory_read_capability{false};
    bool register_read_capability{false};
    bool user_consent_confirmed{false};
    std::uint64_t now_unix_ms{};
};
struct SnapshotDecision {
    bool allowed{false};
    std::string reason;
    std::size_t planned_memory_bytes{};
    std::size_t planned_registers{};
};
struct SnapshotLimits {
    std::size_t max_items{32};
    std::uint32_t max_single_read_bytes{256};
    std::size_t max_total_read_bytes{4096};
    std::size_t max_registers{16};
    std::uint64_t max_approval_lifetime_ms{30000};
};
namespace snapshot_detail {
inline bool token(const std::string& s) {
    if(s.empty() || s.size()>64)return false;
    for(unsigned char c:s)
        if(c<33 || c>126 || c=='/' || c=='\\' || c=='"' || c==':')
            return false;
    return true;
}
inline bool within(std::uint64_t base,std::uint64_t count,
                   std::uint64_t bound,std::uint64_t length,
                   unsigned bits) {
    if(!count || !length || base<bound)return false;
    const auto maximum=bits==32 ? UINT64_C(0xffffffff) :
                                  UINT64_MAX;
    if(base>maximum || bound>maximum ||
       count-1>maximum-base || length-1>maximum-bound)return false;
    return base-bound<length && count-1<=length-1-(base-bound);
}
} // namespace snapshot_detail

// Always fail closed. Never infer a halt, privilege or RAM safety from W14's
// passive *self-reported* SUSPENDED status.
inline SnapshotDecision qualify_snapshot(const SnapshotRequest& request,
                                          const SnapshotContext& context,
                                          SnapshotLimits limits={}) {
    auto refuse=[](std::string reason) {
        SnapshotDecision out;
        out.reason=std::move(reason);
        return out;
    };
    if(!limits.max_items || !limits.max_single_read_bytes ||
       !limits.max_total_read_bytes || !limits.max_registers ||
       !limits.max_approval_lifetime_ms)
        return refuse("Invalid W15 policy limits");
    if(!context.dsf_service_owner || !context.user_consent_confirmed)
        return refuse("Explicit user consent AND existing owning DSF session required");
    if(!context.debugger_suspended || !context.all_cores_confirmed_suspended ||
       context.owner.state!=BridgeState::Suspended)
        return refuse("Owner must verify all cores stopped; never read a running target");
    if(request.session_nonce!=context.owner.session_nonce ||
       request.epoch!=context.owner.epoch ||
       !snapshot_detail::token(request.session_nonce) ||
       !snapshot_detail::token(request.user_approval_id))
        return refuse("Session/epoch mismatch or missing per-request approval");
    if(!request.expires_unix_ms || !context.now_unix_ms ||
       request.expires_unix_ms<context.now_unix_ms ||
       request.expires_unix_ms-context.now_unix_ms>
           limits.max_approval_lifetime_ms)
        return refuse("Snapshot approval expired or has invalid duration");
    if(request.address_bits!=32 && request.address_bits!=64)
        return refuse("Unsupported address width");
    if(request.items.empty() || request.items.size()>limits.max_items)
        return refuse("Snapshot requires a bounded nonempty item list");
    if(request.allowlist.size()>64)
        return refuse("Too many source-declared memory regions");
    std::set<std::string> deduplicate;
    SnapshotDecision result;
    for(const auto& item:request.items) {
        if(!snapshot_detail::token(item.label))
            return refuse("Invalid register/TCB/memory identifier");
        const auto key=std::to_string(static_cast<int>(item.kind))+":"+item.label;
        if(!deduplicate.insert(key).second)
            return refuse("Duplicate read items require separate approvals");
        if(item.kind==SnapshotKind::Register) {
            if(!context.register_read_capability || item.bytes!=0 ||
               item.address!=0 || item.label.size()>16)
                return refuse("Unsupported or ambiguous register read");
            // Only canonical general CPU registers, no special/MMIO reads.
            static const std::set<std::string> cpu_registers{
                "PC","SP","LR","R0","R1","R2","R3","R4","R5","R6","R7",
                "R8","R9","R10","R11","R12","XPSR","MSP","PSP"
            };
            if(!cpu_registers.contains(item.label))
                return refuse("Unapproved register: only a fixed CPU register allowlist");
            if(++result.planned_registers>limits.max_registers)
                return refuse("Register count exceeds operator cap");
            continue;
        }
        if(!context.memory_read_capability || !item.bytes ||
           item.bytes>limits.max_single_read_bytes)
            return refuse("Memory capability absent or request exceeds per-read cap");
        if(item.kind!=SnapshotKind::Memory && item.kind!=SnapshotKind::Tcb)
            return refuse("Unknown W15 read kind");
        bool safe=false;
        for(const auto& region:request.allowlist) {
            if(!region.ram || !region.verified_read_safe ||
               !snapshot_detail::token(region.label))continue;
            if(snapshot_detail::within(item.address,item.bytes,
                                       region.address,region.length,
                                       request.address_bits)) {
                safe=true;break;
            }
        }
        if(!safe)
            return refuse("Read must fit one explicitly approved read-safe RAM region");
        if(item.bytes>limits.max_total_read_bytes-result.planned_memory_bytes)
            return refuse("Total memory budget exceeded");
        result.planned_memory_bytes+=item.bytes;
    }
    result.allowed=true;
    result.reason="Eligible for single-use owning-DSF capture; recheck at execution";
    return result;
}

class SnapshotConsent {
    std::set<std::string> used_;
public:
    // Reject duplicate attempts regardless of whether the prior DSF read
    // succeeded. No consent token is reused after a failed operation.
    SnapshotDecision consume(const SnapshotRequest& request,
                             const SnapshotContext& context,
                             SnapshotLimits limits={}) {
        auto d=qualify_snapshot(request,context,limits);
        if(!d.allowed)return d;
        const auto key=request.session_nonce+":"+std::to_string(request.epoch)+
                       ":"+request.user_approval_id;
        if(!used_.insert(key).second) {
            d.allowed=false;
            d.reason="Approval already consumed (one-shot, including failed reads)";
            d.planned_memory_bytes=0;
            d.planned_registers=0;
        }
        return d;
    }
    // Consent history is deliberately not reset via the W14 epoch UI.
};

// Separate result validation: a failed sample remains unknown, never zeros.
// The producer must not claim a coherent multi-core snapshot based only on
// these offline records.
struct SnapshotSample {
    SnapshotItem requested;
    bool success{false};
    std::vector<std::uint8_t> data; // raw only; no guessed endianness/TCB layout
    std::optional<std::uint64_t> register_value;
    std::string error_code;
};
struct SnapshotCapture {
    SnapshotRequest request;
    std::string source_label;
    std::uint64_t owner_epoch{};
    std::vector<SnapshotSample> samples;
    bool all_cores_coherent{false}; // no W15 capability asserts coherence
};
inline bool verify_snapshot_capture(const SnapshotCapture& capture,
                                    SnapshotLimits limits={}) {
    if(capture.owner_epoch!=capture.request.epoch ||
       capture.samples.size()!=capture.request.items.size() ||
       capture.samples.size()>limits.max_items ||
       capture.all_cores_coherent ||
       !snapshot_detail::token(capture.source_label))return false;
    for(std::size_t i=0;i<capture.samples.size();++i) {
        const auto& sample=capture.samples[i];
        const auto& req=capture.request.items[i];
        if(sample.requested.kind!=req.kind ||
           sample.requested.label!=req.label ||
           sample.requested.address!=req.address ||
           sample.requested.bytes!=req.bytes)return false;
        if(!sample.success) {
            if(!sample.data.empty() || sample.register_value ||
               !snapshot_detail::token(sample.error_code))return false;
        } else if(req.kind==SnapshotKind::Register) {
            if(!sample.register_value || !sample.data.empty())return false;
        } else if(sample.data.size()!=req.bytes || sample.register_value)
            return false;
    }
    return true;
}

} // namespace observatory
