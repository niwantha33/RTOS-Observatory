// SPDX-License-Identifier: Apache-2.0
#include "observatory/desktop/session_controller.hpp"
#include <fstream>
#include <utility>

namespace observatory::desktop {
void SessionController::accept(DecodedTrace candidate, const std::filesystem::path& path, const std::string& mode) {
    auto r = analyze(candidate.capture);
    trace_ = std::move(candidate);
    report_ = std::move(r);
    origin_ = path;
    mode_ = mode;
}
void SessionController::open_trace(const std::filesystem::path& file, const std::string& decoder) {
    if (std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file))
        throw Error("Choose an existing regular trace file; symlinks are not accepted");
    std::ifstream input(file, std::ios::binary);
    if (!input) throw Error("Cannot read the trace file");
    auto candidate = default_decoders().decode(decoder, input);
    accept(std::move(candidate), file, "trace");
}
void SessionController::open_session(const std::filesystem::path& directory) {
    auto candidate = replay_session(directory); // authenticated? NO: integrity checks only
    accept(std::move(candidate), directory, "session");
}
const DecodedTrace& SessionController::trace() const {
    if (!trace_) throw Error("No trace loaded");
    return *trace_;
}
const Report& SessionController::report() const {
    if (!report_) throw Error("No report loaded");
    return *report_;
}
TimelineView SessionController::timeline(std::uint64_t begin_us, std::uint64_t end_us) const {
    if (!loaded()) throw Error("No trace loaded");
    const auto& evidence = trace_->evidence;
    return build_timeline(*report_, begin_us, end_us,
                          evidence.missing_events == 0 && evidence.declared_drops == 0);
}
void SessionController::export_report(const std::filesystem::path& file) const {
    if (!report_) throw Error("No investigation loaded");
    if (file.empty() || !file.has_filename()) throw Error("Invalid report destination");
    if (std::filesystem::is_symlink(file) || std::filesystem::exists(file))
        throw Error("Export refuses to overwrite an existing file");
    if (mode_ == "session") {
        auto parent = std::filesystem::weakly_canonical(file.has_parent_path() ? file.parent_path() : std::filesystem::current_path());
        if (parent == std::filesystem::weakly_canonical(origin_))
            throw Error("An immutable session directory cannot be modified");
    }
    const auto data = report_json(*report_);
    std::ofstream output(file, std::ios::out | std::ios::binary);
    if (!output) throw Error("Cannot create report");
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output) { std::filesystem::remove(file); throw Error("Report export failed"); }
}
} // namespace observatory::desktop
