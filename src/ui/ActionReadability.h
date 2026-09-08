#pragma once

#include <QFrame>
#include <QQueue>
#include <QString>
#include <cstdint>
#include <unordered_set>

#include "session/PlayerViewState.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace sanguosha::ui {

struct UIActionItem {
    std::uint64_t eventId {0};
    QString summary;
    QString stageTitle;
    QString stageDetail;
    QString sourceText;
    QString targetText;
    QString cardText;
    PlayerId source {0};
    PlayerId target {0};
    bool critical {false};
};

class ActionReadability final : public QFrame {
public:
    explicit ActionReadability(QWidget* parent = nullptr);
    void ingest(const PlayerViewState& view, bool reconnectSnapshot = false);
    void resetForLobby();
    void setTestingBypass(bool enabled);
    void setPrompt(const QString& prompt);
    [[nodiscard]] QPlainTextEdit* recentView() const noexcept { return recent_; }
    [[nodiscard]] QPlainTextEdit* compactView() const noexcept { return compact_; }
    [[nodiscard]] QFrame* actionStage() const noexcept { return stage_; }
    [[nodiscard]] int queuedCount() const noexcept { return queue_.size(); }
    [[nodiscard]] std::uint64_t lastConsumedEventId() const noexcept { return lastEventId_; }
    [[nodiscard]] PlayerId stageSource() const noexcept { return stageSource_; }
    [[nodiscard]] PlayerId stageTarget() const noexcept { return stageTarget_; }

private:
    void appendBounded(QPlainTextEdit* view, const QString& text, int maximumBlocks);
    void enqueue(UIActionItem item);
    void playNext();
    UIActionItem present(const GameEvent& event, const PlayerViewState& view) const;
    QString playerName(const PlayerViewState& view, PlayerId id) const;
    QString playerSummary(const PlayerViewState& view, PlayerId id) const;

    QPlainTextEdit* recent_ {};
    QPlainTextEdit* compact_ {};
    QFrame* stage_ {};
    QLabel* stageTitle_ {};
    QLabel* stageDetail_ {};
    QLabel* stagePrompt_ {};
    QLabel* stageSourceLabel_ {};
    QLabel* stageCardLabel_ {};
    QLabel* stageTargetLabel_ {};
    QPushButton* latest_ {};
    QTimer* timer_ {};
    QQueue<UIActionItem> queue_;
    std::unordered_set<std::string> harvestChoices_;
    std::string fireRevealId_;
    std::string judgmentId_;
    std::uint64_t lastEquipmentEventId_ {0};
    std::uint64_t lastEventId_ {0};
    bool playing_ {false};
    bool testingBypass_ {false};
    bool gameOver_ {false};
    PlayerId stageSource_ {0};
    PlayerId stageTarget_ {0};
};

} // namespace sanguosha::ui
