// SPDX-License-Identifier: Apache-2.0
#include "observatory/desktop/session_controller.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using observatory::desktop::SessionController;
using observatory::Error;

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    fs::path sample(argv[1]);
    SessionController controller;
    assert(!controller.loaded());
    bool rejected = false;
    try { (void)controller.report(); } catch (const Error&) { rejected = true; }
    assert(rejected);
    controller.open_trace(sample, "universal-v1");
    assert(controller.loaded());
    assert(controller.trace().decoder == "universal-v1");
    assert(controller.trace().capture.events.size() >= 2);
    const auto full_view = controller.timeline(0, 20);
    assert(full_view.cores.size() == 1);
    assert(full_view.cores[0].unknown_us == 10);
    assert(!full_view.cores[0].busy_percent);
    const auto known_view = controller.timeline(10, 20);
    assert(known_view.cores[0].running_us == 10);
    assert(known_view.cores[0].unknown_us == 0);
    assert(known_view.cores[0].busy_percent && *known_view.cores[0].busy_percent == 100.0);
    const auto count = controller.trace().capture.events.size();
    rejected = false;
    try { controller.open_trace(sample, "missing-decoder"); } catch (const Error&) { rejected = true; }
    assert(rejected && controller.loaded() && controller.trace().capture.events.size() == count);
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path root = fs::temp_directory_path() / ("rtos-observatory-w05-" + suffix);
    fs::create_directory(root);
    try {
        auto export_path = root / "investigation.json";
        controller.export_report(export_path);
        assert(fs::file_size(export_path) > 20);
        rejected = false;
        try { controller.export_report(export_path); } catch (const Error&) { rejected = true; }
        assert(rejected);
        const auto session_path = root / "indexed-session";
        std::ifstream source(sample, std::ios::binary);
        observatory::store_session(session_path, "universal-v1", source);
        controller.open_session(session_path);
        assert(controller.mode() == "session");
        assert(controller.trace().capture.events.size() == count);
        rejected = false;
        try { controller.export_report(session_path / "new.json"); } catch (const Error&) { rejected = true; }
        assert(rejected);
        rejected = false;
        try { controller.open_session(root); } catch (const Error&) { rejected = true; }
        assert(rejected && controller.loaded() && controller.origin() == session_path);
    } catch (...) { fs::remove_all(root); throw; }
    fs::remove_all(root);
    std::cout << "W05 controller tests passed\n";
}
