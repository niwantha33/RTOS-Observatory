// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observatory/desktop/session_controller.hpp"
#include <QMainWindow>
#include <QString>
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;
namespace observatory::desktop {
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool open_trace(const QString& path, const QString& decoder = "universal-v1");
    bool open_session(const QString& path);
    bool is_loaded() const noexcept { return controller_.loaded(); }
    int event_rows() const noexcept;
    QString last_error() const { return error_; }
private:
    void build();
    void refresh();
    void open_dialog();
    void session_dialog();
    void export_dialog();
    void show_error(const QString& title);
    void add_recent(const QString& path, const QString& kind, const QString& decoder);
    void filter_events(const QString& query);
    SessionController controller_;
    QString error_;
    QComboBox* decoder_{};
    QLabel* headline_{};
    QLabel* status_{};
    QLabel* location_{};
    QLabel* event_metric_{};
    QLabel* task_metric_{};
    QLabel* finding_metric_{};
    QListWidget* recent_{};
    QListWidget* warnings_{};
    QPlainTextEdit* evidence_{};
    QStackedWidget* pages_{};
    QTableWidget* events_{};
    QTableWidget* tasks_{};
    QTableWidget* findings_{};
    QLineEdit* filter_{};
};
} // namespace observatory::desktop
