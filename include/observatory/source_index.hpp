// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/engine.hpp"
#include "observatory/sha256.hpp"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace observatory {

struct SymbolLimits {
    std::size_t max_manifest_bytes{1024*1024};
    std::size_t max_line_bytes{4096};
    std::size_t max_files{512};
    std::size_t max_functions{4000};
    std::size_t max_calls{10000};
    std::size_t max_locks{10000};
    std::uint64_t max_elf_bytes{256ull*1024*1024};
    std::uint64_t max_source_bytes{2ull*1024*1024};
};
struct SourceFunction {
    std::string id,language,name;
    std::uint64_t address{},size{};
    std::string path;
    std::uint64_t line{};
    std::size_t manifest_line{};
};
struct SourceCall {
    std::string caller,callee;
    std::uint64_t line{};
    std::size_t manifest_line{};
};
struct SourceLock {
    std::string function,resource,operation;
    std::uint64_t line{};
    std::size_t manifest_line{};
};
struct SourceIndex {
    std::string firmware_label,elf_sha256;
    unsigned address_bits{};
    std::map<std::string,std::string> source_sha256;
    std::vector<SourceFunction> functions;
    std::vector<SourceCall> calls;
    std::vector<SourceLock> locks;
};
struct SymbolVerification {
    bool elf_matches{false};
    bool all_sources_match{false};
    bool manifest_authenticated{false}; // source map claims NEVER independently attested here
    std::map<std::string,std::filesystem::path> verified_sources;
    std::filesystem::path canonical_source_root;
    std::vector<std::string> warnings;
};
struct SymbolMatch {
    std::optional<std::size_t> function_index;
    bool ambiguous{false};
    bool verified_elf_identity{false}; // only the byte digest and ELF header
};
struct SourceExcerpt {
    std::string path;
    std::uint64_t target_line{};
    std::vector<std::pair<std::uint64_t,std::string>> lines;
};
namespace symbol_detail {
inline std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> fields;
    std::size_t begin=0;
    do {
        const auto end=s.find('\t',begin);
        fields.push_back(s.substr(begin,end==std::string::npos ? end : end-begin));
        if(end==std::string::npos)break;
        begin=end+1;
    } while(true);
    return fields;
}
inline void token(const std::string& s,std::size_t line,std::size_t max=256) {
    if(s.empty() || s.size()>max)
        throw Error("symbol line "+std::to_string(line)+": invalid text length");
    for(unsigned char c:s)
        if(c<32 || c==127)
            throw Error("symbol line "+std::to_string(line)+": control in field");
}
inline void path(const std::string& s,std::size_t line) {
    token(s,line,256);
    if(s[0]=='/' || s.find('\\')!=std::string::npos ||
       s.find(':')!=std::string::npos || s.back()=='/' ||
       s.find("//")!=std::string::npos)
        throw Error("symbol line "+std::to_string(line)+": unsafe relative source path");
    const std::filesystem::path p(s);
    if(p.is_absolute() || p.has_root_path())
        throw Error("symbol line "+std::to_string(line)+": source path must be relative");
    for(const auto& part:p)
        if(part==".." || part=="." || part.empty())
            throw Error("symbol line "+std::to_string(line)+": traversal in source path");
}
inline void digest(const std::string& hash,std::size_t line) {
    if(hash.size()!=64)
        throw Error("symbol line "+std::to_string(line)+": expected 64 lowercase SHA-256 hex");
    for(char c:hash)
        if(!((c>='0' && c<='9')||(c>='a' && c<='f')))
            throw Error("symbol line "+std::to_string(line)+": invalid SHA-256");
}
inline std::uint64_t decimal(const std::string& str,std::size_t line) {
    std::uint64_t n{};
    const auto [end,ec]=std::from_chars(str.data(),str.data()+str.size(),n);
    if(str.empty() || ec!=std::errc{} || end!=str.data()+str.size())
        throw Error("symbol line "+std::to_string(line)+": invalid decimal");
    return n;
}
inline std::uint64_t hex(const std::string& str,std::size_t line) {
    std::string_view s(str);
    if(s.starts_with("0x") || s.starts_with("0X"))s.remove_prefix(2);
    std::uint64_t n{};
    const auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n,16);
    if(s.empty() || ec!=std::errc{} || end!=s.data()+s.size())
        throw Error("symbol line "+std::to_string(line)+": invalid address/length");
    return n;
}
inline bool valid_range(std::uint64_t begin,std::uint64_t n,unsigned bits) {
    const auto max=bits==32 ? UINT64_C(0xffffffff) :
                               std::numeric_limits<std::uint64_t>::max();
    return n!=0 && begin<=max && n-1<=max-begin;
}
inline bool inside(const std::filesystem::path& root,
                   const std::filesystem::path& absolute) {
    if(absolute==root)return false;
    const auto relative=absolute.lexically_relative(root);
    if(relative.empty() || relative.is_absolute())return false;
    for(const auto& item:relative)
        if(item=="..")return false;
    return true;
}
inline std::string file_sha256(const std::filesystem::path& path,
                               std::uint64_t limit,
                               bool require_elf=false,
                               unsigned expected_elf_bits=0) {
    if(!std::filesystem::is_regular_file(path) ||
       std::filesystem::file_size(path)>limit)
        throw Error("Symbol identity file absent or exceeds configured size limit");
    std::ifstream in(path,std::ios::binary);
    if(!in)throw Error("Could not open identity file");
    hash_detail::Sha256 sha;
    std::array<char,16384> buf{};
    std::array<unsigned char,64> prefix{};
    std::size_t prefix_size=0;
    std::uint64_t consumed=0;
    while(in.read(buf.data(),static_cast<std::streamsize>(buf.size())) || in.gcount()>0) {
        const auto n=static_cast<std::size_t>(in.gcount());
        if(n>limit-consumed)throw Error("Identity file size changed during read");
        consumed+=n;
        if(prefix_size<prefix.size()) {
            const auto take=std::min(prefix.size()-prefix_size,n);
            for(std::size_t i=0;i<take;++i)
                prefix[prefix_size+i]=static_cast<unsigned char>(buf[i]);
            prefix_size+=take;
        }
        sha.update(reinterpret_cast<const std::uint8_t*>(buf.data()),n);
    }
    if(!in.eof())throw Error("Identity file read failed");
    if(require_elf) {
        if(prefix_size<16 || prefix[0]!=0x7f || prefix[1]!='E' ||
           prefix[2]!='L' || prefix[3]!='F' ||
           (prefix[4]!=1 && prefix[4]!=2) ||
           prefix[5]!=1 || prefix[6]!=1)
            throw Error("Not a supported little-endian ELF32/ELF64 header");
        if(expected_elf_bits &&
           ((expected_elf_bits==32 && prefix[4]!=1) ||
            (expected_elf_bits==64 && prefix[4]!=2)))
            throw Error("ELF address class contradicts declared symbol index");
        const auto expected_header=prefix[4]==1 ? 52u : 64u;
        const auto actual_header=prefix[4]==1 ?
            static_cast<unsigned>(prefix[40]) |
               (static_cast<unsigned>(prefix[41])<<8) :
            static_cast<unsigned>(prefix[52]) |
               (static_cast<unsigned>(prefix[53])<<8);
        if(prefix_size<expected_header || actual_header!=expected_header)
            throw Error("ELF header size field inconsistent with ELF class");
    }
    return sha.digest();
}
} // namespace symbol_detail

