#pragma once

#include <QFrame>
#include <QSize>

#include <functional>

class QLabel;
class QGridLayout;
class QPushButton;
class QScrollArea;

namespace sanguosha::ui {

class InteractionPanel final : public QFrame {
public:
    explicit InteractionPanel(QWidget* parent = nullptr);

    void setContext(const QString& title, const QString& prompt, const QString& detail = {});
    void clearCandidates();
    void addCandidate(QWidget* candidate, int row, int column);
    void setActions(bool confirmVisible, bool confirmEnabled, const QString& confirmText,
                    std::function<void()> confirm,
                    bool cancelVisible, std::function<void()> cancel,
                    bool passVisible = false, std::function<void()> pass = {});
    void setAvailable(bool available);
    void fitToViewport(const QSize& viewportSize);
    void reset();

    [[nodiscard]] QScrollArea* candidateScroll() const noexcept { return scroll_; }
    [[nodiscard]] QPushButton* confirmButton() const noexcept { return confirm_; }
    [[nodiscard]] int candidateCount() const noexcept { return candidateCount_; }

private:
    QLabel* title_ {};
    QLabel* prompt_ {};
    QLabel* detail_ {};
    QScrollArea* scroll_ {};
    QGridLayout* candidates_ {};
    QPushButton* confirm_ {};
    QPushButton* cancel_ {};
    QPushButton* pass_ {};
    int candidateCount_ {0};
};

} // namespace sanguosha::ui
