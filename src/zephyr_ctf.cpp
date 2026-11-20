// SPDX-License-Identifier: Apache-2.0
#include "observatory/zephyr_ctf.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
namespace observatory::ctf {
namespace {
struct Field { std::string type,name; std::size_t bytes{}; bool bounded_string{}; bool sign{}; };
struct Definition { std::string name; std::vector<Field> fields; std::size_t record_bytes{6}; };
using Definitions = std::map<std::uint16_t,Definition>;
constexpr std::size_t max_metadata = 256 * 1024, max_stream = 64 * 1024 * 1024;
std::string read_local(const std::filesystem::path& path, std::size_t limit) {
    if(std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("CTF input must be an existing local regular file (symlink denied)");
    const auto length=std::filesystem::file_size(path);
    if(length>limit) throw std::runtime_error("CTF bounded file limit exceeded");
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Cannot open local CTF input");
    std::string bytes(static_cast<std::size_t>(length),'\0');
    if(!bytes.empty()) file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(!file) throw std::runtime_error("CTF source changed during read");
    return bytes;
}
std::string compact(const std::string& s) {
    return std::regex_replace(s,std::regex(R"(\s+)")," ");
}
bool match(const std::string& s,const std::string& pattern) {
    return std::regex_search(s,std::regex(pattern));
}
std::uint64_t number(const std::string& s,int base=10) {
    std::uint64_t v{};
    const auto* begin=s.data() + (base==16 && s.size()>2?2:0);
    const auto [ptr,ec]=std::from_chars(begin,s.data()+s.size(),v,base);
    if(ec!=std::errc() || ptr!=s.data()+s.size()) throw std::runtime_error("Invalid numeric CTF metadata");
    return v;
}
std::size_t field_bytes(const std::string& kind) {
    if(kind=="uint8_t" || kind=="int8_t") return 1;
    if(kind=="uint16_t" || kind=="int16_t") return 2;
    if(kind=="uint32_t" || kind=="int32_t") return 4;
    if(kind=="uint64_t" || kind=="int64_t") return 8;
    return 0;
}
Definitions parse_metadata(const std::string& s) {
    if(s.rfind("/* CTF 1.8 */",0)!=0 || !match(s,R"(\bmajor\s*=\s*1\s*;)") ||
       !match(s,R"(\bminor\s*=\s*8\s*;)") || !match(s,R"(\bbyte_order\s*=\s*le\s*;)"))
        throw std::runtime_error("W20 supports CTF 1.8 little-endian metadata only");
    const std::regex header(R"(struct\s+event_header\s*\{([\s\S]*?)\}\s*;)");
    std::smatch h;
    if(!std::regex_search(s,h,header) ||
       compact(h[1].str())!=" uint32_t timestamp; uint16_t id; " ||
       !match(s,R"(event\.header\s*:=\s*struct\s+event_header\s*;)"))
        throw std::runtime_error("Unsupported Zephyr CTF event header or stream context");
    if(match(s,R"(\b(packet|variant|enum)\s*\{)"))
        throw std::runtime_error("CTF packet, enum and variant layouts are not supported");
    std::map<std::string,std::string> aliases;
    const std::regex alias(R"(typealias\s+integer\s*\{([^{}]*)\}\s*:=\s*(\w+)\s*;)");
    for(auto i=std::sregex_iterator(s.begin(),s.end(),alias);i!=std::sregex_iterator();++i){
        const auto attrs=compact((*i)[1].str()), name=(*i)[2].str();
        if(!aliases.emplace(name,attrs).second) throw std::runtime_error("Duplicate CTF integer alias");
        const auto bytes=field_bytes(name);
        if(bytes){
            const auto sign=name.rfind("int",0)==0?"true":"false";
            const std::string expected=" size = "+std::to_string(bytes*8)+
                "; align = 8; signed = "+sign+"; ";
            if(attrs!=expected) throw std::runtime_error("Unqualified integer alias representation");
        } else if(name=="ctf_bounded_string_t") {
            if(attrs!=" size = 8; align = 8; signed = false; encoding = ASCII; ")
                throw std::runtime_error("Unqualified string alias representation");
        }
    }
    if(aliases.count("uint32_t")==0 || aliases.count("uint16_t")==0)
        throw std::runtime_error("CTF header integer aliases missing");
    const std::regex block(R"(\bevent\s*\{([\s\S]*?)\n\};)");
    const std::regex name_re(R"(\bname\s*=\s*(\w+)\s*;)");
    const std::regex id_re(R"(\bid\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*;)");
    const std::regex fields_re(R"(\bfields\s*:=\s*struct\s*\{([\s\S]*?)\}\s*;)");
    const std::regex declaration(R"(^\s*(\w+)\s+(\w+)(?:\[(\d+)\])?\s*$)");
    Definitions defs;
    std::set<std::string> names;
    std::size_t seen=0;
    for(auto i=std::sregex_iterator(s.begin(),s.end(),block);i!=std::sregex_iterator();++i){
        if(++seen>1024) throw std::runtime_error("Excessive CTF definitions");
        const auto part=(*i)[1].str();
        std::smatch n,id,f;
        if(!std::regex_search(part,n,name_re) || !std::regex_search(part,id,id_re))
            throw std::runtime_error("Unrecognized CTF event declaration");
        const std::string idstr=id[1].str();
        const auto rawid=number(idstr,idstr.starts_with("0x") || idstr.starts_with("0X")?16:10);
        if(rawid>65535 || !names.insert(n[1].str()).second)
            throw std::runtime_error("CTF duplicate event name or invalid ID");
        Definition d;
        d.name=n[1].str();
        if(std::regex_search(part,f,fields_re)){
            const auto body=f[1].str();
            std::size_t pos=0;
            std::set<std::string> fieldnames;
            while(pos<body.size()){
                auto stop=body.find(';',pos);
                const std::string decl=body.substr(pos,stop==std::string::npos?stop:stop-pos);
                pos=stop==std::string::npos?body.size():stop+1;
                if(compact(decl)==" " || decl.empty()) continue;
                std::smatch m;
                if(!std::regex_match(decl,m,declaration))
                    throw std::runtime_error("Unsupported CTF field declaration");
                Field field;
                field.type=m[1].str();field.name=m[2].str();
                if(!fieldnames.insert(field.name).second || aliases.count(field.type)==0)
                    throw std::runtime_error("CTF duplicate field or undeclared alias");
                field.bounded_string=field.type=="ctf_bounded_string_t";
                if(field.bounded_string){
                    if(!m[3].matched) throw std::runtime_error("CTF string length missing");
                    field.bytes=static_cast<std::size_t>(number(m[3].str()));
                    if(field.bytes==0 || field.bytes>128) throw std::runtime_error("CTF string limit");
                }else{
                    if(m[3].matched) throw std::runtime_error("CTF integer arrays unsupported");
                    field.bytes=field_bytes(field.type);
                    if(field.bytes==0) throw std::runtime_error("Unsupported CTF field type");
                    field.sign=field.type.rfind("int",0)==0;
                }
                d.record_bytes+=field.bytes;
                if(d.fields.size()>=48 || d.record_bytes>2048)
                    throw std::runtime_error("CTF record/field bound exceeded");
                d.fields.push_back(std::move(field));
            }
        }
        std::string remaining=part;
        if(std::regex_search(part,f,fields_re))
            remaining.replace(static_cast<std::size_t>(f.position()),f.length(),"");
        remaining=std::regex_replace(remaining,name_re,"",std::regex_constants::format_first_only);
        remaining=std::regex_replace(remaining,id_re,"",std::regex_constants::format_first_only);
        if(!compact(remaining).empty() && compact(remaining)!=" ")
            throw std::runtime_error("Unrecognized CTF event attributes");
        if(!defs.emplace(static_cast<std::uint16_t>(rawid),std::move(d)).second)
            throw std::runtime_error("Duplicate CTF event ID");
    }
    const std::regex event_start(R"(\bevent\s*\{)");
    if(defs.empty() || std::distance(std::sregex_iterator(s.begin(),s.end(),event_start),
                      std::sregex_iterator())!=static_cast<std::ptrdiff_t>(seen))
        throw std::runtime_error("Malformed or absent CTF event blocks");
    return defs;
}
std::uint64_t little(const std::string& bytes,std::size_t off,std::size_t len){
    std::uint64_t val=0;
    for(std::size_t i=0;i<len;++i) val|=static_cast<std::uint64_t>(
        static_cast<unsigned char>(bytes[off+i])) << (8*i);
    return val;
}
std::string field_to_string(const std::string& buf,std::size_t pos,const Field& f){
    if(f.bounded_string){
        std::string s;
        for(std::size_t i=0;i<f.bytes && buf[pos+i]!='\0';++i){
            auto ch=static_cast<unsigned char>(buf[pos+i]);
            s+=ch>=32 && ch<=126?static_cast<char>(ch):'?';
        }
        return s;
    }
    const auto u=little(buf,pos,f.bytes);
    if(!f.sign)return std::to_string(u);
    if(f.bytes==1)return std::to_string(static_cast<std::int8_t>(u));
    if(f.bytes==2)return std::to_string(static_cast<std::int16_t>(u));
    if(f.bytes==4)return std::to_string(static_cast<std::int32_t>(u));
    // Casting unsigned out-of-range to signed is implementation-defined; use
    // the magnitude split so no undefined 64-bit negation occurs.
    if(u<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::to_string(static_cast<std::int64_t>(u));
    if(u==0x8000000000000000ULL)return "-9223372036854775808";
    return "-"+std::to_string((~u)+1);
}
std::optional<std::uint64_t> id_of(const Event& e){
    auto it=e.fields.find("id");
    if(it==e.fields.end()) return std::nullopt;
    try{return number(it->second);}catch(...){return std::nullopt;}
}
std::optional<std::int64_t> actor_of(const Event& e){
    return e.actor>=0?std::optional<std::int64_t>(e.actor):std::nullopt;
}
bool same_actor(const Event& a,const Event& b){
    return a.actor>=0 && b.actor>=0 && a.actor==b.actor;
}
void find_resources(Session& s) {
    const auto& v=s.events;
    for(std::size_t i=0;i<v.size();++i){
        const auto& block=v[i];
        std::string kind,exit,signal;
        if(block.name=="mutex_lock_blocking"){kind="Mutex";exit="mutex_lock_exit";signal="mutex_unlock_enter";}
        else if(block.name=="semaphore_take_blocking"){kind="Semaphore";exit="semaphore_take_exit";signal="semaphore_give_enter";}
        else if(block.name=="msgq_get_blocking"){kind="Message queue";exit="msgq_get_exit";signal="msgq_put_enter";}
        else continue;
        const auto id=id_of(block);
        if(!id) continue;
        Finding finding;finding.kind=kind;finding.title=kind+" blocking / resource "+std::to_string(*id);
        finding.evidence.push_back(i);
        std::optional<std::size_t> finish,associated,ready,owner;
        for(std::size_t j=i+1;j<v.size();++j){
            if(!finish && v[j].name==exit && id_of(v[j])==id && same_actor(v[j],block)){
                finish=j;
                auto ret=v[j].fields.find("ret");
                finding.complete=(ret!=v[j].fields.end() && ret->second=="0");
                break;
            }
        }
        if(kind=="Mutex"){
            // Owner is a hypothesis only when recorded successful lock callbacks
            // have a unique live candidate without intervening unlock callbacks.
            for(std::size_t j=0;j<i;++j){
                if(id_of(v[j])!=id)continue;
                if(v[j].name=="mutex_unlock_enter" || v[j].name=="mutex_unlock_exit")
                    owner.reset();
                else if(v[j].name=="mutex_lock_exit" &&
                        describe_field(v[j],"ret")=="0" && v[j].actor>=0 &&
                        v[j].actor!=block.actor){
                    if(owner)owner.reset(); // conflicting owners, do not promote
                    else owner=j;
                }
            }
            if(owner)finding.evidence.insert(finding.evidence.begin(),*owner);
        }
        const auto limit=finish.value_or(v.size());
        for(std::size_t j=i+1;j<limit;++j){
            if(v[j].name==signal && id_of(v[j])==id){
                if(kind=="Mutex"){
                    if(owner && same_actor(v[j],v[*owner]) && !associated)associated=j;
                } else if(!associated)associated=j;
            }
            if((v[j].name=="thread_ready" || v[j].name=="thread_sched_ready") &&
                block.actor>=0 && describe_field(v[j],"thread_id")==std::to_string(block.actor) && !ready)
                ready=j;
        }
        if(associated)finding.evidence.push_back(*associated);
        if(ready)finding.evidence.push_back(*ready);
        if(finish)finding.evidence.push_back(*finish);
        finding.start_tick=block.tick;
        finding.end_tick=finish?v[*finish].tick:block.tick;
        finding.assessment=finding.complete
            ?"Observed blocking callback and matching successful completion; duration is raw ticks, not calibrated time."
            :"Incomplete or unsuccessful wait; missing events and return semantics remain unknown.";
        if(associated) finding.assessment+=" Same-object "+std::string(kind=="Mutex"?"unlock-enter":kind=="Semaphore"?"give-enter":"put-enter")+" was observed (correlation only).";
        if(owner)finding.assessment+=" A candidate mutex holder was observed; ownership continuity is not independently proven.";
        if(ready && finish){
            const auto& r=v[*ready];
            if(r.tick>=block.tick && v[*finish].tick>=r.tick)
                finding.assessment+=" Blocked-to-ready "+std::to_string(r.tick-block.tick)+
                    " ticks, ready-to-completion "+std::to_string(v[*finish].tick-r.tick)+" ticks.";
        }
        // W20: Only complete, same-actor callback pairs within the candidate
        // holder interval may establish a bounded temporal overlap. This never
        // establishes CPU residency, priority inheritance, or causal identity.
        if(kind=="Mutex" && owner && associated && ready && finish &&
           v[*ready].tick>=block.tick && v[*associated].tick>=block.tick) {
            std::optional<std::size_t> busy_begin;
            std::vector<std::size_t> callbacks;
            bool ambiguous=false;
            std::uint64_t overlap=0;
            const auto deadline=v[*ready].tick;
            for(std::size_t j=*owner+1;j<*associated;++j) {
                if(!same_actor(v[j],v[*owner]))continue;
                if(v[j].name=="thread_busy_wait_enter") {
                    if(busy_begin){ambiguous=true;break;}
                    busy_begin=j;
                }else if(v[j].name=="thread_busy_wait_exit") {
                    if(!busy_begin || v[j].tick<v[*busy_begin].tick){ambiguous=true;break;}
                    const auto left=std::max(v[*busy_begin].tick,block.tick);
                    const auto right=std::min(v[j].tick,deadline);
                    if(right>left){
                        overlap+=right-left;
                        callbacks.push_back(*busy_begin);
                        callbacks.push_back(j);
                    }
                    busy_begin.reset();
                }
            }
            if(busy_begin)ambiguous=true;
            if(!ambiguous && overlap>0 && overlap<=deadline-block.tick) {
                finding.assessment+=" Candidate holder busy-wait callback overlap: "+
                    std::to_string(overlap)+" / "+std::to_string(deadline-block.tick)+
                    " raw ticks within waiter blocked-to-ready; NOT CPU load or proven cause.";
                finding.evidence.insert(finding.evidence.end(),callbacks.begin(),callbacks.end());
                std::sort(finding.evidence.begin(),finding.evidence.end());
                finding.evidence.erase(std::unique(finding.evidence.begin(),finding.evidence.end()),finding.evidence.end());
            }
        }
        s.findings.push_back(std::move(finding));
        if(s.findings.size()>10000)throw std::runtime_error("W20 resource finding limit");
    }
}
} // namespace

std::string describe_field(const Event& e,const std::string& key){
    auto it=e.fields.find(key);
    return it==e.fields.end()?std::string{}:it->second;
}
Session open(const std::filesystem::path& directory,bool allow_incomplete_tail){
    if(std::filesystem::is_symlink(directory) || !std::filesystem::is_directory(directory))
        throw std::runtime_error("Open a local CTF directory with metadata and channel0_0");
    const auto meta=read_local(directory/"metadata",max_metadata);
    const auto defs=parse_metadata(meta);
    const auto buf=read_local(directory/"channel0_0",max_stream);
    if(buf.empty())throw std::runtime_error("Empty CTF channel");
    Session s;
    std::size_t offset=0;
    std::uint32_t previous=0;
    bool has_previous=false;
    std::uint64_t wraps=0;
    std::int64_t current_thread=-1;
    int isr_depth=0;
    while(offset<buf.size()){
        if(buf.size()-offset<6){
            if(allow_incomplete_tail){s.pending_bytes=buf.size()-offset;break;}
            throw std::runtime_error("Truncated CTF header");
        }
        const auto low=static_cast<std::uint32_t>(little(buf,offset,4));
        const auto id=static_cast<std::uint16_t>(little(buf,offset+4,2));
        auto it=defs.find(id);
        if(it==defs.end())throw std::runtime_error("Undeclared CTF event ID at byte "+std::to_string(offset));
        const auto& d=it->second;
        if(buf.size()-offset<d.record_bytes){
            if(allow_incomplete_tail){s.pending_bytes=buf.size()-offset;break;}
            throw std::runtime_error("Truncated CTF payload");
        }
        if(has_previous && low<previous){
            if(previous<0xF0000000u || low>0x0FFFFFFFu)
                throw std::runtime_error("CTF timestamp regression is not a valid wrap");
            ++wraps;
        }
        Event event;
        event.name=d.name;
        event.tick=(wraps<<32)|low;
        event.byte_offset=offset;
        event.actor=isr_depth>0?-1:current_thread;
        std::size_t at=offset+6;
        for(const auto& field:d.fields){
            event.fields.emplace(field.name,field_to_string(buf,at,field));
            at+=field.bytes;
        }
        const auto thread=describe_field(event,"thread_id");
        if(!thread.empty()){
            const auto n=number(thread);
            const auto name=describe_field(event,"name");
            if(!name.empty())s.thread_names[n]=name;
            if(d.name=="thread_switched_in") {
                current_thread=static_cast<std::int64_t>(n);
                event.actor=current_thread;
            } else if(d.name=="thread_switched_out") {
                event.actor=static_cast<std::int64_t>(n);
                current_thread=-1;
            }
        }
        if(d.name=="isr_enter")++isr_depth;
        if(d.name=="isr_exit" || d.name=="isr_exit_to_scheduler"){
            if(isr_depth>0)--isr_depth;
        }
        s.events.push_back(std::move(event));
        if(s.events.size()>1000000)throw std::runtime_error("CTF event count limit");
        offset+=d.record_bytes;
        previous=low;has_previous=true;
    }
    if(s.events.empty())throw std::runtime_error("CTF contains no complete events yet");
    s.bytes_consumed=offset;
    s.timestamp_wraps=static_cast<std::size_t>(wraps);
    s.complete_file=s.pending_bytes==0;
    find_resources(s);
    return s;
}
} // namespace observatory::ctf
