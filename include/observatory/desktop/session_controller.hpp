// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/session.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace observatory::desktop {
// Native, IDE-independent boundary. Import failures preserve the last good report.
class SessionController final {
public:
    void open_trace(const std::filesystem::path& file, const std::string& decoder);
    void open_session(const std::filesystem::path& directory);
    void export_report(const std::filesystem::path& file) const;
    bool loaded() const noexcept { return trace_.has_value() && report_.has_value(); }
    const DecodedTrace& trace() const;
    const Report& report() const;
    const std::filesystem::path& origin() const noexcept { return origin_; }
    const std::string& mode() const noexcept { return mode_; }
private:
    void accept(DecodedTrace candidate, const std::filesystem::path& path, const std::string& mode);
    std::optional<DecodedTrace> trace_;
    std::optional<Report> report_;
    std::filesystem::path origin_;
    std::string mode_;
};
} // namespace observatory::desktop
