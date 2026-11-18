// SPDX-License-Identifier: Apache-2.0
#include "timeline_canvas.hpp"
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <utility>

namespace observatory::desktop {
namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
QString tick(std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }
QColor state_color(const std::string& state) {
    if (state == "running") return QColor("#39bcef");
    if (state == "idle") return QColor("#3abc9c");
    if (state == "isr") return QColor("#ffb95d");
    return QColor("#66758c");
}
std::string wait_key(const WaitSlice& w) { return w.task + "\n" + w.resource; }
} // namespace

TimelineCanvas::TimelineCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}
void TimelineCanvas::set_view(TimelineView view) {
    view_ = std::move(view);
    rows_.clear();
    core_rows_.clear();
    task_rows_.clear();
    wait_rows_.clear();
    rows_limited_ = false;
    for (const auto& core : view_.cores) {
        const auto id = static_cast<int>(rows_.size());
        rows_.push_back("CPU " + core.core);
        core_rows_[core.core] = id;
    }
    for (const auto& task : view_.tasks) {
        if (!task.running_us) continue;
        if (task_rows_.size() >= 32) { rows_limited_ = true; continue; }
        const auto id = static_cast<int>(rows_.size());
        rows_.push_back("TASK " + task.task);
        task_rows_[task.task] = id;
    }
    for (const auto& wait : view_.waits) {
        const auto key = wait_key(wait);
        if (wait_rows_.contains(key)) continue;
        if (wait_rows_.size() >= 32) { rows_limited_ = true; continue; }
        const auto id = static_cast<int>(rows_.size());
        rows_.push_back("WAIT " + wait.task + " / " + wait.resource);
        wait_rows_[key] = id;
    }
    setMinimumHeight(std::max(160, top_ + row_height_ * static_cast<int>(rows_.size()) + 30));
    update();
}
int TimelineCanvas::lane_for_core(const std::string& core) const {
    const auto at = core_rows_.find(core);
    return at == core_rows_.end() ? -1 : at->second;
}
int TimelineCanvas::lane_for_task(const std::string& task) const {
    const auto at = task_rows_.find(task);
    return at == task_rows_.end() ? -1 : at->second;
}
int TimelineCanvas::lane_for_wait(const std::string& task, const std::string& resource) const {
    const auto at = wait_rows_.find(task + "\n" + resource);
    return at == wait_rows_.end() ? -1 : at->second;
}
QRectF TimelineCanvas::bar(std::uint64_t begin, std::uint64_t end, int lane) const {
    if (lane < 0 || view_.end_us <= view_.start_us || width() <= left_ + 25)
        return {};
    const auto span = static_cast<long double>(view_.end_us - view_.start_us);
    const auto available = static_cast<qreal>(width() - left_ - 20);
    const auto x1 = static_cast<qreal>(static_cast<long double>(begin - view_.start_us) / span) * available;
    const auto x2 = static_cast<qreal>(static_cast<long double>(end - view_.start_us) / span) * available;
    return {left_ + x1, static_cast<qreal>(top_ + lane * row_height_ + 4),
            std::max<qreal>(0, x2 - x1), 22};
}
void TimelineCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#111d30"));
    p.setPen(QColor("#98acca"));
    p.drawText(10, 23, "EXECUTION / OBSERVED CORE STATES");
    if (view_.end_us <= view_.start_us) {
        p.drawText(12, 85, "Open a trace to display observed CPU activity.");
        return;
    }
    constexpr int ticks = 4;
    for (int i = 0; i <= ticks; ++i) {
        const auto span = view_.end_us - view_.start_us;
        const auto timestamp = view_.start_us + static_cast<std::uint64_t>(
            static_cast<long double>(span) * i / ticks);
        const auto x = left_ + static_cast<qreal>(width() - left_ - 20) * i / ticks;
        p.setPen(QColor("#304157"));
        p.drawLine(QPointF(x, top_ - 6), QPointF(x, height() - 15));
        p.setPen(QColor("#9eafc7"));
        p.drawText(QRectF(x - 50, 25, 100, 17), Qt::AlignCenter, tick(timestamp));
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const int y = top_ + static_cast<int>(i) * row_height_;
        p.fillRect(QRect(left_, y, std::max(0, width() - left_ - 20), row_height_ - 4),
                   i % 2 ? QColor("#19293e") : QColor("#1e3046"));
        p.setPen(QColor("#c9daed"));
        const auto name = q(rows_[i]);
        p.drawText(QRect(8, y, left_ - 20, row_height_ - 3),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(name, Qt::ElideRight, left_ - 26));
    }
    for (const auto& slice : view_.slices) {
        const auto lane = lane_for_core(slice.core);
        auto r = bar(slice.start_us, slice.end_us, lane);
        if (r.isEmpty()) continue;
        if (slice.state == "unknown") {
            p.fillRect(r, QBrush(state_color(slice.state), Qt::BDiagPattern));
        } else {
            p.setBrush(state_color(slice.state));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r, 3, 3);
        }
        if (r.width() > 64 && slice.state != "unknown") {
            p.setPen(QColor("#071627"));
            p.drawText(r.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(q(slice.task), Qt::ElideRight,
                                                  static_cast<int>(r.width()) - 7));
        }
    }
    // Separate per-task tracks show proven execution, not inferred Ready/Sleeping.
    for (const auto& slice : view_.slices) {
        if (slice.state != "running") continue;
        auto r = bar(slice.start_us, slice.end_us, lane_for_task(slice.task));
        if (r.isEmpty()) continue;
        p.setBrush(QColor("#468ee5"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, 3, 3);
    }
    for (const auto& wait : view_.waits) {
        const auto lane = lane_for_wait(wait.task, wait.resource);
        auto r = bar(wait.start_us, wait.end_us, lane);
        if (r.isEmpty()) continue;
        p.setBrush(QColor("#a58afb"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, 3, 3);
    }
    if (rows_limited_) {
        p.setPen(QColor("#ffcf83"));
        p.drawText(12, height() - 9, "Task/wait lanes capped at 32 each; all durations remain in the CPU/wait tables.");
    }
}
void TimelineCanvas::mouseMoveEvent(QMouseEvent* event) {
    const auto point = event->position();
    for (const auto& slice : view_.slices) {
        const auto r = bar(slice.start_us, slice.end_us, lane_for_core(slice.core));
        if (!r.contains(point)) continue;
        const auto title = slice.state == "unknown" ? QStringLiteral("Unknown / no proven CPU state")
                                                   : q(slice.state + " / " + slice.task);
        QToolTip::showText(event->globalPosition().toPoint(),
            "Core: " + q(slice.core) + "\n" + title +
            "\n[" + tick(slice.start_us) + ", " + tick(slice.end_us) + ") µs", this);
        return;
    }
    for (const auto& slice : view_.slices) {
        if (slice.state != "running") continue;
        const auto r = bar(slice.start_us, slice.end_us, lane_for_task(slice.task));
        if (!r.contains(point)) continue;
        QToolTip::showText(event->globalPosition().toPoint(),
            "Observed task execution: " + q(slice.task) + "\nCore: " + q(slice.core) +
            "\n[" + tick(slice.start_us) + ", " + tick(slice.end_us) + ") µs", this);
        return;
    }
    for (const auto& wait : view_.waits) {
        const auto r = bar(wait.start_us, wait.end_us, lane_for_wait(wait.task, wait.resource));
        if (!r.contains(point)) continue;
        QToolTip::showText(event->globalPosition().toPoint(),
            "Completed " + q(wait.kind) + " wait\n" + q(wait.task) + " / " + q(wait.resource) +
            "\n[" + tick(wait.start_us) + ", " + tick(wait.end_us) + ") µs", this);
        return;
    }
    QToolTip::hideText();
    QWidget::mouseMoveEvent(event);
}
} // namespace observatory::desktop
