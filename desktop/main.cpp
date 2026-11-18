// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("RTOS Observatory");
    QCoreApplication::setOrganizationName("RTOS Observatory");
    observatory::desktop::MainWindow window;
    const auto args = QCoreApplication::arguments();
    if (args.size() >= 2 && args.at(1) == "--help") {
        QTextStream(stdout) << "rtos-observatory-desktop [--trace PATH | --session PATH]\n"
                               "--smoke PATH: native Qt headless test, no event loop\n";
        return 0;
    }
    if (args.size() >= 3 && args.at(1) == "--smoke") {
        const bool ok = window.open_trace(args.at(2)) && window.is_loaded() && window.event_rows() > 0;
        QTextStream(stdout) << (ok ? "W05 desktop smoke passed\n" : "W05 desktop smoke failed: " + window.last_error() + "\n");
        return ok ? 0 : 2;
    }
    if (args.size() >= 3 && args.at(1) == "--trace") window.open_trace(args.at(2));
    if (args.size() >= 3 && args.at(1) == "--session") window.open_session(args.at(2));
    window.show();
    return app.exec();
}
