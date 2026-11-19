// SPDX-License-Identifier: Apache-2.0
#include "observatory/framed_tcp.hpp"
#include <charconv>
#include <chrono>
#include <iostream>
#include <string>
namespace {
unsigned number(const std::string& s) {
    unsigned n{};
    const auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(s.empty() || ec!=std::errc{} || end!=s.data()+s.size() || !n)
        throw observatory::Error("invalid positive integer: "+s);
    return n;
}
}
int main(int argc,char** argv) {
    try {
        observatory::FramedTcpOptions options;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--help") {
                std::cout<<"rtos-observatory-framed-capture "
                         <<"--address 127.0.0.1 --port 12345 "
                         <<"[--max-bytes 1048576] [--max-frames 100000] "
                         <<"[--idle-ms 5000] [--duration-ms 30000]\n";
                return 0;
            }
            if(i+1>=argc)throw observatory::Error("missing W17 option value");
            const std::string value=argv[++i];
            if(arg=="--address")options.address=value;
            else if(arg=="--port") {
                const auto port=number(value);
                if(port>65535u)throw observatory::Error("invalid port");
                options.port=static_cast<unsigned short>(port);
            } else if(arg=="--max-bytes")options.max_bytes=number(value);
            else if(arg=="--max-frames")options.max_frames=number(value);
            else if(arg=="--idle-ms")
                options.idle_timeout=std::chrono::milliseconds(number(value));
            else if(arg=="--duration-ms")
                options.total_timeout=std::chrono::milliseconds(number(value));
            else throw observatory::Error("unknown W17 option: "+arg);
        }
        asio::io_context io;
        observatory::FramedTcpResult outcome;
        bool called=false;
        const auto capture=observatory::FramedTcpCapture::create(
            io,options,[&](observatory::FramedTcpResult received) {
                outcome=std::move(received);
                called=true;
            });
        capture->start();
        io.run(); // one strand; no background/remote debugger work
        if(!called || !outcome.clean_eof || !outcome.error.empty())
            throw observatory::Error(called?outcome.error:"capture did not complete");
        const bool incomplete=outcome.crc_errors || outcome.invalid_frames ||
            outcome.replayed_frames || outcome.missing_frames ||
            outcome.missing_attempts || outcome.epoch_changes ||
            outcome.loss_saturated;
        std::cout<<"W17 frames="<<outcome.frames<<" events="<<outcome.events
                 <<" stats="<<outcome.stats<<" bytes="<<outcome.bytes
                 <<" crc_errors="<<outcome.crc_errors
                 <<" invalid="<<outcome.invalid_frames
                 <<" replayed="<<outcome.replayed_frames
                 <<" missing_frames="<<outcome.missing_frames
                 <<" missing_attempts="<<outcome.missing_attempts
                 <<" epoch_changes="<<outcome.epoch_changes
                 <<" evidence="<<(incomplete?"INCOMPLETE":"NO_OBSERVED_LOSS")
                 <<'\n';
        return incomplete?3:0;
    } catch(const std::exception& e) {
        std::cerr<<"W17 framed capture failed: "<<e.what()<<'\n';
        return 1;
    }
}
