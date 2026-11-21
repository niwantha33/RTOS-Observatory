// SPDX-License-Identifier: Apache-2.0
#include "observatory/elf_intelligence.hpp"
#include "observatory/engine.hpp"
#include "observatory/sha256.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

namespace observatory {
namespace {
constexpr std::uint32_t SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_NOTE = 7,
                        SHT_NOBITS = 8, SHT_DYNSYM = 11;
constexpr std::uint16_t SHN_UNDEF = 0;
constexpr std::uint8_t STT_FUNC = 2, STT_GNU_IFUNC = 10;

struct Reader {
    const std::vector<std::uint8_t>& data;
    void require(std::uint64_t at, std::uint64_t length) const {
        if (at > data.size() || length > data.size() - at)
            throw Error("ELF: offset/length outside file");
    }
    std::uint8_t u8(std::uint64_t at) const {
        require(at, 1);
        return data[static_cast<std::size_t>(at)];
    }
    std::uint64_t number(std::uint64_t at, unsigned n) const {
        require(at, n);
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= static_cast<std::uint64_t>(data[static_cast<std::size_t>(at + i)])
                 << (8u * i);
        return v;
    }
    std::uint16_t u16(std::uint64_t at) const {
        return static_cast<std::uint16_t>(number(at, 2));
    }
    std::uint32_t u32(std::uint64_t at) const {
        return static_cast<std::uint32_t>(number(at, 4));
    }
    std::uint64_t u64(std::uint64_t at) const { return number(at, 8); }
};

struct Section {
    std::uint32_t name{}, type{}, link{};
    std::uint64_t offset{}, size{}, entry_size{}, flags{};
};
std::string text_at(const Reader& r, const Section& table,
                    std::uint64_t offset, std::size_t max_name) {
    if (offset >= table.size)
        throw Error("ELF: invalid string table offset");
    const auto available = table.size - offset;
    const auto length = std::min<std::uint64_t>(available, max_name + 1ull);
    const auto base = table.offset + offset;
    r.require(base, length);
    std::size_t count = 0;
    while (count < length && r.u8(base + count) != 0)
        ++count;
    if (count == length || count > max_name)
        throw Error("ELF: unterminated or overlong string");
    return std::string(reinterpret_cast<const char*>(
        r.data.data() + static_cast<std::size_t>(base)), count);
}
std::string architecture(std::uint16_t machine) {
    switch (machine) {
        case 3: return "x86";
        case 8: return "MIPS";
        case 20: return "PowerPC";
        case 40: return "ARM";
        case 62: return "x86_64";
        case 94: return "Xtensa";
        case 183: return "AArch64";
        case 243: return "RISC-V";
        default: throw Error("ELF: unsupported machine architecture");
    }
}
std::uint64_t round4(std::uint64_t n) { return (n + 3u) & ~std::uint64_t{3}; }
void note_build_id(const Reader& r, const Section& section, std::string& id) {
    // GNU build-id note is metadata; it is not proof that trace and ELF match.
    std::uint64_t cursor = section.offset;
    const auto end = section.offset + section.size;
    constexpr char hex[] = "0123456789abcdef";
    while (cursor < end) {
        if (end - cursor < 12) {
            // Section padding is permitted only when all remaining bytes are zero.
            for (; cursor < end; ++cursor)
                if (r.u8(cursor) != 0) throw Error("ELF: malformed note padding");
            break;
        }
        const auto namesz = r.u32(cursor), descsz = r.u32(cursor + 4);
        const auto type = r.u32(cursor + 8);
        cursor += 12;
        const auto name_padded = round4(namesz), desc_padded = round4(descsz);
        if (name_padded > end - cursor)
            throw Error("ELF: note name exceeds section");
        const auto name_at = cursor;
        cursor += name_padded;
        if (desc_padded > end - cursor)
            throw Error("ELF: note data exceeds section");
        if (type == 3 && namesz == 4 &&
            r.u8(name_at) == 'G' && r.u8(name_at + 1) == 'N' &&
            r.u8(name_at + 2) == 'U' && r.u8(name_at + 3) == 0) {
            if (descsz == 0 || descsz > 128)
                throw Error("ELF: invalid GNU build-id size");
            std::string value;
            value.reserve(2 * descsz);
            for (std::uint64_t i = 0; i < descsz; ++i) {
                const auto b = r.u8(cursor + i);
                value += hex[b >> 4];
                value += hex[b & 15];
            }
            if (!id.empty() && id != value)
                throw Error("ELF: conflicting GNU build-id notes");
            id = std::move(value);
        }
        cursor += desc_padded;
    }
}

} // namespace

ElfImage parse_elf_image(const std::vector<std::uint8_t>& bytes, ElfLimits limits) {
    if (!limits.max_file_bytes || !limits.max_sections ||
        !limits.max_symbols_scanned || !limits.max_functions ||
        !limits.max_name_bytes || limits.max_name_bytes > 65536)
        throw Error("ELF: invalid inspection limits");
    if (bytes.size() > limits.max_file_bytes)
        throw Error("ELF: image exceeds permitted size");
    const Reader r{bytes};
    r.require(0, 16);
    if (r.u8(0) != 0x7f || r.u8(1) != 'E' || r.u8(2) != 'L' ||
        r.u8(3) != 'F')
        throw Error("ELF: magic not found");
    const auto elf_class = r.u8(4);
    if (elf_class != 1 && elf_class != 2)
        throw Error("ELF: unsupported word size");
    if (r.u8(5) != 1 || r.u8(6) != 1)
        throw Error("ELF: only little-endian ELF v1 supported");
    const bool is64 = elf_class == 2;
    r.require(0, is64 ? 64 : 52);
    if (r.u32(20) != 1 || r.u16(is64 ? 52 : 40) != (is64 ? 64 : 52))
        throw Error("ELF: malformed header");
    const auto type = r.u16(16);
    if (type != 1 && type != 2 && type != 3)
        throw Error("ELF: unsupported file type");
    ElfImage result;
    result.machine = r.u16(18);
    result.architecture = architecture(result.machine);
    result.address_bits = is64 ? 64u : 32u;
    result.is_relocatable = type == 1;
    result.requires_load_bias = type == 3;
    result.sha256 = hash_detail::sha256(std::string_view(
        reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const auto section_offset = is64 ? r.u64(40) : r.u32(32);
    const auto section_entry_size = r.u16(is64 ? 58 : 46);
    const auto section_count = r.u16(is64 ? 60 : 48);
    const auto section_names_index = r.u16(is64 ? 62 : 50);
    const auto required_entry_size = is64 ? 64u : 40u;
    if (section_count == 0) {
        if (section_offset != 0)
            throw Error("ELF: extended section count not supported");
        if (section_names_index != 0)
            throw Error("ELF: section name table without sections");
        return result; // Valid stripped ELF with no section table.
    }
    if (section_count > limits.max_sections ||
        section_entry_size != required_entry_size ||
        section_names_index == 0xffffu)
        throw Error("ELF: unsupported or excessive section table");
    if (section_names_index >= section_count && section_names_index != 0)
        throw Error("ELF: invalid section-name index");
    r.require(section_offset,
              static_cast<std::uint64_t>(section_count) * section_entry_size);

    std::vector<Section> sections;
    sections.reserve(section_count);
    for (std::size_t i = 0; i < section_count; ++i) {
        const auto at = section_offset + i * section_entry_size;
        Section sec;
        sec.name = r.u32(at);
        sec.type = r.u32(at + 4);
        sec.flags = is64 ? r.u64(at + 8) : r.u32(at + 8);
        sec.offset = is64 ? r.u64(at + 24) : r.u32(at + 16);
        sec.size = is64 ? r.u64(at + 32) : r.u32(at + 20);
        sec.link = r.u32(at + (is64 ? 40 : 24));
        sec.entry_size = is64 ? r.u64(at + 56) : r.u32(at + 36);
        if (sec.type != SHT_NOBITS && sec.type != 0)
            r.require(sec.offset, sec.size);
        sections.push_back(sec);
    }
    if (section_names_index != 0) {
        const auto& names = sections[section_names_index];
        if (names.type != SHT_STRTAB)
            throw Error("ELF: section-name table is not a string table");
        for (const auto& sec : sections) {
            const auto name = text_at(r, names, sec.name, limits.max_name_bytes);
            if (name.starts_with(".debug_") || name.starts_with(".zdebug_"))
                result.has_dwarf_sections = true;
        }
    }
    std::size_t scanned = 0;
    for (const auto& section : sections) {
        if (section.type == SHT_NOTE) {
            note_build_id(r, section, result.build_id);
            continue;
        }
        if (section.type != SHT_SYMTAB && section.type != SHT_DYNSYM)
            continue;
        if (section.link >= sections.size() ||
            sections[section.link].type != SHT_STRTAB)
            throw Error("ELF: symbol table has invalid string table link");
        const auto min_entry_size = is64 ? 24u : 16u;
        if (section.entry_size != min_entry_size ||
            section.size % section.entry_size != 0)
            throw Error("ELF: invalid symbol table entry size");
        const auto count = section.size / section.entry_size;
        if (count > limits.max_symbols_scanned - scanned)
            throw Error("ELF: symbol scan limit exceeded");
        scanned += static_cast<std::size_t>(count);
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto at = section.offset + i * section.entry_size;
            const auto name = r.u32(at);
            const auto info = r.u8(at + (is64 ? 4 : 12));
            const auto type = info & 0x0fu;
            const auto shndx = r.u16(at + (is64 ? 6 : 14));
            if ((type != STT_FUNC && type != STT_GNU_IFUNC) ||
                shndx == SHN_UNDEF || name == 0 || shndx >= 0xff00u)
                continue;
            if (shndx >= sections.size())
                throw Error("ELF: function has invalid section index");
            // STT_FUNC is a declaration, not proof of executable section residency.
            if ((sections[shndx].flags & 0x4u) == 0)
                continue;
            const auto address = is64 ? r.u64(at + 8) : r.u32(at + 4);
            const auto size = is64 ? r.u64(at + 16) : r.u32(at + 8);
            const bool thumb = result.machine == 40 && (address & 1u) != 0;
            const auto normalized = thumb ? address - 1u : address;
            if (size != 0 && size - 1 > std::numeric_limits<std::uint64_t>::max() - normalized)
                throw Error("ELF: symbol address range overflow");
            const auto label = text_at(r, sections[section.link], name,
                                       limits.max_name_bytes);
            if (label.empty())
                continue;
            if (result.functions.size() >= limits.max_functions)
                throw Error("ELF: function count limit exceeded");
            result.functions.push_back(
                {label, normalized, size, section.type == SHT_DYNSYM, thumb});
        }
    }
    // Prefer .symtab over .dynsym when a symbol is duplicated in both.
    std::sort(result.functions.begin(), result.functions.end(),
              [](const ElfFunction& a, const ElfFunction& b) {
        if (a.address != b.address) return a.address < b.address;
        if (a.name != b.name) return a.name < b.name;
        if (a.size != b.size) return a.size > b.size;
        return a.dynamic_symbol < b.dynamic_symbol;
    });
    result.functions.erase(
        std::unique(result.functions.begin(), result.functions.end(),
                    [](const ElfFunction& a, const ElfFunction& b) {
            return a.address == b.address && a.name == b.name && a.size == b.size;
        }), result.functions.end());
    return result;
}

ElfImage inspect_elf_file(const std::filesystem::path& path, ElfLimits limits) {
    std::error_code ec;
    if (std::filesystem::is_symlink(path, ec) ||
        !std::filesystem::is_regular_file(path, ec))
        throw Error("ELF: select a regular file, not a symlink");
    const auto length = std::filesystem::file_size(path, ec);
    if (ec || length > limits.max_file_bytes ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        throw Error("ELF: file too large or unavailable");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error("ELF: unable to open file");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (length) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(length));
        if (!input) throw Error("ELF: file changed or truncated while reading");
    }
    if (input.peek() != std::char_traits<char>::eof())
        throw Error("ELF: file size changed during inspection");
    if (!input.eof())
        throw Error("ELF: read failed");
    return parse_elf_image(bytes, limits);
}

ElfPcMatch lookup_elf_pc(const ElfImage& elf, std::uint64_t pc) {
    ElfPcMatch match;
    if (elf.is_relocatable || elf.requires_load_bias) return match;
    match.thumb_normalized = elf.machine == 40 && (pc & 1u) != 0;
    if (match.thumb_normalized) --pc;
    for (std::size_t i = 0; i < elf.functions.size(); ++i) {
        const auto& fn = elf.functions[i];
        if (fn.size != 0 && pc >= fn.address && pc - fn.address < fn.size) {
            if (match.index) {
                match.index.reset();
                match.ambiguous = true;
                return match;
            }
            match.index = i;
        }
    }
    return match;
}

} // namespace observatory
