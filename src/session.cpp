// SPDX-License-Identifier: Apache-2.0
#include "observatory/session.hpp"
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <set>

namespace observatory {
namespace {
std::string bounded_read(std::istream& in, const Limits& limits) {
    std::string raw;
    std::size_t line = 0;
    char c;
    while (in.get(c)) {
        if (raw.size() >= limits.max_bytes) throw Error("input byte limit exceeded");
        if (c == '\n') line = 0;
        else if (++line > limits.max_line_bytes) throw Error("input line limit exceeded");
        raw.push_back(c);
    }
    if (!in.eof()) throw Error("input read failed");
    return raw;
}
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    do {
        const auto end = line.find('\t', start);
        out.push_back(line.substr(start, end == std::string::npos ? end : end-start));
        if (end == std::string::npos) break;
        start = end+1;
    } while (true);
    return out;
}
std::uint64_t uint(const std::string& s) {
    std::uint64_t n{};
    const auto [end, ec] = std::from_chars(s.data(), s.data()+s.size(), n);
    if (s.empty() || ec != std::errc{} || end != s.data()+s.size()) throw Error("invalid unsigned integer");
    return n;
}
void token(const std::string& s) {
    if (s.empty() || s.size() > 128) throw Error("invalid metadata token length");
    for (unsigned char c : s) if (c < 33 || c > 126) throw Error("invalid metadata token");
}
std::uint64_t add_checked(std::uint64_t a, std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max()-a) throw Error("loss counter overflow");
    return a+b;
}
class LegacyDecoder final : public Decoder {
public:
    DecodedTrace decode(std::istream& in, const Limits& limits) const override {
        auto raw = bounded_read(in, limits);
        std::istringstream stream(raw);
        DecodedTrace result;
        result.capture = parse_trace(stream, limits.max_events);
        result.identity = {"unknown", "unknown", "microseconds", 1000000};
        for (const auto& e : result.capture.events)
            if (e.type == "DROP") result.evidence.declared_drops = add_checked(result.evidence.declared_drops, static_cast<std::uint64_t>(e.value));
        return result;
    }
};
class V1Decoder final : public Decoder {
public:
    DecodedTrace decode(std::istream& in, const Limits& limits) const override {
        UniversalStream stream(limits);
        char buffer[4096];
        while (in.read(buffer,sizeof(buffer)) || in.gcount()>0)
            stream.feed(std::string_view(buffer,static_cast<std::size_t>(in.gcount())));
        if(!in.eof()) throw Error("input read failed");
        return stream.finish();
    }
};
std::uint32_t crc32(const std::string& raw) {
    std::uint32_t crc=0xffffffffU;
    for (unsigned char byte : raw) {
        crc ^= byte;
        for (int bit=0;bit<8;++bit) crc=(crc>>1) ^ ((crc&1U) ? 0xedb88320U : 0U);
    }
    return crc ^ 0xffffffffU;
}
std::string metadata(const DecodedTrace& t, const std::string& raw) {
    std::ostringstream s;
    s << "RTOS-OBSERVATORY-SESSION\t1\n" << t.decoder << '\n'
      << t.identity.firmware << '\t' << t.identity.rtos << '\t' << t.identity.clock << '\t' << t.identity.ticks_per_second << '\n'
      << raw.size() << '\t' << crc32(raw) << '\n'
      << t.capture.session << '\t' << t.capture.events.size() << '\n'
      << t.evidence.sequence_available << '\t' << t.evidence.first_sequence << '\t' << t.evidence.last_sequence << '\t' << t.evidence.missing_events << '\t' << t.evidence.declared_drops << '\n';
    return s.str();
}
std::string index(const std::string& raw) {
    // Every data row indexed by physical line, byte offset and timestamp.
    std::ostringstream out;
    out << "RTOS-OBSERVATORY-INDEX\t1\n";
    std::istringstream in(raw);
    std::string line;
    std::size_t offset=0, number=0;
    bool v1 = raw.starts_with("RTOS-OBSERVATORY\t");
    while (std::getline(in,line)) {
        ++number;
        if (!line.empty() && line[0]!='#' && !(v1 && number==1)) {
            const auto f=fields(line);
            out << number << '\t' << offset << '\t' << f.at(v1 ? 1 : 0) << '\n';
        }
        offset += line.size()+1;
    }
    return out.str();
}
void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out) throw Error("session write failed: " + path.string());
}
std::string read(const std::filesystem::path& path, const Limits& limits) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) throw Error("session file must be regular");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("session read failed: " + path.string());
    return bounded_read(in, limits);
}
}
UniversalStream::UniversalStream(Limits limits):limits_(limits) {
    trace_.decoder="universal-v1";
}
void UniversalStream::row() {
    ++line_;
    auto line=std::move(pending_);pending_.clear();
    if(!line.empty() && line.back()=='\r') line.pop_back();
    if(!header_) {
        const auto h=fields(line);
        if(h.size()!=6 || h[0]!="RTOS-OBSERVATORY" || h[1]!="1") throw Error("unsupported event schema");
        token(h[2]);token(h[3]);token(h[4]);
        trace_.identity={h[2],h[3],h[4],uint(h[5])};
        if(trace_.identity.ticks_per_second!=1000000) throw Error("v1 requires normalized microsecond timestamps");
        header_=true;trace_.evidence.sequence_available=true;return;
    }
    if(line.empty() || line[0]=='#') return;
    const auto split=line.find('\t');
    if(split==std::string::npos) throw Error("v1 event requires sequence plus five event fields");
    const auto sequence=uint(line.substr(0,split));
    auto event=detail::parse_event_row(line.substr(split+1),line_);
    auto& evidence=trace_.evidence;
    if(trace_.capture.events.empty()) evidence.first_sequence=sequence;
    else {
        if(sequence<=evidence.last_sequence) throw Error("duplicate or decreasing sequence");
        if(event.timestamp_us<trace_.capture.events.back().timestamp_us) throw Error("decreasing timestamp");
        const auto gap=sequence-evidence.last_sequence-1;
        if(gap) {
            if(gap>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) throw Error("sequence gap exceeds event range");
            evidence.missing_events=add_checked(evidence.missing_events,gap);
            detail::append_event(trace_.capture,{event.timestamp_us,"DROP","-","-",static_cast<std::int64_t>(gap),line_},limits_.max_events);
        }
    }
    if(event.type=="DROP") evidence.declared_drops=add_checked(evidence.declared_drops,static_cast<std::uint64_t>(event.value));
    detail::append_event(trace_.capture,std::move(event),limits_.max_events);
    evidence.last_sequence=sequence;
}
void UniversalStream::feed(std::string_view bytes) {
    if(failed_ || finalized_) throw Error("stream is no longer writable");
    try {
        if(bytes.size()>limits_.max_bytes-bytes_) throw Error("input byte limit exceeded");
        bytes_+=bytes.size();
        for(char c:bytes) {
            if(c=='\n') row();
            else {
                if(pending_.size()>=limits_.max_line_bytes) throw Error("input line limit exceeded");
                pending_.push_back(c);
            }
        }
    } catch(...) {failed_=true;throw;}
}
DecodedTrace UniversalStream::finish(bool require_final_newline) {
    if(failed_ || finalized_) throw Error("stream is no longer writable");
    try {
        if(!pending_.empty()) {
            if(require_final_newline) throw Error("truncated stream: missing final newline");
            row();
        }
        if(!header_ || trace_.capture.events.size()<2) throw Error("missing header, SESSION or trace events");
        finalized_=true;
        return std::move(trace_);
    } catch(...) {failed_=true;throw;}
}
StreamProgress UniversalStream::progress() const {
    if(failed_ || finalized_) throw Error("progress requires an active valid stream");
    return {bytes_,trace_.capture.events.size(),pending_.size(),
        trace_.capture.events.empty()?0:trace_.capture.events.back().timestamp_us,
        trace_.evidence.missing_events,trace_.evidence.declared_drops,header_,false};
}
void DecoderRegistry::add(std::string id, std::shared_ptr<const Decoder> decoder) {
    token(id);
    if (!decoder || decoders_.contains(id)) throw Error("invalid or duplicate decoder");
    decoders_.emplace(std::move(id), std::move(decoder));
}
DecodedTrace DecoderRegistry::decode(const std::string& id, std::istream& in, const Limits& limits) const {
    const auto it=decoders_.find(id);
    if (it==decoders_.end()) throw Error("unknown decoder: " + id);
    auto raw=bounded_read(in, limits);
    std::istringstream bounded(raw);
    auto result=it->second->decode(bounded, limits);
    if (result.capture.events.size()>limits.max_events) throw Error("decoder event limit exceeded");
    result.decoder=id;
    return result;
}
DecoderRegistry default_decoders() {
    DecoderRegistry registry;
    registry.add("legacy-tsv", std::make_shared<LegacyDecoder>());
    registry.add("universal-v1", std::make_shared<V1Decoder>());
    return registry;
}
void store_session(const std::filesystem::path& directory, const std::string& decoder, std::istream& in, const Limits& limits) {
    const auto raw=bounded_read(in, limits);
    std::istringstream stream(raw);
    const auto trace=default_decoders().decode(decoder, stream, limits);
    // Reserving a previously absent directory prevents accidental overwrites.
    if (!std::filesystem::create_directory(directory)) throw Error("session directory already exists");
    try {
        write(directory/"raw.trace", raw);
        write(directory/"index.tsv", index(raw));
        write(directory/"metadata.tsv", metadata(trace, raw)); // commit marker written last
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
}
DecodedTrace replay_session(const std::filesystem::path& directory, const Limits& limits) {
    if (std::filesystem::is_symlink(directory)) throw Error("session directory cannot be a symlink");
    auto meta=read(directory/"metadata.tsv", limits);
    std::istringstream header(meta);
    std::string line, decoder;
    std::getline(header,line); std::getline(header,decoder);
    if (line!="RTOS-OBSERVATORY-SESSION\t1") throw Error("unsupported session schema");
    const auto raw=read(directory/"raw.trace", limits);
    std::istringstream input(raw);
    auto trace=default_decoders().decode(decoder,input,limits);
    if (metadata(trace, raw)!=meta || index(raw)!=read(directory/"index.tsv",limits)) throw Error("session metadata/index mismatch");
    return trace;
}
} // namespace observatory
