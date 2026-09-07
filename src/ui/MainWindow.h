#pragma once

#include <QMainWindow>
#include <QSet>
#include <QString>
#include <optional>
#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;
class QWidget;
class QComboBox;
class QStackedWidget;
class QScrollArea;
class QEvent;

namespace sanguosha { class IGameClient; enum class CardType; namespace network { class GameServer; } }

namespace sanguosha::ui {
class BattleLogWindow;
class InteractionPanel;
enum class CardInteractionState { Idle, CardSelected, SelectingTarget, ReadyToConfirm, SelectingSerpentSpearCards, SelectingSerpentSpearTarget };

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(IGameClient& client, network::GameServer* hostServer = nullptr, std::function<void()> leaveRoom = {}, QWidget* parent = nullptr);
private:
    void refresh(); void refreshLobbyPage(); void rebuildPlayers(); void rebuildHand(); void rebuildTargets(); void rebuildCardSelection(); void rebuildNullificationInteraction(); void updateControls();
    void updateInteractionOverlayGeometry();
    bool eventFilter(QObject* watched, QEvent* event) override;
    void clearCardSelection(); void selectCard(const QString& cardId, CardType type);
    void beginSerpentSpear(); void clearSerpentSpearSelection();
    void clearDiscardSelection(); [[nodiscard]] QSet<QString> selectedDiscardCardIdsFromUi() const;
    [[nodiscard]] bool canConfirmCardUse() const; [[nodiscard]] bool targetSelectionActive() const;
    [[nodiscard]] int selectedCardMinTargets() const;
    [[nodiscard]] int selectedCardMaxTargets() const;
    [[nodiscard]] bool serpentSpearEquipped() const;
    [[nodiscard]] bool serpentSpearResponseActive() const;
    [[nodiscard]] bool serpentSpearModeActive() const;
    [[nodiscard]] static bool cardRequiresTarget(CardType type); [[nodiscard]] static bool cardCanBeActivelyUsed(CardType type);
    IGameClient& client_;
    network::GameServer* hostServer_ {};
    std::function<void()> leaveRoom_;
    QStackedWidget* pages_ {}; QWidget* lobbyPage_ {}; QWidget* gamePage_ {}; QLabel* lobbyStatusLabel_ {};
    QWidget* lobbySection_ {}; QComboBox* playerCountBox_ {}; QLabel* lobbySummaryLabel_ {}; QPushButton* addAIButton_ {}; QPushButton* removeAIButton_ {}; QPushButton* startGameButton_ {}; QPushButton* leaveRoomButton_ {}; QPushButton* returnLobbyButton_ {};
    QLabel* modeLabel_ {}; QLabel* turnLabel_ {}; QLabel* phaseLabel_ {}; QLabel* timeoutLabel_ {}; QLabel* actionBanner_ {}; QLabel* judgmentResultLabel_ {}; QLabel* equipmentEffectLabel_ {}; QLabel* instructionLabel_ {}; QLabel* gameOverLabel_ {};
    QGridLayout* opponentsLayout_ {}; QHBoxLayout* selfPanelLayout_ {}; QHBoxLayout* handLayout_ {};
    InteractionPanel* interactionPanel_ {}; BattleLogWindow* battleLogWindow_ {}; QPushButton* battleLogButton_ {}; QScrollArea* battleScroll_ {};
    QPushButton* endPlayButton_ {}; QPushButton* confirmUseButton_ {}; QPushButton* cancelSelectionButton_ {}; QPushButton* serpentSpearButton_ {}; QPushButton* declineButton_ {}; QPushButton* restartButton_ {};
    QString selectedCardId_; std::optional<CardType> selectedCardType_; QSet<QString> selectedTargetIds_; QSet<QString> selectedDiscardIds_; QSet<QString> serpentSpearMaterialIds_; QSet<qulonglong> selectedSelectionOptionIds_;
    CardInteractionState cardInteractionState_ {CardInteractionState::Idle};
    QString interactionSignature_;
    std::optional<qulonglong> activeCardSelectionRequestId_;
    std::optional<qulonglong> activeResponseRequestId_;
    QString selectedResponseCardId_;
    std::optional<qulonglong> serpentSpearResponseRequestId_;
    std::uint64_t lastDisplayedEquipmentEffectEventId_ {0};
    std::size_t lastVisibleLogCount_ {0};
    QString lastRecentLog_;
};
} // namespace sanguosha::ui
