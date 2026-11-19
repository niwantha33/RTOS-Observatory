// SPDX-License-Identifier: Apache-2.0
#include "observatory/snapshot_receipt.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace observatory;
static const std::string sample=
    "RTOS-OBSERVATORY-APPROVED-SNAPSHOT\t1\tsession-A\t7\tapproval-A\tfixture\t32\tunverified\n"
    "TCB\tControl\t0x20000100\t4\tok\tdeadbeef\n"
    "MEM\tStack\t0x20002000\t4\tfailed\t-\n"
    "REG\tPC\t0x0\t0\tok\t0x08000400\n"
    "REG\tSP\t0x0\t0\tfailed\t-\n";
static SnapshotCapture parse(const std::string& data,SnapshotLimits l={}) {
    std::istringstream in(data);return parse_snapshot_receipt(in,l);
}
static bool rejects(const std::string& s,SnapshotLimits l={}) {
    try{(void)parse(s,l);}catch(const Error&){return true;}return false;
}
static std::string as_crlf(std::string s) {
    std::string out;
    for(const char c:s) {
        if(c=='\n')out+='\r';
        out+=c;
    }
    return out;
}
int main(int argc,char** argv) {
    const auto x=parse(sample);
    assert(x.request.session_nonce=="session-A");
    assert(x.request.epoch==7 && x.request.address_bits==32);
    assert(x.source_label=="fixture" && !x.all_cores_coherent);
    assert(x.samples.size()==4 && x.request.items.size()==4);
    assert(x.samples[0].success && x.samples[0].data.size()==4);
    assert(x.samples[0].data[0]==0xde && x.samples[0].data[3]==0xef);
    assert(!x.samples[1].success && x.samples[1].data.empty());
    assert(x.samples[2].register_value && *x.samples[2].register_value==0x08000400);
    assert(!x.samples[3].success && !x.samples[3].register_value);
    assert(verify_snapshot_capture(x));
    assert(rejects(""));
    assert(rejects(sample.substr(0,sample.size()-1)));
    assert(rejects(sample+"\n"));
    // Extra valid samples are allowed by v1 (up to max_items).
    const auto extra=sample+"REG\tLR\t0x0\t0\tok\t0x4\n";
    assert(parse(extra).samples.size()==5);
    assert(rejects(extra,SnapshotLimits{4,256,4096,16,30000}));
    // Git for Windows can convert the sample fixture to CRLF.
    assert(parse(as_crlf(sample)).samples.size()==4);
    assert(rejects(as_crlf(sample).substr(0,as_crlf(sample).size()-1)));
    assert(rejects(sample.substr(0,5)+"\r"+sample.substr(5)));
    assert(rejects(sample.substr(0,sample.size()-1)+"\r"));
    assert(rejects("RTOS-OBSERVATORY-APPROVED-SNAPSHOT\t2\tsession-A\t7\tapproval-A\tfixture\t32\tunverified\n"
                  "REG\tPC\t0x0\t0\tok\t0x10\n"));
    assert(rejects("RTOS-OBSERVATORY-APPROVED-SNAPSHOT\t1\tsession-A\t7\tapproval-A\tfixture\t32\tcertified\n"
                  "REG\tPC\t0x0\t0\tok\t0x10\n"));
    const auto header=sample.substr(0,sample.find('\n')+1);
    assert(rejects(header+"MEM\tStack\t0x40000000\t300\tok\t00\n"));
    assert(rejects(header+"MEM\tStack\t0xfffffff0\t32\tfailed\t-\n"));
    assert(rejects(header+"MEM\tStack\t0x20002000\t4\tok\t00\n"));
    assert(rejects(header+"MEM\tStack\t0x20002000\t4\tfailed\t01020304\n"));
    assert(rejects(header+"REG\tPC\t0x0\t1\tok\t0x10\n"));
    assert(rejects(header+"REG\tPC\t0x20\t0\tok\t0x10\n"));
    assert(rejects(header+"REG\tPC\t0x0\t0\tfailed\t0x10\n"));
    assert(rejects(header+"REG\tPC\t0x0\t0\tok\t-\n"));
    assert(rejects(header+"REG\tPC\t0x0\t0\tok\tinvalid\n"));
    assert(rejects(header+"MEM\tUnsafe\t0x20000000\t4\tok\tzzzzzzzz\n"));
    assert(rejects(header+"TCB\tbad/path\t0x20000000\t4\tfailed\t-\n"));
    assert(rejects(header+"DMA\tlabel\t0x20000000\t4\tfailed\t-\n"));
    assert(rejects(header+"MEM\tlabel\t0x20000000\t0\tfailed\t-\n"));
    assert(rejects(header+"MEM\tlabel\t0x20000000\t4294967296\tfailed\t-\n"));
    assert(rejects(header+"REG\tSP\t0x0\t0\tok\t0x10\n",
                   SnapshotLimits{32,256,4096,0,30000}));
    assert(rejects(header+"MEM\tStack\t0x20000000\t4\tfailed\t-\n",
                   SnapshotLimits{32,256,2,16,30000}));
    assert(rejects(sample+std::string("\0junk",5)));
    if(argc>1) {
        const auto path=std::filesystem::path(argv[1]);
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out<<sample;out.close();assert(out.good());
        assert(read_snapshot_receipt(path).samples.size()==4);
        std::filesystem::remove(path);
    }
    std::cout<<"W15 receipt unknown/read consistency and bounded malformed-input tests passed\n";
}
