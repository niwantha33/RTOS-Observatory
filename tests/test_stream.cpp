// SPDX-License-Identifier: Apache-2.0
#include "observatory/session.hpp"
#include <iostream>
#include <sstream>
#include <source_location>
using namespace observatory;
void require(bool v, std::source_location at=std::source_location::current()) {if(!v) throw Error("stream assertion failed at "+std::to_string(at.line()));}
template<class F> void rejects(F f) {bool bad=false;try {f();} catch(const Error&){bad=true;}require(bad);}
const std::string header="RTOS-OBSERVATORY\t1\tbuild-a\tzephyr\tmono\t1000000\n";
const std::string rows="10\t0\tSESSION\t-\tsynthetic\t0\n# provenance comment\n11\t0\tCREATE\tworker\t-\t5\n13\t10\tSWITCH_IN\tworker\tcpu0\t0\n14\t20\tSWITCH_OUT\tworker\tcpu0\t0\n15\t21\tDROP\t-\t-\t2\n";
int main() {
    try {
        const auto raw=header+rows;
        std::istringstream canonical("0\tSESSION\t-\tsynthetic\t0\n0\tCREATE\tworker\t-\t5\n10\tDROP\t-\t-\t1\n10\tSWITCH_IN\tworker\tcpu0\t0\n20\tSWITCH_OUT\tworker\tcpu0\t0\n21\tDROP\t-\t-\t2\n");
        auto reference=parse_trace(canonical);
        const std::size_t physical_lines[]={2,4,5,5,6,7};
        for(std::size_t i=0;i<reference.events.size();++i) reference.events[i].line=physical_lines[i];
        const auto expected=report_json(analyze(reference));
        for(std::size_t split=0;split<=raw.size();++split) {
            UniversalStream stream;
            stream.feed(std::string_view(raw).substr(0,split));
            stream.feed(std::string_view(raw).substr(split));
            auto p=stream.progress();
            require(p.bytes==raw.size() && p.events==6 && p.last_timestamp_us==21 && p.missing_events==1 && p.declared_drops==2);
            auto trace=stream.finish(true);
            require(report_json(analyze(trace.capture))==expected);
            require(trace.capture.events[1].line==4 && trace.capture.events[2].line==5);
            require(trace.evidence.first_sequence==10 && trace.evidence.last_sequence==15);
            rejects([&]{stream.feed("x");});rejects([&]{stream.finish();});
        }
        UniversalStream single;
        for(char ch:raw) single.feed(std::string_view(&ch,1));
        require(report_json(analyze(single.finish().capture))==expected);
        auto crlf=std::string{};for(char c:raw) {if(c=='\n') crlf+='\r';crlf+=c;}
        UniversalStream windows;windows.feed(crlf);require(report_json(analyze(windows.finish(true).capture))==expected);
        auto no_newline=raw.substr(0,raw.size()-1);
        UniversalStream offline;offline.feed(no_newline);require(report_json(analyze(offline.finish().capture))==expected);
        UniversalStream live;live.feed(no_newline);rejects([&]{live.finish(true);});rejects([&]{live.feed("\n");});
        for(const auto& bad: {header+"1\t0\tCREATE\tw\t-\t0\n",header+"0\t0\tSESSION\t-\ts\t0\n0\t1\tCREATE\tw\t-\t2\n",header+"0\t0\tSESSION\t-\ts\t0\n1\t4\tCREATE\tw\t-\t2\n2\t3\tPRIORITY\tw\t-\t2\n",header+"0\t0\tSESSION\t-\ts\t0\n1\t0\tBOGUS\t-\t-\t0\n"}) {
            UniversalStream stream;rejects([&]{stream.feed(bad);});rejects([&]{stream.finish();});
        }
        UniversalStream bytes(Limits{10,100,4096});rejects([&]{bytes.feed(header);});
        UniversalStream line(Limits{10000,100,8});rejects([&]{line.feed("123456789");});
        UniversalStream events(Limits{10000,2,4096});rejects([&]{events.feed(raw);});
        UniversalStream empty;rejects([&]{empty.finish();});
        UniversalStream partial;partial.feed(header);partial.feed("10\t0\tSESSION");
        require(partial.progress().header_valid && partial.progress().events==0 && partial.progress().pending_line_bytes>0);
        std::cout<<"Incremental stream tests passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
