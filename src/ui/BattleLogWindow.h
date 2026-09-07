#pragma once

#include <QWidget>
#include <QStringList>

class QPlainTextEdit;

namespace sanguosha::ui {

class BattleLogWindow final : public QWidget {
public:
    explicit BattleLogWindow(QWidget* parent = nullptr);

    void setEntries(const QStringList& entries);
    void clearEntries();
    [[nodiscard]] QPlainTextEdit* logView() const noexcept { return logView_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QPlainTextEdit* logView_ {};
    QString cachedText_;
};

} // namespace sanguosha::ui
