// SPDX-License-Identifier: Apache-2.0
#include "observatory/session.hpp"
#include "observatory/elf_intelligence.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--elf-inspect") {
        if (argc != 3) {
            std::cerr << "Usage: rtos-observatory --elf-inspect firmware.elf\n";
            return 2;
        }
        try {
            const auto image = observatory::inspect_elf_file(argv[2]);
            std::cout << image.architecture << " / " << image.address_bits
                      << "-bit | functions=" << image.functions.size()
                      << " | DWARF present=" << (image.has_dwarf_sections ? "yes" : "no")
                      << " (not decoded)\nSHA-256: " << image.sha256
                      << "\nGNU build-id: " << (image.build_id.empty() ? "not found" : image.build_id)
                      << "\n";
            for (std::size_t i = 0; i < image.functions.size() && i < 100; ++i) {
                const auto& fn = image.functions[i];
                std::cout << "0x" << std::hex << fn.address << std::dec
                          << " +" << fn.size << " " << fn.name << "\n";
            }
            if (image.functions.size() > 100)
                std::cout << "(display capped at 100; native GUI lists all symbols)\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "ELF inspection failed: " << e.what() << "\n";
            return 1;
        }
    }
    std::string input_path, output_path, session_path, replay_path, decoder="legacy-tsv";
    for (int i=1;i<argc;++i) {
        std::string arg=argv[i];
        if (arg=="--help" || arg=="-h") {
            std::cout << "RTOS Observatory 0.2.0\n"
              "--input trace --output report.json [--decoder legacy-tsv|universal-v1] [--store session-dir]\n"
              "--replay session-dir --output report.json\n"
              "--elf-inspect firmware.elf\n"; return 0;
        }
        if (i+1>=argc) { std::cerr<<"Missing value\n";return 2; }
        if (arg=="--input") input_path=argv[++i];
        else if(arg=="--output") output_path=argv[++i];
        else if(arg=="--decoder") decoder=argv[++i];
        else if(arg=="--store") session_path=argv[++i];
        else if(arg=="--replay") replay_path=argv[++i];
        else {std::cerr<<"Unknown option: "<<arg<<'\n';return 2;}
    }
    try {
        if(output_path.empty() || (input_path.empty()==replay_path.empty()) || (!replay_path.empty()&&!session_path.empty())) throw observatory::Error("specify input or replay, and output");
        auto out=std::filesystem::weakly_canonical(output_path);
        if(!input_path.empty() && out==std::filesystem::weakly_canonical(input_path)) throw observatory::Error("output aliases input");
        auto session=!replay_path.empty()?replay_path:session_path;
        if(!session.empty()) {
            auto base=std::filesystem::weakly_canonical(session);
            auto relative=out.lexically_relative(base);
            if(relative.empty() || (!relative.empty() && *relative.begin()!="..")) throw observatory::Error("output must be outside session directory");
        }
        observatory::DecodedTrace trace;
        if(!replay_path.empty()) trace=observatory::replay_session(replay_path);
        else {
            std::ifstream input(input_path,std::ios::binary);
            if(!input) throw observatory::Error("cannot open input");
            if(!session_path.empty()) {
                observatory::store_session(session_path,decoder,input);
                trace=observatory::replay_session(session_path);
            } else trace=observatory::default_decoders().decode(decoder,input);
        }
        auto report=observatory::analyze(trace.capture);
        std::ofstream output(output_path,std::ios::binary|std::ios::trunc);
        output<<observatory::report_json(report); output.close();
        if(!output) throw observatory::Error("cannot write report");
        std::cout<<"Session: "<<report.session<<", events: "<<trace.capture.events.size()<<", firmware: "<<trace.identity.firmware<<", missing: "<<trace.evidence.missing_events<<", complete: "<<(report.complete?"yes":"no")<<'\n';
        return report.complete?0:3;
    } catch(const std::exception& e) {std::cerr<<"Analysis failed: "<<e.what()<<'\n';return 1;}
}
