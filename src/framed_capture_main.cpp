// SPDX-License-Identifier: Apache-2.0
#include "observatory/framed_tcp.hpp"
#include <charconv>
#include <chrono>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <memory>
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
        std::string spool_path;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--help") {
                std::cout<<"rtos-observatory-framed-capture "
                         <<"--address 127.0.0.1 --port 12345 "
                         <<"[--max-bytes 1048576] [--max-frames 100000] "
                         <<"[--idle-ms 5000] [--duration-ms 30000] "
                         <<"[--spool NEW_BINARY_FILE]\n";
                return 0;
            }
            if(i+1>=argc)throw observatory::Error("missing W17 option value");
            const std::string value=argv[++i];
            if(arg=="--spool")spool_path=value;
            else if(arg=="--address")options.address=value;
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
        // Write a local append-only binary spool for opt-in GUI following.
        // No file overwrite and no automatic upload/stream into debugger.
        std::unique_ptr<std::ofstream> spool;
        if(!spool_path.empty()) {
            const std::filesystem::path path(spool_path);
            if(std::filesystem::exists(path) ||
               path.empty() || std::filesystem::is_symlink(path))
                throw observatory::Error("W18 spool output already exists or is unsafe");
            spool=std::make_unique<std::ofstream>(path,std::ios::binary|std::ios::out);
            if(!*spool)throw observatory::Error("cannot create W18 binary spool");
            options.on_bytes=[&](const std::uint8_t* bytes,std::size_t size) {
                spool->write(reinterpret_cast<const char*>(bytes),
                             static_cast<std::streamsize>(size));
                spool->flush();
                if(!*spool)throw observatory::Error("W18 binary spool write failed");
            };
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
        if(spool)spool->close();
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
