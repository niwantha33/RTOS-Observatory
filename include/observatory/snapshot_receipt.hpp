// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/snapshot_acquisition.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace observatory {

// W15 v1: an OFFLINE, explicitly bounded, self-reported approved-read receipt.
// No assertion of current target state, coherent multi-core halt or probe identity.
// A parsed receipt is evidence of what a *producer claimed*, not a read permit.
namespace receipt_detail {
inline std::uint64_t decimal(const std::string& s) {
    std::uint64_t n{};
    const auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n,10);
    if(s.empty() || ec!=std::errc{} || end!=s.data()+s.size())
        throw Error("W15 receipt: invalid decimal");
    return n;
}
inline std::uint64_t hex(const std::string& raw) {
    std::string_view s(raw);
    if(s.starts_with("0x") || s.starts_with("0X"))s.remove_prefix(2);
    std::uint64_t n{};
    const auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n,16);
    if(s.empty() || ec!=std::errc{} || end!=s.data()+s.size())
        throw Error("W15 receipt: invalid hex");
    return n;
}
inline unsigned char nibble(char c) {
    if(c>='0' && c<='9')return static_cast<unsigned char>(c-'0');
    if(c>='a' && c<='f')return static_cast<unsigned char>(c-'a'+10);
    if(c>='A' && c<='F')return static_cast<unsigned char>(c-'A'+10);
    throw Error("W15 receipt: invalid byte hex");
}
inline std::vector<std::uint8_t> bytes(const std::string& s) {
    if(s.empty() || s.size()%2 || s.size()>512)
        throw Error("W15 receipt: invalid byte length");
    std::vector<std::uint8_t> output;
    output.reserve(s.size()/2);
    for(std::size_t i=0;i<s.size();i+=2)
        output.push_back(static_cast<std::uint8_t>((nibble(s[i])<<4)|nibble(s[i+1])));
    return output;
}
inline SnapshotKind kind(const std::string& s) {
    if(s=="MEM")return SnapshotKind::Memory;
    if(s=="TCB")return SnapshotKind::Tcb;
    if(s=="REG")return SnapshotKind::Register;
    throw Error("W15 receipt: invalid kind");
}
inline bool addr_width(std::uint64_t a,std::uint64_t n,unsigned bits) {
    if(bits==64)return n>0 && n-1<=UINT64_MAX-a;
    return n>0 && a<=UINT32_MAX && n-1<=UINT32_MAX-a;
}
} // namespace receipt_detail

inline SnapshotCapture parse_snapshot_receipt(std::istream& input,
                                              SnapshotLimits limits={}) {
    if(!limits.max_items || !limits.max_single_read_bytes ||
       !limits.max_total_read_bytes || !limits.max_registers)
        throw Error("W15 receipt: invalid limits");
    SnapshotCapture out;
    std::string line;
    std::size_t line_number=0,raw_bytes=0,read_bytes=0,registers=0;
    bool header=false;
    char c=0;
    auto parse=[&](const std::string& s) {
        ++line_number;
        if(s.empty())throw Error("W15 receipt: empty record");
        const auto f=bridge_detail::split(s);
        if(!header) {
            if(f.size()!=8 || f[0]!="RTOS-OBSERVATORY-APPROVED-SNAPSHOT" ||
               f[1]!="1" || f[7]!="unverified")
                throw Error("W15 receipt: unsupported header or claimed coherence");
            if(!snapshot_detail::token(f[2]) || !snapshot_detail::token(f[4]) ||
               !snapshot_detail::token(f[5]))
                throw Error("W15 receipt: invalid source label");
            out.request.session_nonce=f[2];
            out.request.epoch=receipt_detail::decimal(f[3]);
            out.owner_epoch=out.request.epoch;
            out.request.user_approval_id=f[4];
            out.source_label=f[5];
            const auto bits=receipt_detail::decimal(f[6]);
            if(!out.owner_epoch || (bits!=32 && bits!=64))
                throw Error("W15 receipt: invalid epoch or address width");
            out.request.address_bits=static_cast<unsigned>(bits);
            header=true;
            return;
        }
        if(f.size()!=6)throw Error("W15 receipt: unexpected fields");
        if(out.samples.size()>=limits.max_items)
            throw Error("W15 receipt: item limit");
        SnapshotItem item;
        item.kind=receipt_detail::kind(f[0]);
        if(!snapshot_detail::token(f[1]))
            throw Error("W15 receipt: invalid item label");
        item.label=f[1];
        item.address=receipt_detail::hex(f[2]);
        const auto requested=receipt_detail::decimal(f[3]);
        if(requested>UINT32_MAX)
            throw Error("W15 receipt: oversized read size");
        item.bytes=static_cast<std::uint32_t>(requested);
        const bool ok=f[4]=="ok";
        if(!ok && f[4]!="failed")
            throw Error("W15 receipt: result must be ok or failed");
        SnapshotSample sample;
        sample.requested=item;
        sample.success=ok;
        if(item.kind==SnapshotKind::Register) {
            if(item.bytes || item.address || ++registers>limits.max_registers)
                throw Error("W15 receipt: invalid register operation");
            if(ok) sample.register_value=receipt_detail::hex(f[5]);
            else {
                if(f[5]!="-")throw Error("W15 receipt: failed register has value");
                sample.error_code="READ_FAILED";
            }
        } else {
            if(!item.bytes || item.bytes>limits.max_single_read_bytes ||
               !receipt_detail::addr_width(item.address,item.bytes,out.request.address_bits) ||
               item.bytes>limits.max_total_read_bytes-read_bytes)
                throw Error("W15 receipt: unsafe memory range/size");
            read_bytes+=item.bytes;
            if(ok) {
                sample.data=receipt_detail::bytes(f[5]);
                if(sample.data.size()!=item.bytes)
                    throw Error("W15 receipt: truncated/misreported successful bytes");
            } else {
                if(f[5]!="-")throw Error("W15 receipt: failed memory has values");
                sample.error_code="READ_FAILED";
            }
        }
        out.request.items.push_back(std::move(item));
        out.samples.push_back(std::move(sample));
    };
    std::string pending;
    while(input.get(c)) {
        if(++raw_bytes>16384)
            throw Error("W15 receipt: 16 KiB file cap");
        if(c=='\n'){parse(pending);pending.clear();}
        else {
            if(c=='\r' || c=='\0' || pending.size()>=1024)
                throw Error("W15 receipt: invalid control/line too long");
            pending.push_back(c);
        }
    }
    if(!input.eof())throw Error("W15 receipt: input error");
    if(!pending.empty())
        throw Error("W15 receipt: incomplete, missing LF terminator");
    if(!header || out.request.items.empty())
        throw Error("W15 receipt: missing header/items");
    if(!verify_snapshot_capture(out,limits))
        throw Error("W15 receipt: inconsistent sample records");
    return out;
}
inline SnapshotCapture read_snapshot_receipt(const std::filesystem::path& path,
                                             SnapshotLimits limits={}) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path) ||
       std::filesystem::file_size(path)>16384)
        throw Error("W15 receipt: unsafe local path or file size");
    std::ifstream input(path,std::ios::binary);
    if(!input)throw Error("W15 receipt: cannot open local receipt");
    return parse_snapshot_receipt(input,limits);
}

} // namespace observatory
