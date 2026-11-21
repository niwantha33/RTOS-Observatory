// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace observatory {

// Offline, read-only native ELF symbols. DWARF/source line decoding is NOT provided.
struct ElfLimits {
    std::uint64_t max_file_bytes{256ull * 1024 * 1024};
    std::size_t max_sections{16384};
    std::size_t max_symbols_scanned{2000000};
    std::size_t max_functions{200000};
    std::size_t max_name_bytes{4096};
};

struct ElfFunction {
    std::string name;
    std::uint64_t address{};
    std::uint64_t size{};
    bool dynamic_symbol{};  // false = .symtab, true = .dynsym
    bool thumb_symbol{};    // ARM odd-value Thumb indication
};

struct ElfImage {
    unsigned address_bits{};
    std::uint16_t machine{};
    std::string architecture;
    std::string sha256;       // Hash of the exact bytes inspected
    std::string build_id;     // GNU note (if present); not a signature
    bool has_dwarf_sections{}; // Presence only, NOT parsed debug info
    bool is_relocatable{};   // Addresses are section-relative, not runtime PCs
    bool requires_load_bias{}; // ET_DYN addresses require a proven runtime image base
    std::vector<ElfFunction> functions;
};

struct ElfPcMatch {
    std::optional<std::size_t> index;
    bool ambiguous{};
    bool thumb_normalized{};
};

// Throws observatory::Error for malformed/unsupported/beyond-limits binaries.
ElfImage parse_elf_image(const std::vector<std::uint8_t>& bytes,
                         ElfLimits limits = {});
ElfImage inspect_elf_file(const std::filesystem::path& path,
                          ElfLimits limits = {});

// A PC lookup is useful only when the trace's firmware image identity matches.
// The caller MUST perform that verification. This function cannot attest identity.
ElfPcMatch lookup_elf_pc(const ElfImage& elf, std::uint64_t pc);

} // namespace observatory