inline SourceIndex parse_source_index(std::istream& input,SymbolLimits limits={}) {
    if(!limits.max_manifest_bytes || !limits.max_line_bytes || !limits.max_files ||
       !limits.max_functions || !limits.max_calls || !limits.max_locks ||
       !limits.max_elf_bytes || !limits.max_source_bytes)
        throw Error("W13 source limits must be positive");
    SourceIndex result;
    bool header=false;
    std::size_t bytes=0,line_no=0;
    std::set<std::string> ids;
    auto row=[&](std::string line) {
        ++line_no;
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty() || line[0]=='#')return;
        const auto cells=symbol_detail::split(line);
        auto expect=[&](std::size_t n) {
            if(cells.size()!=n)
                throw Error("symbol line "+std::to_string(line_no)+": wrong column count");
        };
        if(!header) {
            expect(5);
            if(cells[0]!="RTOS-OBSERVATORY-SYMBOLS" || cells[1]!="1")
                throw Error("Unsupported W13 sidecar schema");
            symbol_detail::digest(cells[2],line_no);
            symbol_detail::token(cells[3],line_no,128);
            const auto bits=symbol_detail::decimal(cells[4],line_no);
            if(bits!=32 && bits!=64)throw Error("W13 address width must be 32 or 64");
            result.elf_sha256=cells[2];
            result.firmware_label=cells[3];
            result.address_bits=static_cast<unsigned>(bits);
            header=true;
        } else if(cells[0]=="FILE") {
            expect(3);
            if(result.source_sha256.size()>=limits.max_files)
                throw Error("W13 source file cap");
            symbol_detail::path(cells[1],line_no);
            symbol_detail::digest(cells[2],line_no);
            if(!result.source_sha256.emplace(cells[1],cells[2]).second)
                throw Error("W13 duplicate source path");
        } else if(cells[0]=="FUNC") {
            expect(8);
            if(result.functions.size()>=limits.max_functions)
                throw Error("W13 function cap");
            symbol_detail::token(cells[1],line_no,64);
            symbol_detail::token(cells[3],line_no);
            symbol_detail::path(cells[6],line_no);
            if(cells[2]!="C" && cells[2]!="C++" && cells[2]!="Rust")
                throw Error("W13 language must be C, C++, or Rust");
            if(!ids.insert(cells[1]).second)throw Error("W13 duplicate function ID");
            const auto address=symbol_detail::hex(cells[4],line_no);
            const auto size=symbol_detail::hex(cells[5],line_no);
            if(!symbol_detail::valid_range(address,size,result.address_bits))
                throw Error("W13 invalid function range");
            const auto src_line=symbol_detail::decimal(cells[7],line_no);
            if(src_line==0)throw Error("W13 source line must be positive");
            result.functions.push_back({cells[1],cells[2],cells[3],address,size,
                                        cells[6],src_line,line_no});
        } else if(cells[0]=="CALL") {
            expect(4);
            if(result.calls.size()>=limits.max_calls)throw Error("W13 call edge cap");
            symbol_detail::token(cells[1],line_no,64);
            symbol_detail::token(cells[2],line_no,64);
            const auto line_num=symbol_detail::decimal(cells[3],line_no);
            if(!line_num)throw Error("W13 call line must be positive");
            result.calls.push_back({cells[1],cells[2],line_num,line_no});
        } else if(cells[0]=="LOCK") {
            expect(5);
            if(result.locks.size()>=limits.max_locks)throw Error("W13 lock edge cap");
            symbol_detail::token(cells[1],line_no,64);
            symbol_detail::token(cells[2],line_no,128);
            if(cells[3]!="acquire" && cells[3]!="release" &&
               cells[3]!="wait" && cells[3]!="other")
                throw Error("W13 lock operation must be explicit");
            const auto line_num=symbol_detail::decimal(cells[4],line_no);
            if(!line_num)throw Error("W13 lock line must be positive");
            result.locks.push_back({cells[1],cells[2],cells[3],line_num,line_no});
        } else throw Error("symbol line "+std::to_string(line_no)+": unknown record");
    };
    std::string pending;
    char ch=0;
    while(input.get(ch)) {
        if(++bytes>limits.max_manifest_bytes)throw Error("W13 manifest byte cap");
        if(ch=='\n'){row(std::move(pending));pending.clear();}
        else {
            if(pending.size()>=limits.max_line_bytes)
                throw Error("W13 manifest line cap");
            pending.push_back(ch);
        }
    }
    if(!input.eof())throw Error("W13 manifest read failed");
    if(!pending.empty())row(std::move(pending));
    if(!header)throw Error("W13 manifest header missing");
    for(const auto& f:result.functions)
        if(!result.source_sha256.contains(f.path))
            throw Error("W13 function references undeclared source path");
    for(const auto& e:result.calls)
        if(!ids.contains(e.caller) || !ids.contains(e.callee))
            throw Error("W13 call references unknown function");
    for(const auto& e:result.locks)
        if(!ids.contains(e.function))
            throw Error("W13 lock edge references unknown function");
    return result;
}

