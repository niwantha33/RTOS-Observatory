// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"

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
        {"Overview", 0}, {"Events", 1}, {"Findings", 2}, {"Evidence details", 3}};
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
    footer->addWidget(make_label("W05 · Native Qt 6 desktop · Offline analysis only", "subhead"));
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
        "\n\nW05 SCOPE\nNo live debug, raw CTF decoder, physical probe or historical memory reconstruction.");
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

int MainWindow::event_rows() const noexcept {
    return events_ ? events_->rowCount() : 0;
}

} // namespace observatory::desktop
