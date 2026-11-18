// SPDX-License-Identifier: Apache-2.0
#include "observatory/live.hpp"
#include <charconv>
#include <iostream>
#include <sstream>
#include <thread>
namespace {
unsigned number(const std::string& s) {
    unsigned n{};auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(ec!=std::errc{} || end!=s.data()+s.size() || !n) throw observatory::Error("invalid positive number: "+s);
    return n;
}
}
int main(int argc,char** argv) {
    try {
        observatory::TcpCaptureOptions options;
        std::string session;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--help") {
                std::cout<<"rtos-observatory-capture --address 127.0.0.1 --port 12345 --store new-session [--idle-ms 5000] [--duration-ms 30000] [--max-bytes 67108864]\n";
                return 0;
            }
            if(i+1>=argc) throw observatory::Error("missing option value");
            std::string value=argv[++i];
            if(arg=="--address") options.address=value;
            else if(arg=="--port") {auto p=number(value);if(p>65535) throw observatory::Error("port out of range");options.port=static_cast<unsigned short>(p);}
            else if(arg=="--store") session=value;
            else if(arg=="--idle-ms") options.idle_timeout=std::chrono::milliseconds(number(value));
            else if(arg=="--duration-ms") options.total_timeout=std::chrono::milliseconds(number(value));
            else if(arg=="--max-bytes") options.limits.max_bytes=number(value);
            else throw observatory::Error("unknown option: "+arg);
        }
        if(session.empty()) throw observatory::Error("--store is required");
        if(std::filesystem::exists(session)) throw observatory::Error("session directory already exists");
        asio::io_context io;
        observatory::CaptureResult result;
        auto capture=observatory::TcpCapture::create(io,options,[&](auto r){result=std::move(r);});
        capture->start();
        // Demonstrates that a session remains serialized with two host workers.
        std::jthread worker([&]{io.run();});
        io.run();worker.join();
        if(!result.clean_eof || !result.error.empty()) throw observatory::Error(result.error);
        std::istringstream raw(result.raw);
        observatory::store_session(session,"universal-v1",raw,options.limits);
        auto trace=observatory::replay_session(session,options.limits);
        auto report=observatory::analyze(trace.capture);
        std::cout<<"Stored "<<trace.capture.events.size()<<" normalized events, "<<result.raw.size()
                 <<" raw bytes; sequence gaps: "<<trace.evidence.missing_events
                 <<"; complete: "<<(report.complete?"yes":"no")<<'\n';
        return report.complete?0:3;
    } catch(const std::exception& e) {std::cerr<<"Capture failed: "<<e.what()<<'\n';return 1;}
}