inline SymbolVerification verify_source_index(
    const SourceIndex& index,const std::filesystem::path& elf,
    const std::filesystem::path& root,SymbolLimits limits={}) {
    SymbolVerification out;
    try {
        const auto elf_hash=symbol_detail::file_sha256(elf,limits.max_elf_bytes,true,index.address_bits);
        out.elf_matches=elf_hash==index.elf_sha256;
        if(!out.elf_matches)
            out.warnings.push_back("ELF SHA-256 differs from the sidecar: address/source navigation disabled");
    } catch(const std::exception& e) {
        out.warnings.push_back(std::string("ELF identity unavailable: ")+e.what());
    }
    try {
        const auto canonical_root=std::filesystem::canonical(root);
        out.canonical_source_root=canonical_root;
        if(!std::filesystem::is_directory(canonical_root))
            throw Error("Source root is not a directory");
        for(const auto& [name,expected]:index.source_sha256) {
            try {
                const auto file=std::filesystem::canonical(canonical_root/name);
                if(!symbol_detail::inside(canonical_root,file))
                    throw Error("Source path escapes selected root");
                if(symbol_detail::file_sha256(file,limits.max_source_bytes)==expected)
                    out.verified_sources.emplace(name,file);
                else out.warnings.push_back("Source SHA-256 mismatch: "+name);
            } catch(const std::exception& e) {
                out.warnings.push_back("Source unavailable or unsafe: "+name+" ("+e.what()+")");
            }
        }
    } catch(const std::exception& e) {
        out.warnings.push_back(std::string("Source root unavailable: ")+e.what());
    }
    out.all_sources_match=out.verified_sources.size()==index.source_sha256.size();
    if(!out.manifest_authenticated)
        out.warnings.push_back("Sidecar symbols/call/lock edges are source DECLARATIONS, not validated DWARF or ELF disassembly");
    if(!out.elf_matches || !out.all_sources_match)
        out.warnings.push_back("Source jump links require matching local ELF and source SHA-256 identities");
    return out;
}
inline SymbolMatch resolve_source_pc(const SourceIndex& index,
                                     const SymbolVerification& verify,
                                     std::uint64_t pc) {
    SymbolMatch out;
    out.verified_elf_identity=verify.elf_matches;
    if(!verify.elf_matches)return out;
    for(std::size_t i=0;i<index.functions.size();++i) {
        const auto& f=index.functions[i];
        if(pc<f.address || pc-f.address>=f.size)continue;
        if(out.function_index) {
            out.function_index.reset();
            out.ambiguous=true;
            break;
        }
        out.function_index=i;
    }
    return out;
}
inline SourceExcerpt read_verified_source_excerpt(const SourceIndex& index,
    const SymbolVerification& verified,std::size_t function_index,
    std::uint64_t around=4,SymbolLimits limits={}) {
    if(!verified.elf_matches || function_index>=index.functions.size())
        throw Error("ELF identity not verified or source function missing");
    const auto& f=index.functions[function_index];
    const auto it=verified.verified_sources.find(f.path);
    if(it==verified.verified_sources.end())
        throw Error("Source checksum unknown/mismatch: refusing source navigation");
    // Do not follow a path that moved outside the verified source root
    // after the manifest was loaded.
    const auto now=std::filesystem::canonical(it->second);
    if(verified.canonical_source_root.empty() ||
       !symbol_detail::inside(verified.canonical_source_root,now))
        throw Error("Source path no longer confined to selected root");
    // Read once into a bounded buffer, hash THESE SAME bytes and only then
    // expose lines. A separate pre-read hash would create a TOCTOU window.
    std::ifstream file(now,std::ios::binary);
    if(!file)throw Error("Source file no longer readable");
    std::string contents;
    std::array<char,8192> buffer{};
    while(file.read(buffer.data(),static_cast<std::streamsize>(buffer.size())) ||
          file.gcount()>0) {
        const auto n=static_cast<std::size_t>(file.gcount());
        if(n>limits.max_source_bytes-contents.size())
            throw Error("Source file exceeded the preview byte budget");
        contents.append(buffer.data(),n);
    }
    if(!file.eof())throw Error("Verified source file read failed");
    if(hash_detail::sha256(contents)!=index.source_sha256.at(f.path))
        throw Error("Source file changed since import: refusing source navigation");
    std::istringstream in(contents);
    SourceExcerpt out;
    out.path=f.path;
    out.target_line=f.line;
    std::string line;
    std::uint64_t number=0;
    const auto first=f.line>around ? f.line-around : 1;
    const auto last=around>std::numeric_limits<std::uint64_t>::max()-f.line ?
                    std::numeric_limits<std::uint64_t>::max() : f.line+around;
    while(std::getline(in,line)) {
        ++number;
        if(number>=first && number<=last) {
            if(line.size()>4096)throw Error("Source preview line exceeds 4096 bytes");
            if(!line.empty() && line.back()=='\r')line.pop_back();
            out.lines.emplace_back(number,std::move(line));
        }
        if(number>=last)break;
    }
    if(number<f.line)throw Error("Declared function line is beyond verified source file");
    return out;
}
} // namespace observatory
