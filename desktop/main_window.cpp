// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"
#include "timeline_canvas.hpp"

#include <QAbstractItemView>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <utility>

namespace observatory::desktop {
namespace {

std::filesystem::path native_path(const QString& p) {
#ifdef _WIN32
    return std::filesystem::path(p.toStdWString());
#else
    return std::filesystem::path(p.toUtf8().constData());
#endif
}
QString text(const std::string& s) { return QString::fromUtf8(s.c_str(), static_cast<qsizetype>(s.size())); }
QString num(std::uint64_t x) { return QString::number(static_cast<qulonglong>(x)); }
QString value(const std::string& s) { return s.empty() ? QStringLiteral("—") : text(s); }
QLabel* make_label(const QString& t, const QString& id = {}) {
    auto* l = new QLabel(t);
    if (!id.isEmpty()) l->setObjectName(id);
    return l;
}
QFrame* metric_card(const QString& caption, QLabel*& output) {
    auto* frame = new QFrame;
    frame->setObjectName("metric");
    auto* l = new QVBoxLayout(frame);
    l->setContentsMargins(18, 14, 18, 16);
    l->addWidget(make_label(caption, "subhead"));
    output = make_label("—", "bigNumber");
    l->addWidget(output);
    return frame;
}
QTableWidget* make_table(const QStringList& headings, const QString& id) {
    auto* t = new QTableWidget(0, headings.size());
    t->setObjectName(id);
    t->setHorizontalHeaderLabels(headings);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setShowGrid(false);
    t->setAlternatingRowColors(true);
    return t;
}
void append(QTableWidget* table, const QStringList& fields) {
    const int row = table->rowCount();
    table->insertRow(row);
    for (int i = 0; i < fields.size(); ++i)
        table->setItem(row, i, new QTableWidgetItem(fields.at(i)));
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName("rtosObservatoryDesktop");
    setWindowTitle("RTOS Observatory — Evidence Workspace");
    resize(1220, 780);
    setMinimumSize(880, 600);
    setStyleSheet(R"QSS(
      QWidget { background:#0c1523; color:#e6efff; font:12px "Segoe UI"; }
      QFrame#sidebar { background:#131f32; border-right:1px solid #2c3a51; }
      QLabel#brand { font-size:24px; font-weight:700; color:#f5f9ff; }
      QLabel#headline { font-size:26px; font-weight:700; color:#f5f9ff; }
      QLabel#bigNumber { font-size:30px; font-weight:650; color:#f7faff; }
      QLabel#subhead { font-size:11px; color:#96a9c5; }
      QFrame#metric { background:#18263c; border:1px solid #34455f; border-radius:12px; }
      QPushButton { background:#21334d; border:1px solid #38516c;
        border-radius:8px; padding:10px 12px; text-align:left; font-weight:600; }
      QPushButton:hover { background:#2c4261; }
      QPushButton:checked, QPushButton#primary {
        background:#2463eb; color:white; border-color:#3479fd; }
      QLabel#status[quality="complete"] { color:#5fdbab; font-weight:600; }
      QLabel#status[quality="incomplete"] { color:#ffd080; font-weight:600; }
      QLabel#status { color:#91a8c7; font-weight:600; }
      QListWidget, QTableWidget, QPlainTextEdit, QLineEdit, QComboBox {
        background:#131f32; color:#e7efff; border:1px solid #30445f;
        border-radius:8px; padding:5px; selection-background-color:#2156a5; }
      QHeaderView::section { background:#21334e; color:#c4d4ec;
        padding:8px; border:0; border-right:1px solid #33475e; }
      QListWidget::item { padding:7px; }
      QTableWidget::item { padding:5px; }
      QScrollBar:vertical { background:#162439; width:12px; }
      QScrollBar::handle:vertical { background:#3a5170; border-radius:5px; min-height:22px; }
    )QSS");
    build();
}

void MainWindow::build() {
    auto* root = new QWidget;
    setCentralWidget(root);
    auto* row = new QHBoxLayout(root);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    auto* sideFrame = new QFrame;
    sideFrame->setObjectName("sidebar");
    sideFrame->setFixedWidth(246);
    auto* side = new QVBoxLayout(sideFrame);
    side->setContentsMargins(18, 22, 18, 18);
    side->setSpacing(10);
    side->addWidget(make_label("◈ RTOS", "brand"));
    side->addWidget(make_label("OBSERVATORY / OFFLINE", "subhead"));
    side->addSpacing(18);
    auto* loadButton = new QPushButton("＋  Open trace");
    loadButton->setObjectName("primary");
    side->addWidget(loadButton);
    connect(loadButton, &QPushButton::clicked, this, [this] { open_dialog(); });
    auto* sessionButton = new QPushButton("▣  Open indexed session");
    side->addWidget(sessionButton);
    connect(sessionButton, &QPushButton::clicked, this, [this] { session_dialog(); });
    side->addWidget(make_label("TRACE DECODER", "subhead"));
    decoder_ = new QComboBox;
    decoder_->addItem("Universal v1", "universal-v1");
    decoder_->addItem("Legacy TSV", "legacy-tsv");
    side->addWidget(decoder_);
    side->addSpacing(14);
    side->addWidget(make_label("WORKSPACE", "subhead"));

    auto* nav = new QButtonGroup(this);
    nav->setExclusive(true);
    const auto entries = std::initializer_list<std::pair<const char*, int>>{
        {"Overview", 0}, {"Events", 1}, {"Execution timeline", 2}, {"CPU / waits", 3}, {"Findings", 4}, {"Evidence details", 5}};
    for (const auto& entry : entries) {
        auto* b = new QPushButton(QString::fromLatin1(entry.first));
        b->setCheckable(true);
        nav->addButton(b);
        side->addWidget(b);
        if (entry.second == 0) b->setChecked(true);
        connect(b, &QPushButton::clicked, this, [this, index = entry.second] {
            pages_->setCurrentIndex(index);
        });
    }
    side->addSpacing(18);
    side->addWidget(make_label("OPENED IN THIS PROCESS", "subhead"));
    recent_ = new QListWidget;
    recent_->setObjectName("recentSessions");
    recent_->setMaximumHeight(170);
    side->addWidget(recent_);
    connect(recent_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const QString p = item->data(Qt::UserRole).toString();
        const QString kind = item->data(Qt::UserRole + 1).toString();
        const bool ok = kind == "session"
            ? open_session(p)
            : open_trace(p, item->data(Qt::UserRole + 2).toString());
        if (!ok) show_error("Reopen failed");
    });
    side->addStretch(1);
    side->addWidget(make_label("No probe connection in W05", "subhead"));
    row->addWidget(sideFrame);

    auto* body = new QWidget;
    auto* main = new QVBoxLayout(body);
    main->setContentsMargins(27, 20, 27, 22);
    main->setSpacing(12);
    headline_ = make_label("Evidence workspace", "headline");
    main->addWidget(headline_);
    status_ = make_label("● NO SESSION  ·  Open a trace or saved session", "status");
    main->addWidget(status_);
    location_ = make_label("Local and read-only. No network listener, probe control or background polling.", "subhead");
    location_->setWordWrap(true);
    main->addWidget(location_);
    pages_ = new QStackedWidget;
    main->addWidget(pages_, 1);

    auto* overview = new QWidget;
    auto* ov = new QVBoxLayout(overview);
    ov->setSpacing(12);
    auto* cards = new QHBoxLayout;
    cards->addWidget(metric_card("RECORDED EVENTS", event_metric_));
    cards->addWidget(metric_card("OBSERVED TASKS", task_metric_));
    cards->addWidget(metric_card("FINDINGS", finding_metric_));
    ov->addLayout(cards);
    ov->addWidget(make_label("TASKS  /  VERIFIED FROM TRACE EVENTS", "subhead"));
    tasks_ = make_table({"Task", "Priority", "Period (µs)", "Idle"}, "taskTable");
    ov->addWidget(tasks_, 1);
    ov->addWidget(make_label("DATA QUALITY AND WARNINGS", "subhead"));
    warnings_ = new QListWidget;
    warnings_->setObjectName("qualityWarnings");
    warnings_->setMaximumHeight(145);
    warnings_->addItem("Open a trace to inspect its evidence.");
    ov->addWidget(warnings_);
    pages_->addWidget(overview);

    auto* eventsPage = new QWidget;
    auto* ev = new QVBoxLayout(eventsPage);
    ev->addWidget(make_label("EVENTS  /  CAPTURE ORDER  /  READ ONLY", "subhead"));
    filter_ = new QLineEdit;
    filter_->setPlaceholderText("Filter events by type, task, object or timestamp…");
    ev->addWidget(filter_);
    events_ = make_table({"Time (µs)", "Type", "Task", "Object / core", "Value", "Source line"}, "eventTable");
    ev->addWidget(events_);
    connect(filter_, &QLineEdit::textChanged, this, [this](const QString& q) { filter_events(q); });
    pages_->addWidget(eventsPage);

    // W06: native timeline with explicit unknown gaps; no implied history outside evidence.
    auto* timelinePage = new QWidget;
    auto* tl = new QVBoxLayout(timelinePage);
    tl->addWidget(make_label("EXECUTION TIMELINE  /  µs  /  SOLID = OBSERVED; HATCHED = UNKNOWN", "subhead"));
    auto* tools = new QHBoxLayout;
    tools->addWidget(make_label("From (µs)", "subhead"));
    range_begin_ = new QLineEdit("0");
    range_begin_->setObjectName("timelineRangeBegin");
    range_begin_->setMaximumWidth(145);
    tools->addWidget(range_begin_);
    tools->addWidget(make_label("To (µs)", "subhead"));
    range_end_ = new QLineEdit("0");
    range_end_->setObjectName("timelineRangeEnd");
    range_end_->setMaximumWidth(145);
    tools->addWidget(range_end_);
    auto* fit = new QPushButton("Fit");
    auto* zoomIn = new QPushButton("Zoom +");
    auto* zoomOut = new QPushButton("Zoom −");
    tools->addWidget(fit);
    tools->addWidget(zoomIn);
    tools->addWidget(zoomOut);
    tools->addStretch(1);
    tl->addLayout(tools);
    connect(range_begin_, &QLineEdit::editingFinished, this, [this] { refresh_timeline(); });
    connect(range_end_, &QLineEdit::editingFinished, this, [this] { refresh_timeline(); });
    connect(fit, &QPushButton::clicked, this, [this] {
        if (!controller_.loaded()) return;
        range_begin_->setText(num(controller_.report().start_us));
        range_end_->setText(num(controller_.report().end_us));
        refresh_timeline();
    });
    connect(zoomIn, &QPushButton::clicked, this, [this] { zoom_timeline(true); });
    connect(zoomOut, &QPushButton::clicked, this, [this] { zoom_timeline(false); });
    timeline_quality_ = make_label("Open a trace to explore recorded scheduler intervals.", "subhead");
    timeline_quality_->setWordWrap(true);
    tl->addWidget(timeline_quality_);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("timelineScroll");
    scroll->setWidgetResizable(true);
    timeline_ = new TimelineCanvas;
    timeline_->setObjectName("executionTimeline");
    scroll->setWidget(timeline_);
    tl->addWidget(scroll, 1);
    tl->addWidget(make_label("Blue: task running   Green: explicit idle   Amber: ISR   Hatched: unknown   Purple: completed wait", "subhead"));
    pages_->addWidget(timelinePage);

    auto* cpuPage = new QWidget;
    auto* cpuLayout = new QVBoxLayout(cpuPage);
    cpuLayout->addWidget(make_label("PER-CORE ACCOUNTING  /  NO AGGREGATE WITHOUT CORE QUALIFICATION", "subhead"));
    cpu_ = make_table({"Core", "Running (µs)", "ISR (µs)", "Idle (µs)", "Unknown (µs)", "Busy %"}, "cpuAnalysisTable");
    cpuLayout->addWidget(cpu_, 2);
    cpuLayout->addWidget(make_label("COMPLETED WAIT TIME  /  DOES NOT IMPLY TASK BLOCKED AT EVERY INSTANT", "subhead"));
    wait_summary_ = make_table({"Task", "Running (µs)", "Completed wait (µs)", "Waits"}, "waitSummaryTable");
    cpuLayout->addWidget(wait_summary_, 2);
    cpuLayout->addWidget(make_label("Durations are clipped to the selected window. Busy % appears only with full observed CPU coverage and no reported evidence loss.", "subhead"));
    pages_->addWidget(cpuPage);

    auto* findingsPage = new QWidget;
    auto* fi = new QVBoxLayout(findingsPage);
    fi->addWidget(make_label("FINDINGS  /  OBSERVED PATTERNS ARE NOT PROVEN ROOT CAUSES", "subhead"));
    findings_ = make_table({"Code", "Evidence status", "Task", "Resource", "Duration (µs)", "Description"}, "findingTable");
    fi->addWidget(findings_);
    pages_->addWidget(findingsPage);

    auto* detailsPage = new QWidget;
    auto* det = new QVBoxLayout(detailsPage);
    det->addWidget(make_label("CAPTURE IDENTITY  /  CLOCK  /  LOSS / COMPLETENESS", "subhead"));
    evidence_ = new QPlainTextEdit;
    evidence_->setObjectName("evidenceDetails");
    evidence_->setReadOnly(true);
    evidence_->setPlainText("No session loaded.");
    det->addWidget(evidence_);
    pages_->addWidget(detailsPage);

    auto* footer = new QHBoxLayout;
    footer->addWidget(make_label("W06 · Observed execution & CPU evidence · Offline only", "subhead"));
    footer->addStretch(1);
    auto* save = new QPushButton("Export report JSON");
    footer->addWidget(save);
    connect(save, &QPushButton::clicked, this, [this] { export_dialog(); });
    main->addLayout(footer);
    row->addWidget(body, 1);
}

void MainWindow::show_error(const QString& title) {
    QMessageBox::warning(this, title, error_);
}

void MainWindow::open_dialog() {
    const auto file = QFileDialog::getOpenFileName(this, "Open normalized trace", {}, "Traces (*.trace *.tsv *.txt);;All files (*)");
    if (!file.isEmpty() && !open_trace(file, decoder_->currentData().toString())) show_error("Trace import failed");
}

void MainWindow::session_dialog() {
    const auto folder = QFileDialog::getExistingDirectory(this, "Open verified indexed session");
    if (!folder.isEmpty() && !open_session(folder)) show_error("Session load failed");
}

void MainWindow::export_dialog() {
    if (!controller_.loaded()) {
        QMessageBox::information(this, "No report", "Open a trace or session first.");
        return;
    }
    const auto path = QFileDialog::getSaveFileName(this, "Export report into a new file", {}, "JSON (*.json)");
    if (path.isEmpty()) return;
    try { controller_.export_report(native_path(path)); }
    catch (const std::exception& e) { QMessageBox::warning(this, "Export failed", text(e.what())); }
}

bool MainWindow::open_trace(const QString& path, const QString& decoder) {
    try {
        controller_.open_trace(native_path(path), decoder.toStdString());
        error_.clear();
        refresh();
        add_recent(path, "trace", decoder);
        return true;
    } catch (const std::exception& e) {
        error_ = text(e.what());
        return false;
    }
}

bool MainWindow::open_session(const QString& path) {
    try {
        controller_.open_session(native_path(path));
        error_.clear();
        refresh();
        add_recent(path, "session", "");
        return true;
    } catch (const std::exception& e) {
        error_ = text(e.what());
        return false;
    }
}

void MainWindow::add_recent(const QString& path, const QString& kind, const QString& decoder) {
    for (int i = recent_->count() - 1; i >= 0; --i)
        if (recent_->item(i)->data(Qt::UserRole).toString() == path) delete recent_->takeItem(i);
    auto* item = new QListWidgetItem(QFileInfo(path).fileName());
    item->setToolTip(path);
    item->setData(Qt::UserRole, path);
    item->setData(Qt::UserRole + 1, kind);
    item->setData(Qt::UserRole + 2, decoder);
    recent_->insertItem(0, item);
    while (recent_->count() > 8) delete recent_->takeItem(recent_->count() - 1);
}

void MainWindow::refresh() {
    const auto& decoded = controller_.trace();
    const auto& report = controller_.report();
    headline_->setText(text(report.session) + "  /  Investigation");
    const bool complete = report.complete && decoded.evidence.missing_events == 0 && decoded.evidence.declared_drops == 0;
    status_->setText(complete ? "● OFFLINE EVIDENCE  ·  Complete within reported capture boundaries"
                              : "● INCOMPLETE EVIDENCE  ·  Gaps, lost events or uncertain state");
    status_->setProperty("quality", complete ? "complete" : "incomplete");
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    location_->setText(QString::fromStdString(controller_.origin().string()));
    event_metric_->setText(num(static_cast<std::uint64_t>(decoded.capture.events.size())));
    task_metric_->setText(num(static_cast<std::uint64_t>(report.threads.size())));
    finding_metric_->setText(num(static_cast<std::uint64_t>(report.findings.size())));
    tasks_->setRowCount(0);
    for (const auto& t : report.threads)
        append(tasks_, {value(t.id), t.priority ? QString::number(*t.priority) : "Unknown",
            t.period_us ? num(*t.period_us) : "Unknown", t.idle ? "Yes" : "No"});

    events_->setRowCount(0);
    constexpr std::size_t max_rows = 20000;
    for (std::size_t i = 0; i < std::min(max_rows, decoded.capture.events.size()); ++i) {
        const auto& e = decoded.capture.events[i];
        append(events_, {num(e.timestamp_us), value(e.type), value(e.task), value(e.object),
            QString::number(static_cast<qlonglong>(e.value)), num(e.line)});
    }
    filter_->clear();
    range_begin_->setText(num(report.start_us));
    range_end_->setText(num(report.end_us));
    refresh_timeline();
    findings_->setRowCount(0);
    for (const auto& f : report.findings)
        append(findings_, {value(f.code), value(f.confidence), value(f.task), value(f.resource),
            num(f.observed_us), value(f.description)});

    warnings_->clear();
    if (!complete) warnings_->addItem("Incomplete: do not infer missing scheduler or resource transitions.");
    for (const auto& msg : report.warnings) warnings_->addItem(text(msg));
    if (decoded.evidence.missing_events)
        warnings_->addItem("Sequence gaps: " + num(decoded.evidence.missing_events));
    if (decoded.evidence.declared_drops)
        warnings_->addItem("Declared dropped events: " + num(decoded.evidence.declared_drops));
    if (decoded.capture.events.size() > max_rows)
        warnings_->addItem("Preview capped at 20,000 events; full bounded capture remains in the core.");
    if (!warnings_->count()) warnings_->addItem("No reported gaps inside the observed capture boundaries.");

    evidence_->setPlainText(
        "ORIGIN\n" + QString::fromStdString(controller_.origin().string()) +
        "\n\nDECODER\n" + text(decoded.decoder) +
        "\n\nFIRMWARE LABEL (NOT ATTESTED)\n" + text(decoded.identity.firmware) +
        "\n\nRTOS\n" + text(decoded.identity.rtos) +
        "\n\nCLOCK\n" + text(decoded.identity.clock) +
        "\nTicks/second: " + num(decoded.identity.ticks_per_second) +
        "\n\nRECORDS\nEvents: " + num(decoded.capture.events.size()) +
        "\nSequence metadata: " + QString(decoded.evidence.sequence_available ? "available" : "unavailable") +
        "\nMissing events: " + num(decoded.evidence.missing_events) +
        "\nDeclared drops: " + num(decoded.evidence.declared_drops) +
        "\n\nQUALITY\n" + QString(complete ? "Complete within capture boundaries" : "Incomplete evidence") +
        "\n\nW06 SCOPE\nObserved offline timeline and CPU accounting only; no live debug, raw CTF decoder, physical probe or historical memory reconstruction.");
}

void MainWindow::filter_events(const QString& q) {
    for (int i = 0; i < events_->rowCount(); ++i) {
        bool match = q.isEmpty();
        for (int c = 0; !match && c < events_->columnCount(); ++c) {
            auto* item = events_->item(i, c);
            match = item && item->text().contains(q, Qt::CaseInsensitive);
        }
        events_->setRowHidden(i, !match);
    }
}

void MainWindow::refresh_timeline() {
    if (!controller_.loaded()) return;
    bool valid_begin = false, valid_end = false;
    const auto begin = range_begin_->text().toULongLong(&valid_begin);
    const auto end = range_end_->text().toULongLong(&valid_end);
    if (!valid_begin || !valid_end) {
        timeline_quality_->setText("Invalid range: enter non-negative decimal µs timestamps.");
        return;
    }
    try {
        auto snapshot = controller_.timeline(begin, end);
        cpu_->setRowCount(0);
        for (const auto& c : snapshot.cores)
            append(cpu_, {value(c.core), num(c.running_us), num(c.isr_us), num(c.idle_us),
                          num(c.unknown_us),
                          c.busy_percent ? QString::number(*c.busy_percent, 'f', 2) + "%"
                                         : "Unknown"});
        wait_summary_->setRowCount(0);
        for (const auto& t : snapshot.tasks)
            if (t.running_us || t.completed_waits)
                append(wait_summary_, {value(t.task), num(t.running_us),
                    num(t.completed_wait_us), num(static_cast<std::uint64_t>(t.completed_waits))});
        QString status = "Window [" + num(begin) + ", " + num(end) +
                         ") µs  ·  " + (snapshot.evidence_complete ? "No reported loss" : "Incomplete evidence");
        for (const auto& warning : snapshot.warnings) status += "\n• " + text(warning);
        timeline_quality_->setText(status);
        timeline_->set_view(std::move(snapshot));
    } catch (const std::exception& ex) {
        timeline_quality_->setText("Invalid timeline window: " + text(ex.what()));
    }
}
void MainWindow::zoom_timeline(bool zoom_in) {
    if (!controller_.loaded()) return;
    bool a = false, b = false;
    const auto begin = range_begin_->text().toULongLong(&a);
    const auto end = range_end_->text().toULongLong(&b);
    if (!a || !b || end <= begin) return;
    const auto& report = controller_.report();
    const auto width = end - begin;
    const auto new_width = zoom_in ? std::max<std::uint64_t>(1, width / 2)
                                  : std::min<std::uint64_t>(report.end_us - report.start_us,
                                                            width + std::min(width, report.end_us - report.start_us - width));
    const auto offset = (width - (zoom_in ? new_width : width)) / 2;
    auto from = zoom_in ? begin + offset : begin >= (new_width - width) / 2
                              ? begin - (new_width - width) / 2 : report.start_us;
    if (from < report.start_us) from = report.start_us;
    if (new_width > report.end_us - from) from = report.end_us - new_width;
    range_begin_->setText(num(from));
    range_end_->setText(num(from + new_width));
    refresh_timeline();
}
int MainWindow::cpu_rows() const noexcept {
    return cpu_ ? cpu_->rowCount() : 0;
}

int MainWindow::event_rows() const noexcept {
    return events_ ? events_->rowCount() : 0;
}

} // namespace observatory::desktop
