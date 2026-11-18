// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/timeline.hpp"
#include <QWidget>
#include <map>
#include <string>
#include <vector>

namespace observatory::desktop {
// Read-only rasterization of validated model intervals. No synthesized task states.
class TimelineCanvas final : public QWidget {
public:
    explicit TimelineCanvas(QWidget* parent = nullptr);
    void set_view(TimelineView view);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
private:
    int lane_for_core(const std::string& core) const;
    int lane_for_wait(const std::string& task, const std::string& resource) const;
    int lane_for_task(const std::string& task) const;
    QRectF bar(std::uint64_t begin, std::uint64_t end, int lane) const;
    TimelineView view_;
    std::vector<std::string> rows_;
    std::map<std::string, int> core_rows_;
    std::map<std::string, int> task_rows_;
    std::map<std::string, int> wait_rows_;
    bool rows_limited_{false};
    static constexpr int left_ = 170;
    static constexpr int top_ = 48;
    static constexpr int row_height_ = 36;
};
} // namespace observatory::desktop
