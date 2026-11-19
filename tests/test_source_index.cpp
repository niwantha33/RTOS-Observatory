// SPDX-License-Identifier: Apache-2.0
#include "observatory/source_index.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace observatory;
namespace fs = std::filesystem;

static void write(const fs::path& p,const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p,std::ios::binary|std::ios::trunc);
    f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    f.close();
    assert(f.good());
}
static SourceIndex parse(const std::string& s,SymbolLimits l={}) {
    std::istringstream input(s);
    return parse_source_index(input,l);
}
static bool rejects(const std::string& s,SymbolLimits l={}) {
    try { (void)parse(s,l); }
    catch(const Error&) {return true;}
    return false;
}
int main(int argc,char** argv) {
    assert(hash_detail::sha256("")==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(hash_detail::sha256("abc")==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const fs::path root=argc>1 ? fs::path(argv[1]) :
                        fs::temp_directory_path()/"w13-source-symbol-tests";
    const fs::path elf=root/"firmware.elf";
    const fs::path sources=root/"sources";
    fs::create_directories(sources);
    std::string image(64,'\0');
    image[0]=static_cast<char>(0x7f);
    image[1]='E';image[2]='L';image[3]='F';
    image[4]=2;image[5]=1;image[6]=1; // ELF64 little-endian
    image[52]=64;  // e_ehsize, synthetic header only, NOT a linked firmware
    write(elf,image);
    const std::string c_source="void entry(void) {\n  handler();\n}\n";
    const std::string cpp_source="void handler() {}\n";
    const std::string rust_source="fn task() {}\n";
    write(sources/"src"/"app.c",c_source);
    write(sources/"src"/"handler.cpp",cpp_source);
    write(sources/"src"/"task.rs",rust_source);
    const auto e_hash=hash_detail::sha256(image);
    const auto fhash=hash_detail::sha256(c_source);
    const auto header=std::string("RTOS-OBSERVATORY-SYMBOLS\t1\t")+
                      e_hash+"\tsynthetic-fw\t64\n";
    const auto files=
        "FILE\tsrc/app.c\t"+fhash+"\n"+
        "FILE\tsrc/handler.cpp\t"+hash_detail::sha256(cpp_source)+"\n"+
        "FILE\tsrc/task.rs\t"+hash_detail::sha256(rust_source)+"\n";
    const auto funcs=std::string(
        "FUNC\tf1\tC\tentry\t0x1000\t0x40\tsrc/app.c\t1\n"
        "FUNC\tf2\tC++\thandler\t0x1100\t0x40\tsrc/handler.cpp\t1\n"
        "FUNC\tf3\tRust\ttask\t0x1200\t0x40\tsrc/task.rs\t1\n");
    const auto edges=std::string(
        "CALL\tf1\tf2\t2\n"
        "CALL\tf3\tf1\t1\n"
        "LOCK\tf2\tmutex:M1\tacquire\t1\n");
    const auto data=header+files+funcs+edges;
    const auto index=parse(data);
    assert(index.firmware_label=="synthetic-fw");
    assert(index.functions.size()==3 && index.calls.size()==2 && index.locks.size()==1);
    assert(index.address_bits==64 && index.source_sha256.size()==3);
    auto v=verify_source_index(index,elf,sources);
    assert(v.elf_matches && v.all_sources_match && !v.manifest_authenticated);
    assert(v.verified_sources.size()==3);
    auto at_pc=resolve_source_pc(index,v,0x1101);
    assert(at_pc.function_index && *at_pc.function_index==1);
    assert(!at_pc.ambiguous && at_pc.verified_elf_identity);
    assert(!resolve_source_pc(index,v,0x1140).function_index);
    const auto excerpt=read_verified_source_excerpt(index,v,0);
    assert(excerpt.path=="src/app.c" && excerpt.target_line==1);
    assert(excerpt.lines.size()==3 && excerpt.lines[1].second=="  handler();");

    auto overlap=parse(header+files+funcs+
        "FUNC\toverlap\tRust\tother\t0x1110\t0x10\tsrc/task.rs\t1\n"+edges);
    auto ambiguous=resolve_source_pc(overlap,v,0x1115);
    assert(!ambiguous.function_index && ambiguous.ambiguous);
    auto mismatch=v;
    mismatch.elf_matches=false;
    assert(!resolve_source_pc(index,mismatch,0x1101).function_index);
    bool refused=false;
    try {(void)read_verified_source_excerpt(index,mismatch,0);}
    catch(const Error&) {refused=true;}
    assert(refused);

    // Reject stale files on every source jump, not just once when importing.
    write(sources/"src"/"app.c",c_source+"// changed after verification\n");
    refused=false;
    try {(void)read_verified_source_excerpt(index,v,0);}
    catch(const Error&) {refused=true;}
    assert(refused);
    const auto changed=verify_source_index(index,elf,sources);
    assert(changed.elf_matches && !changed.all_sources_match);
    assert(!changed.verified_sources.contains("src/app.c"));
    write(sources/"src"/"app.c",c_source);

    image[28]=1; // Still a synthetic ELF header, but modified binary digest
    write(elf,image);
    const auto wrong=verify_source_index(index,elf,sources);
    assert(!wrong.elf_matches && wrong.all_sources_match);
    assert(!resolve_source_pc(index,wrong,0x1100).function_index);
    write(elf,std::string("not an ELF\n"));
    const auto invalid_elf=verify_source_index(index,elf,sources);
    assert(!invalid_elf.elf_matches);
    write(elf,image);

    const auto invalid=[
        &](const std::string& fragment) {return rejects(header+files+fragment);};
    assert(rejects(""));
    assert(rejects("RTOS-OBSERVATORY-SYMBOLS\t3\t"+e_hash+"\tfw\t64\n"));
    assert(rejects("RTOS-OBSERVATORY-SYMBOLS\t1\t"+e_hash+"\tfw\t16\n"));
    assert(rejects("RTOS-OBSERVATORY-SYMBOLS\t1\t"+std::string(64,'f')+"F\tfw\t64\n"));
    assert(invalid("FILE\tsrc/app.c\t"+fhash+"\n"));
    assert(invalid("FUNC\tf1\tGo\tfn\t0x1000\t0x40\tsrc/app.c\t1\n"));
    assert(invalid("FUNC\tf1\tC\tfn\t0x1000\t0x0\tsrc/app.c\t1\n"));
    assert(invalid("FUNC\tf1\tC\tfn\t0xfffffffffffffff0\t0x40\tsrc/app.c\t1\n"));
    assert(invalid("FUNC\tf1\tC\tfn\t0x1000\t0x40\tsrc/app.c\t0\n"));
    assert(invalid("FUNC\tf1\tC\tfn\t0x1000\t0x40\tsrc/app.c\t1\n"
                   "FUNC\tf1\tC\tfn2\t0x1100\t0x40\tsrc/app.c\t2\n"));
    assert(invalid("CALL\tmissing\tf1\t2\n"));
    assert(invalid("LOCK\tmissing\tM1\tacquire\t2\n"));
    assert(invalid("LOCK\tf1\tM1\tlock-guess\t2\n"));
    assert(rejects(header+"FILE\t../escape.c\t"+fhash+"\n"));
    assert(rejects(header+"FILE\tC:/Windows/system32\t"+fhash+"\n"));
    assert(rejects(header+"FILE\t/src/absolute.c\t"+fhash+"\n"));
    assert(rejects(header+"FILE\tsrc\\escape.c\t"+fhash+"\n"));
    auto tiny=SymbolLimits{};
    tiny.max_functions=1;
    assert(rejects(data,tiny));
    tiny=SymbolLimits{};
    tiny.max_manifest_bytes=30;
    assert(rejects(data,tiny));
    tiny=SymbolLimits{};
    tiny.max_line_bytes=30;
    assert(rejects(data,tiny));
    tiny=SymbolLimits{};
    tiny.max_calls=1;
    assert(rejects(data,tiny));
    // Relocated/copies outside the selected source root must not resolve.
    const auto missing_root=verify_source_index(index,elf,root/"missing");
    assert(!missing_root.all_sources_match);
    std::cout<<"W13 SHA-256, source/call/lock graph, verified source navigation and fail-closed identity tests passed\n";
}
