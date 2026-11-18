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
        auto raw = bounded_read(in, limits);
        std::istringstream stream(raw);
        std::string line;
        if (!std::getline(stream, line)) throw Error("missing v1 header");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto h = fields(line);
        if (h.size() != 6 || h[0] != "RTOS-OBSERVATORY" || h[1] != "1") throw Error("unsupported event schema");
        token(h[2]); token(h[3]); token(h[4]);
        DecodedTrace result;
        result.identity = {h[2], h[3], h[4], uint(h[5])};
        if (result.identity.ticks_per_second != 1000000) throw Error("v1 requires normalized microsecond timestamps");
        std::ostringstream normalized;
        std::uint64_t previous = 0;
        bool first = true;
        std::size_t count = 0;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (++count > limits.max_events) throw Error("event limit exceeded");
            const auto f = fields(line);
            if (f.size() != 6) throw Error("v1 event requires sequence plus five event fields");
            const auto seq = uint(f[0]);
            if (!first && seq <= previous) throw Error("duplicate or decreasing sequence");
            if (first) result.evidence.first_sequence = seq;
            else if (seq-previous > 1) {
                const auto missing = seq-previous-1;
                if (missing > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) throw Error("sequence gap exceeds event range");
                result.evidence.missing_events = add_checked(result.evidence.missing_events, missing);
                normalized << f[1] << "\tDROP\t-\t-\t" << missing << '\n';
            }
            for (std::size_t i=1; i<f.size(); ++i) normalized << (i==1 ? "" : "\t") << f[i];
            normalized << '\n';
            previous = seq; first = false;
        }
        result.evidence.sequence_available = true;
        result.evidence.last_sequence = previous;
        std::istringstream events(normalized.str());
        result.capture = parse_trace(events, limits.max_events);
        // Synthetic DROP events carry gap evidence; explicit DROP is separately counted.
        std::istringstream original(raw);
        std::getline(original, line);
        while (std::getline(original, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto f = fields(line);
            if (f.size()==6 && f[2]=="DROP") result.evidence.declared_drops = add_checked(result.evidence.declared_drops, uint(f[5]));
        }
        return result;
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
