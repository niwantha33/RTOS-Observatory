// SPDX-License-Identifier: Apache-2.0
#include "observatory/elf_intelligence.hpp"
#include "observatory/engine.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace observatory;

static void put(std::vector<std::uint8_t>& b, std::size_t at,
                std::uint64_t n, unsigned width) {
    assert(at + width <= b.size());
    for (unsigned i = 0; i < width; ++i)
        b[at + i] = static_cast<std::uint8_t>(n >> (i * 8u));
}
static void copy(std::vector<std::uint8_t>& b, std::size_t at, std::string_view s) {
    assert(at + s.size() <= b.size());
    for (std::size_t i = 0; i < s.size(); ++i)
        b[at + i] = static_cast<std::uint8_t>(s[i]);
}
static std::uint32_t append(std::string& table, std::string_view name) {
    const auto offset = static_cast<std::uint32_t>(table.size());
    table.append(name);
    table += '\0';
    return offset;
}
static void ident(std::vector<std::uint8_t>& b, std::uint8_t elf_class,
                  std::uint16_t machine) {
    b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
    b[4] = elf_class; b[5] = 1; b[6] = 1;
    put(b, 16, 2, 2);       // ET_EXEC
    put(b, 18, machine, 2);
    put(b, 20, 1, 4);
}
static std::vector<std::uint8_t> elf64() {
    std::vector<std::uint8_t> b(1024);
    ident(b, 2, 62);
    put(b, 40, 0x200, 8); put(b, 52, 64, 2); // ELF and section headers
    put(b, 58, 64, 2); put(b, 60, 7, 2); put(b, 62, 3, 2);
    std::string strings(1, '\0');
    const auto foo = append(strings, "foo"), bar = append(strings, "bar");
    copy(b, 0x80, strings);
    // Three ELF64 symbol entries, including mandatory null symbol.
    put(b, 0xa0 + 24, foo, 4);
    put(b, 0xa0 + 28, 0x12, 1); // global function
    put(b, 0xa0 + 30, 5, 2);    // .text
    put(b, 0xa0 + 32, 0x1000, 8);
    put(b, 0xa0 + 40, 16, 8);
    put(b, 0xa0 + 48, bar, 4);
    put(b, 0xa0 + 52, 0x12, 1);
    put(b, 0xa0 + 54, 5, 2);
    put(b, 0xa0 + 56, 0x1008, 8); // deliberately overlaps foo
    put(b, 0xa0 + 64, 16, 8);
    std::string names(1, '\0');
    const auto str = append(names, ".strtab");
    const auto sym = append(names, ".symtab");
    const auto shstr = append(names, ".shstrtab");
    const auto note = append(names, ".note.gnu.build-id");
    const auto text = append(names, ".text");
    const auto debug = append(names, ".debug_info");
    copy(b, 0x100, names);
    put(b, 0x160, 4, 4); put(b, 0x164, 4, 4); put(b, 0x168, 3, 4);
    copy(b, 0x16c, std::string_view("GNU\0", 4));
    b[0x170] = 1; b[0x171] = 2; b[0x172] = 3; b[0x173] = 4;
    const auto sh = [&](unsigned index, std::uint32_t name, unsigned type,
                        std::uint64_t offset, std::uint64_t size,
                        unsigned link = 0, unsigned ent = 0) {
        const auto at = 0x200 + index * 64u;
        put(b, at, name, 4); put(b, at + 4, type, 4);
        put(b, at + 24, offset, 8); put(b, at + 32, size, 8);
        put(b, at + 40, link, 4); put(b, at + 56, ent, 8);
    };
    sh(1, str, 3, 0x80, strings.size());
    sh(2, sym, 2, 0xa0, 72, 1, 24);
    sh(3, shstr, 3, 0x100, names.size());
    sh(4, note, 7, 0x160, 20);
    sh(5, text, 1, 0x190, 16);
    sh(6, debug, 1, 0x1a0, 4);
    return b;
}
static std::vector<std::uint8_t> elf32() {
    std::vector<std::uint8_t> b(512);
    ident(b, 1, 40);
    put(b, 32, 0x100, 4); put(b, 40, 52, 2);
    put(b, 46, 40, 2); put(b, 48, 5, 2); put(b, 50, 3, 2);
    std::string strings(1, '\0');
    const auto task = append(strings, "task");
    copy(b, 0x60, strings);
    put(b, 0x70 + 16, task, 4); put(b, 0x70 + 20, 0x08001235, 4);
    put(b, 0x70 + 24, 16, 4); put(b, 0x70 + 28, 0x12, 1);
    put(b, 0x70 + 30, 4, 2);
    std::string names(1, '\0');
    const auto str = append(names, ".strtab");
    const auto sym = append(names, ".symtab");
    const auto shstr = append(names, ".shstrtab");
    const auto text = append(names, ".text");
    copy(b, 0x90, names);
    const auto sh = [&](unsigned index, std::uint32_t name, unsigned type,
                        unsigned offset, unsigned size, unsigned link = 0,
                        unsigned entsize = 0) {
        const auto at = 0x100 + index * 40u;
        put(b, at, name, 4); put(b, at + 4, type, 4);
        put(b, at + 16, offset, 4); put(b, at + 20, size, 4);
        put(b, at + 24, link, 4); put(b, at + 36, entsize, 4);
    };
    sh(1, str, 3, 0x60, strings.size());
    sh(2, sym, 2, 0x70, 32, 1, 16);
    sh(3, shstr, 3, 0x90, names.size());
    sh(4, text, 1, 0xd0, 16);
    return b;
}
static bool rejects(const std::vector<std::uint8_t>& b, ElfLimits limits = {}) {
    try { (void)parse_elf_image(b, limits); }
    catch (const Error&) { return true; }
    return false;
}
int main(int argc, char** argv) {
    const auto fixture = elf64();
    const auto image = parse_elf_image(fixture);
    assert(image.address_bits == 64 && image.architecture == "x86_64");
    assert(!image.sha256.empty() && image.sha256.size() == 64);
    assert(image.build_id == "01020304");
    assert(image.has_dwarf_sections); // Presence only; no DWARF decoding.
    assert(image.functions.size() == 2);
    assert(image.functions[0].name == "foo");
    assert(image.functions[1].name == "bar");
    assert(lookup_elf_pc(image, 0x1002).index == 0);
    assert(lookup_elf_pc(image, 0x1009).ambiguous);
    assert(!lookup_elf_pc(image, 0x2000).index);

    const auto arm = parse_elf_image(elf32());
    assert(arm.address_bits == 32 && arm.architecture == "ARM");
    assert(arm.functions.size() == 1 && arm.functions[0].name == "task");
    assert(arm.functions[0].address == 0x08001234);
    assert(arm.functions[0].thumb_symbol);
    assert(lookup_elf_pc(arm, 0x08001235).index == 0);
    assert(lookup_elf_pc(arm, 0x08001235).thumb_normalized);
    assert(!lookup_elf_pc(arm, 0x08001244).index);

    auto dyn = fixture;
    put(dyn, 16, 3, 2); // ET_DYN: symbol addresses need an observed load base.
    const auto pie = parse_elf_image(dyn);
    assert(pie.requires_load_bias && !lookup_elf_pc(pie, 0x1002).index);
    auto bad = fixture;
    bad[5] = 2;
    assert(rejects(bad)); // Unsupported big-endian.
    bad = fixture;
    put(bad, 18, 999, 2);
    assert(rejects(bad)); // Unsupported architecture.
    bad = fixture;
    bad.resize(0x250);
    assert(rejects(bad)); // Truncated section table.
    bad = fixture;
    put(bad, 0x200 + 2 * 64 + 40, 77, 4);
    assert(rejects(bad)); // Out-of-range linked string table.
    bad = fixture;
    put(bad, 0x200 + 2 * 64 + 56, 0, 8);
    assert(rejects(bad)); // Zero symbol entry size.
    bad = fixture;
    put(bad, 0x200 + 2 * 64 + 24, 0xffffffffffull, 8);
    assert(rejects(bad)); // Offset overflow.
    bad = fixture;
    put(bad, 0xa0 + 40, UINT64_MAX, 8);
    assert(rejects(bad)); // Address plus size overflow.
    ElfLimits limits;
    limits.max_functions = 1;
    assert(rejects(fixture, limits));
    limits = {};
    limits.max_file_bytes = fixture.size() - 1;
    assert(rejects(fixture, limits));
    bad = fixture;
    put(bad, 40, 0, 8); put(bad, 60, 0, 2); put(bad, 62, 0, 2);
    assert(parse_elf_image(bad).functions.empty()); // Stripped/no sections.

    if (argc > 1) {
        const fs::path path = fs::path(argv[1]) / "elf01-test.elf";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(fixture.data()),
                  static_cast<std::streamsize>(fixture.size()));
        out.close();
        assert(out.good());
        const auto disk = inspect_elf_file(path);
        assert(disk.sha256 == image.sha256 && disk.functions.size() == 2);
        fs::remove(path);
    }
    std::cout << "ELF native symbol regression tests passed\n";
}
