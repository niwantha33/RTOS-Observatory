// SPDX-License-Identifier: Apache-2.0
#include "observatory/zephyr_ctf.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace observatory::ctf;
namespace {
void put32(std::string& b,std::uint32_t x){for(int i=0;i<4;++i)b+=static_cast<char>((x>>(8*i))&255u);}
void put16(std::string& b,std::uint16_t x){for(int i=0;i<2;++i)b+=static_cast<char>((x>>(8*i))&255u);}
void event(std::string& b,std::uint32_t tick,std::uint16_t id,std::uint32_t arg){
    put32(b,tick);put16(b,id);put32(b,arg);
}
void save(const std::filesystem::path& dir,const std::string& channel){
    const char* m=R"(/* CTF 1.8 */
typealias integer { size = 16; align = 8; signed = false; } := uint16_t;
typealias integer { size = 32; align = 8; signed = false; } := uint32_t;
struct event_header { uint32_t timestamp; uint16_t id; };
trace { major = 1; minor = 8; byte_order = le; };
stream { event.header := struct event_header; };
event {
 name = thread_switched_in;
 id = 0x10;
 fields := struct { uint32_t thread_id; };
};
event {
 name = thread_switched_out;
 id = 0x11;
 fields := struct { uint32_t thread_id; };
};
event {
 name = semaphore_take_blocking;
 id = 0x25;
 fields := struct { uint32_t id; };
};
event {
 name = semaphore_give_enter;
 id = 0x22;
 fields := struct { uint32_t id; };
};
event {
 name = semaphore_take_exit;
 id = 0x26;
 fields := struct { uint32_t id; };
};
)";
    std::ofstream f(dir/"metadata",std::ios::binary);f<<m;f.close();
    std::ofstream o(dir/"channel0_0",std::ios::binary);o.write(channel.data(),static_cast<std::streamsize>(channel.size()));
}
}
int main(int argc,char** argv){
    assert(argc==2);
    auto dir=std::filesystem::path(argv[1])/"ctf-test-synthetic";
    std::filesystem::create_directories(dir);
    std::string bytes;
    event(bytes,100,0x10,42); // waiter running
    event(bytes,110,0x25,73); // waiter blocked on semaphore
    event(bytes,120,0x11,42);
    event(bytes,150,0x10,7); // producer running
    event(bytes,160,0x22,73); // same resource signalled
    event(bytes,180,0x11,7);
    event(bytes,190,0x10,42);
    event(bytes,205,0x26,73); // missing ret in fixture => incomplete semantics
    save(dir,bytes);
    auto s=open(dir);
    assert(s.complete_file && s.events.size()==8);
    assert(s.findings.size()==1 && s.findings[0].kind=="Semaphore");
    assert(s.findings[0].evidence.size()==3);
    assert(!s.findings[0].complete);
    assert(s.findings[0].start_tick==110 && s.findings[0].end_tick==205);
    assert(s.events[1].actor==42 && s.events[4].actor==7);
    std::string incomplete=bytes.substr(0,bytes.size()-2);
    save(dir,incomplete);
    bool threw=false;
    try{(void)open(dir);}catch(const std::runtime_error&){threw=true;}
    assert(threw);
    auto tail=open(dir,true);
    assert(tail.events.size()==7 && tail.pending_bytes==8 && !tail.complete_file);
    std::string unknown=bytes;
    unknown[4]=static_cast<char>(0xF1); unknown[5]=static_cast<char>(0xFF);
    save(dir,unknown);
    threw=false;try{(void)open(dir,true);}catch(const std::runtime_error&){threw=true;}
    assert(threw);
    std::string regress=bytes;
    regress[10]=0; regress[11]=0; regress[12]=0; regress[13]=0; // event 2 tick=0, not wrap
    save(dir,regress);
    threw=false;try{(void)open(dir);}catch(const std::runtime_error&){threw=true;}
    assert(threw);
    std::filesystem::remove_all(dir);
    return 0;
}
