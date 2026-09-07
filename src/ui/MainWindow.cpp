#include "ui/MainWindow.h"

#include <algorithm>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "client/IGameClient.h"
#include "network/GameServer.h"
#include "network/NetworkGameClient.h"
#include "ui/GameText.h"
#include "ui/GameWidgets.h"
#include "ui/BattleLogWindow.h"
#include "ui/InteractionPanel.h"

namespace sanguosha::ui {
namespace {
QString playerName(const PublicPlayerView& p)
{
    const auto name = playerDisplayName(p.displayName);
    return p.connected ? name : name + QStringLiteral("（已断开）");
}
QString lobbyRosterText(const network::LobbyState& state)
{
    QString text;
    for (const auto& player : state.players) {
        const auto status = player.ai ? QObject::tr("AI") : player.connected ? QObject::tr("在线") : QObject::tr("离线，等待重连");
        text += QObject::tr("\n座位 %1  %2%3  [%4]").arg(player.seat + 1).arg(player.nickname)
                    .arg(player.host ? QObject::tr(" [房主]") : QString()).arg(status);
    }
    if (state.aiPlayerCount > 0) text += QObject::tr("\nAI 补位：%1").arg(state.aiPlayerCount);
    return text;
}
QString cardName(const CardView& c) { return cardDisplayName(Card(c.id, c.displayName, c.type, c.suit, c.rank)); }
QString cardLabel(const CardView& c) { return QString("【%1】 %2 %3").arg(cardName(c), suitToSymbol(c.suit), rankToDisplayName(c.rank)); }
QString playerNameFor(const PlayerViewState& view, PlayerId id)
{
    const auto found = std::find_if(view.players.begin(), view.players.end(), [id](const auto& player) { return player.id == id; });
    return found == view.players.end() ? QObject::tr("玩家 %1").arg(id) : playerName(*found);
}
void clearLayout(QLayout* layout) { while (auto* item = layout->takeAt(0)) { delete item->widget(); delete item; } }
QString cardTooltip(const CardView& c) { Card card(c.id, c.displayName, c.type, c.suit, c.rank); return QString("%1\n%2 %3\n%4\n%5").arg(cardName(c), suitToSymbol(c.suit), rankToDisplayName(c.rank), cardCategoryDisplayName(c.type), cardDescription(card)); }
QString equipmentDetail(const CardView& c) { return cardTooltip(c).replace('\n', QStringLiteral(" · ")); }
QString localizedPrompt(const std::string& source)
{
    QString result = QString::fromStdString(source);
    result.replace(QRegularExpression(QStringLiteral("\\bPlayer (\\d+)")), QStringLiteral("玩家 \\1"));
    if (result == QStringLiteral("Activate Ice Sword")) return QStringLiteral("发动【寒冰剑】");
    return result;
}
QString localizedOptionName(const std::string& source)
{
    const QString name = QString::fromStdString(source);
    if (name == QStringLiteral("JueYing")) return QStringLiteral("绝影");
    if (name == QStringLiteral("ZhuaHuangFeiDian")) return QStringLiteral("爪黄飞电");
    if (name == QStringLiteral("HuaLiu")) return QStringLiteral("骅骝");
    if (name == QStringLiteral("DiLu")) return QStringLiteral("的卢");
    if (name == QStringLiteral("DaYuan")) return QStringLiteral("大宛");
    if (name == QStringLiteral("ChiTu")) return QStringLiteral("赤兔");
    if (name == QStringLiteral("ZiXing")) return QStringLiteral("紫骍");
    return name;
}
QString selectionPrompt(CardSelectionPurpose purpose)
{
    switch (purpose) {
    case CardSelectionPurpose::Dismantlement: return QObject::tr("过河拆桥：请选择目标的一张手牌或装备。 ");
    case CardSelectionPurpose::Snatch: return QObject::tr("顺手牵羊：请选择要获得的一张手牌或装备。 ");
    case CardSelectionPurpose::Harvest: return QObject::tr("五谷丰登：请选择一张公开牌。 ");
    case CardSelectionPurpose::FireAttackReveal: return QObject::tr("火攻：请选择要展示的一张手牌。 ");
    case CardSelectionPurpose::FireAttackDiscard: return QObject::tr("火攻：请选择一张与展示牌同花色的手牌，或直接跳过。 ");
    case CardSelectionPurpose::AxeDiscard: return QObject::tr("贯石斧：你的【杀】被【闪】抵消，选择两张自己的手牌或装备弃置以继续造成伤害，或直接跳过。 ");
    case CardSelectionPurpose::KirinBowMount: return QObject::tr("麒麟弓：请选择要弃置的坐骑，或放弃发动。 ");
    case CardSelectionPurpose::DoubleSwordDiscard: return QObject::tr("雌雄双股剑：可弃置一张手牌，否则攻击者摸一张牌。 ");
    case CardSelectionPurpose::IceSwordPrompt: return QObject::tr("寒冰剑：可发动以防止本次【杀】伤害，并弃置目标至多两张牌；也可直接跳过。 ");
    case CardSelectionPurpose::IceSwordDiscard: return QObject::tr("寒冰剑：请选择目标的一张手牌或装备弃置。 ");
    }
    return QObject::tr("请选择一张牌。 ");
}
}

MainWindow::MainWindow(IGameClient& client, network::GameServer* hostServer, std::function<void()> leaveRoom, QWidget* parent) : QMainWindow(parent), client_(client), hostServer_(hostServer), leaveRoom_(std::move(leaveRoom))
{
    setWindowTitle(tr("基础三国杀 - 玩家 %1").arg(client_.selfPlayerId())); resize(1280, 820); setMinimumSize(960, 700);
    setStyleSheet(QStringLiteral("QMainWindow{background:#e8e1d5;} QLabel{font-size:13px;} QPushButton{min-height:30px;padding:4px 10px;}"));
    auto* central = new QWidget(this); auto* rootLayout = new QVBoxLayout(central);
    pages_ = new QStackedWidget(central); lobbyPage_ = new QWidget(pages_); gamePage_ = new QWidget(pages_);
    auto* lobbyLayout = new QVBoxLayout(lobbyPage_); auto* layout = new QVBoxLayout(gamePage_);
    layout->setContentsMargins(12, 10, 12, 10); layout->setSpacing(7);
    pages_->addWidget(lobbyPage_); pages_->addWidget(gamePage_); pages_->setCurrentWidget(lobbyPage_); rootLayout->addWidget(pages_);
    auto* lobbyTitle = new QLabel(tr("基础三国杀"), lobbyPage_); lobbyTitle->setStyleSheet("font-size:22px;font-weight:600;"); lobbyLayout->addWidget(lobbyTitle);
    lobbyStatusLabel_ = new QLabel(lobbyPage_); lobbyStatusLabel_->setStyleSheet("padding:8px;background:#eef3f8;border-radius:6px;"); lobbyStatusLabel_->setWordWrap(true); lobbyLayout->addWidget(lobbyStatusLabel_);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("基础三国杀"), gamePage_); title->setStyleSheet("font-size:21px;font-weight:700;color:#472d20;"); header->addWidget(title);
    modeLabel_ = new QLabel(gamePage_); modeLabel_->setStyleSheet("color:#715b47;font-weight:600;"); header->addWidget(modeLabel_); header->addStretch();
    battleLogButton_ = new QPushButton(tr("对战日志"), gamePage_); battleLogButton_->setObjectName(QStringLiteral("battleLogButton")); header->addWidget(battleLogButton_);
    returnLobbyButton_ = new QPushButton(tr("结束游戏并返回大厅"), gamePage_); returnLobbyButton_->setVisible(false); header->addWidget(returnLobbyButton_); layout->addLayout(header);
    battleLogWindow_ = new BattleLogWindow(this);
    connect(battleLogButton_, &QPushButton::clicked, this, [this] { battleLogWindow_->show(); battleLogWindow_->raise(); battleLogWindow_->activateWindow(); });
    if (hostServer_) { lobbySection_ = new QWidget(lobbyPage_); auto* lobby = new QHBoxLayout(lobbySection_); lobby->addWidget(new QLabel(tr("总人数："), lobbySection_)); playerCountBox_ = new QComboBox(lobbySection_); for (int i = 2; i <= 8; ++i) playerCountBox_->addItem(QString::number(i), i); lobby->addWidget(playerCountBox_); lobbySummaryLabel_ = new QLabel(lobbySection_); lobby->addWidget(lobbySummaryLabel_); addAIButton_ = new QPushButton(tr("添加 AI"), lobbySection_); removeAIButton_ = new QPushButton(tr("移除 AI"), lobbySection_); lobby->addWidget(addAIButton_); lobby->addWidget(removeAIButton_); startGameButton_ = new QPushButton(tr("开始游戏"), lobbySection_); lobby->addWidget(startGameButton_); leaveRoomButton_ = new QPushButton(tr("离开房间"), lobbySection_); lobby->addWidget(leaveRoomButton_); lobby->addStretch(); lobbyLayout->addWidget(lobbySection_); lobbyLayout->addStretch(); playerCountBox_->setCurrentText(QString::number(hostServer_->targetPlayerCount())); connect(playerCountBox_, &QComboBox::currentIndexChanged, this, [this] { const int value = playerCountBox_->currentData().toInt(); if (!hostServer_->setTargetPlayerCount(value)) { playerCountBox_->setCurrentText(QString::number(hostServer_->targetPlayerCount())); lobbyStatusLabel_->setText(tr("当前真人数量超过所选总人数。")); } refresh(); }); connect(addAIButton_, &QPushButton::clicked, this, [this] { hostServer_->addAI(); refresh(); }); connect(removeAIButton_, &QPushButton::clicked, this, [this] { hostServer_->removeAI(); refresh(); }); connect(startGameButton_, &QPushButton::clicked, this, [this] { if (!hostServer_->startLobbyGame()) { lobbyStatusLabel_->setText(tr("无法开始游戏。")); return; } refresh(); }); connect(returnLobbyButton_, &QPushButton::clicked, this, [this] { if (QMessageBox::question(this, tr("返回大厅"), tr("确定结束当前对局并返回大厅吗？")) == QMessageBox::Yes) hostServer_->returnToLobby(); }); connect(hostServer_, &network::GameServer::stateChanged, this, &MainWindow::refresh); } else { leaveRoomButton_ = new QPushButton(tr("离开房间"), lobbyPage_); lobbyLayout->addWidget(leaveRoomButton_); lobbyLayout->addStretch(); }
    if (leaveRoomButton_) connect(leaveRoomButton_, &QPushButton::clicked, this, [this] { if (leaveRoom_) leaveRoom_(); });
    auto* tableColumn = new QVBoxLayout; tableColumn->setSpacing(7);
    auto* opponents = new QWidget(gamePage_); opponents->setObjectName(QStringLiteral("opponentsGrid")); opponentsLayout_ = new QGridLayout(opponents); opponentsLayout_->setContentsMargins(0,0,0,0); opponentsLayout_->setSpacing(6); tableColumn->addWidget(opponents);
    auto* state = new QHBoxLayout; turnLabel_ = new QLabel(gamePage_); phaseLabel_ = new QLabel(gamePage_); timeoutLabel_ = new QLabel(gamePage_);
    phaseLabel_->setObjectName(QStringLiteral("phaseBanner")); phaseLabel_->setStyleSheet("font-size:18px;font-weight:700;color:#7b251b;padding:7px;background:#fff4d8;border-radius:7px;");
    timeoutLabel_->setStyleSheet("font-weight:600;color:#8b2020;"); state->addWidget(phaseLabel_, 1); state->addWidget(turnLabel_); state->addWidget(timeoutLabel_); tableColumn->addLayout(state);
    actionBanner_ = new QLabel(tr("等待对局开始"), gamePage_); actionBanner_->setObjectName(QStringLiteral("actionBanner")); actionBanner_->setWordWrap(true);
    actionBanner_->setStyleSheet("font-size:16px;font-weight:700;padding:9px;background:#3f3026;color:#fff7e8;border-radius:8px;"); tableColumn->addWidget(actionBanner_);
    judgmentResultLabel_ = new QLabel(gamePage_); judgmentResultLabel_->setStyleSheet("padding:7px;background:#fff1d6;border-radius:6px;font-weight:600;"); judgmentResultLabel_->setWordWrap(true); judgmentResultLabel_->setVisible(false); tableColumn->addWidget(judgmentResultLabel_);
    equipmentEffectLabel_ = new QLabel(gamePage_); equipmentEffectLabel_->setStyleSheet("padding:7px;background:#edf8e9;border-radius:6px;font-weight:600;"); equipmentEffectLabel_->setWordWrap(true); equipmentEffectLabel_->setVisible(false); tableColumn->addWidget(equipmentEffectLabel_);
    instructionLabel_ = new QLabel(gamePage_); instructionLabel_->setWordWrap(true); instructionLabel_->setStyleSheet("font-weight:600;color:#2f4055;padding:6px;background:#edf3f8;border-radius:6px;"); tableColumn->addWidget(instructionLabel_);
    auto* selfPanel = new QWidget(gamePage_); selfPanelLayout_ = new QHBoxLayout(selfPanel); selfPanelLayout_->setContentsMargins(0,0,0,0); tableColumn->addWidget(selfPanel);
    auto* handTitle = new QLabel(tr("我的手牌"), gamePage_); handTitle->setStyleSheet("font-size:16px;font-weight:700;"); tableColumn->addWidget(handTitle);
    auto* handWidget = new QWidget(gamePage_); handLayout_ = new QHBoxLayout(handWidget); handLayout_->setContentsMargins(8,6,8,6); handLayout_->setSpacing(7);
    auto* scroll = new QScrollArea(gamePage_); scroll->setWidgetResizable(true); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(handWidget); scroll->setMinimumHeight(155); scroll->setMaximumHeight(165); tableColumn->addWidget(scroll);
    auto* actionBar = new QWidget(gamePage_); actionBar->setObjectName(QStringLiteral("actionBar")); auto* actions = new QHBoxLayout(actionBar); actions->setContentsMargins(0,0,0,0);
    confirmUseButton_ = new QPushButton(tr("确认使用"), actionBar); cancelSelectionButton_ = new QPushButton(tr("取消选择"), actionBar); serpentSpearButton_ = new QPushButton(tr("丈八蛇矛：两张手牌当【杀】"), actionBar); endPlayButton_ = new QPushButton(tr("结束出牌阶段"), actionBar); declineButton_ = new QPushButton(tr("不出"), actionBar); restartButton_ = new QPushButton(tr("重新开始"), actionBar);
    confirmUseButton_->setObjectName(QStringLiteral("primaryAction")); confirmUseButton_->setStyleSheet("QPushButton#primaryAction{background:#a83226;color:white;font-weight:700;border-radius:6px;} QPushButton#primaryAction:disabled{background:#aaa;color:#ddd;}");
    actions->addWidget(confirmUseButton_); actions->addWidget(cancelSelectionButton_); actions->addWidget(serpentSpearButton_); actions->addWidget(endPlayButton_); actions->addWidget(declineButton_); actions->addStretch(); actions->addWidget(restartButton_); tableColumn->addWidget(actionBar);
    gameOverLabel_ = new QLabel(gamePage_); gameOverLabel_->setStyleSheet("font-size:18px;font-weight:700;color:#8b2020;"); tableColumn->addWidget(gameOverLabel_);
    auto* tableContent = new QWidget(gamePage_); tableContent->setLayout(tableColumn);
    battleScroll_ = new QScrollArea(gamePage_); battleScroll_->setObjectName(QStringLiteral("mainBattleScroll")); battleScroll_->setWidgetResizable(true); battleScroll_->setFrameShape(QFrame::NoFrame); battleScroll_->setWidget(tableContent); layout->addWidget(battleScroll_, 1);
    interactionPanel_ = new InteractionPanel(battleScroll_->viewport()); interactionPanel_->raise(); battleScroll_->viewport()->installEventFilter(this); updateInteractionOverlayGeometry(); setCentralWidget(central);
    connect(confirmUseButton_, &QPushButton::clicked, this, [this] { if (!canConfirmCardUse()) return; if (serpentSpearModeActive()) { std::vector<CardId> materials; for (const auto& id : serpentSpearMaterialIds_) materials.push_back(id.toStdString()); if (serpentSpearResponseActive()) client_.submit(RespondVirtualSlashAction {client_.selfPlayerId(), *serpentSpearResponseRequestId_, std::move(materials)}); else { std::vector<PlayerId> targets; for (const auto& target : selectedTargetIds_) targets.push_back(target.toInt()); client_.submit(PlayVirtualSlashAction {client_.selfPlayerId(), std::move(materials), std::move(targets)}); } clearSerpentSpearSelection(); refresh(); return; } std::vector<PlayerId> targets; for (const auto& target : selectedTargetIds_) targets.push_back(target.toInt()); if (selectedCardType_ && *selectedCardType_ == CardType::BorrowedSword && targets.size() == 2) { const auto& players = client_.view().players; const auto hasWeapon = [&players](PlayerId id) { const auto it = std::find_if(players.begin(), players.end(), [id](const auto& p) { return p.id == id; }); return it != players.end() && it->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)].has_value(); }; if (!hasWeapon(targets.front()) && hasWeapon(targets.back())) std::swap(targets.front(), targets.back()); } client_.submit(PlayCardAction {client_.selfPlayerId(), selectedCardId_.toStdString(), std::move(targets)}); clearCardSelection(); refresh(); });
    connect(cancelSelectionButton_, &QPushButton::clicked, this, [this] { clearSerpentSpearSelection(); clearCardSelection(); clearDiscardSelection(); refresh(); });
    connect(serpentSpearButton_, &QPushButton::clicked, this, [this] { beginSerpentSpear(); refresh(); });
    connect(endPlayButton_, &QPushButton::clicked, this, [this] { const auto& v = client_.view(); if (v.currentTurnPlayer != v.selfPlayerId) return; if (v.currentPhase == Phase::Play) { clearCardSelection(); client_.submit(EndPlayPhaseAction {v.selfPlayerId}); } else if (v.currentPhase == Phase::Discard) { std::vector<CardId> ids; for (const auto& id : selectedDiscardCardIdsFromUi()) ids.push_back(id.toStdString()); client_.submit(DiscardAction {v.selfPlayerId, std::move(ids)}); clearDiscardSelection(); } refresh(); });
    connect(declineButton_, &QPushButton::clicked, this, [this] { if (const auto& r = client_.view().response; r && r->isResponder) client_.submit(RespondAction {client_.selfPlayerId(), r->requestId, std::nullopt}); refresh(); });
    connect(restartButton_, &QPushButton::clicked, this, [this] { client_.restart(); clearCardSelection(); refresh(); });
    auto* timer = new QTimer(this); connect(timer, &QTimer::timeout, this, &MainWindow::refresh); timer->start(150); refresh();
}

void MainWindow::refresh()
{
    client_.refresh(); const auto& v = client_.view(); bool started = false; if (hostServer_) started = hostServer_->gameStarted(); else if (const auto* remote = dynamic_cast<const network::NetworkGameClient*>(&client_)) started = remote->lobbyState().gameStarted;
    if (!started) { clearSerpentSpearSelection(); clearCardSelection(); clearDiscardSelection(); selectedSelectionOptionIds_.clear(); selectedResponseCardId_.clear(); activeCardSelectionRequestId_.reset(); activeResponseRequestId_.reset(); lastDisplayedEquipmentEffectEventId_ = 0; lastVisibleLogCount_ = 0; lastRecentLog_.clear(); equipmentEffectLabel_->clear(); equipmentEffectLabel_->setVisible(false); timeoutLabel_->clear(); turnLabel_->clear(); phaseLabel_->clear(); actionBanner_->clear(); judgmentResultLabel_->clear(); judgmentResultLabel_->setVisible(false); instructionLabel_->clear(); gameOverLabel_->clear(); clearLayout(opponentsLayout_); clearLayout(selfPanelLayout_); clearLayout(handLayout_); interactionPanel_->reset(); battleLogWindow_->clearEntries(); battleLogWindow_->hide(); interactionSignature_.clear(); pages_->setCurrentWidget(lobbyPage_); refreshLobbyPage(); return; }
    QStringList newEffects;
    for (const auto& event : newEquipmentEffects(v.equipmentEffects, lastDisplayedEquipmentEffectEventId_)) {
        const auto text = formatEquipmentEffect(event, playerNameFor(v, event.targetId));
        if (!text.isEmpty()) newEffects << text;
    }
    if (!newEffects.isEmpty()) { equipmentEffectLabel_->setText(newEffects.join('\n')); equipmentEffectLabel_->setVisible(true); }
    pages_->setCurrentWidget(gamePage_); modeLabel_->setText(v.gameMode == GameMode::Identity ? tr("身份模式") : tr("自由混战")); if (hostServer_) returnLobbyButton_->setVisible(v.gameOver); if (!serpentSpearEquipped() || v.gameOver || v.cardSelection || (serpentSpearResponseRequestId_ && (!v.response || !v.response->isResponder || v.response->requestId != *serpentSpearResponseRequestId_)) || (!serpentSpearResponseRequestId_ && cardInteractionState_ >= CardInteractionState::SelectingSerpentSpearCards && (v.response || v.currentPhase != Phase::Play || v.currentTurnPlayer != v.selfPlayerId))) clearSerpentSpearSelection(); if (v.currentPhase != Phase::Play || v.response || v.cardSelection || v.gameOver || v.currentTurnPlayer != v.selfPlayerId) clearCardSelection(); if (v.currentPhase != Phase::Discard || v.currentTurnPlayer != v.selfPlayerId) clearDiscardSelection();
    if (v.response && v.response->isResponder && v.response->type == ResponseType::Nullification
        && v.response->selectableCards.empty() && !client_.actionPending()) {
        const auto requestId = v.response->requestId;
        QTimer::singleShot(0, this, [this, requestId] {
            const auto& current = client_.view().response;
            if (current && current->isResponder && current->type == ResponseType::Nullification
                && current->requestId == requestId && current->selectableCards.empty() && !client_.actionPending())
                client_.submit(RespondAction {client_.selfPlayerId(), requestId, std::nullopt});
        });
    }
    rebuildPlayers();
    if (v.judgment) {
        const auto& judgment = *v.judgment;
        const auto source = judgment.delayedTrickId == "bagua" ? tr("八卦阵判定") : tr("%1 判定").arg(cardTypeToDisplayName(judgment.delayedTrickType));
        QString result;
        if (judgment.delayedTrickId == "bagua") result = judgment.succeeded ? tr("判定成功，视为打出【闪】") : tr("判定失败，请打出【闪】");
        else if (judgment.delayedTrickType == CardType::Indulgence) result = judgment.succeeded ? tr("判定成功，正常出牌") : tr("判定失败，跳过出牌阶段");
        else if (judgment.delayedTrickType == CardType::SupplyShortage) result = judgment.succeeded ? tr("判定成功，正常摸牌") : tr("判定失败，跳过摸牌阶段");
        else result = judgment.succeeded ? tr("判定命中，造成雷电伤害") : tr("判定未命中，闪电将转移或弃置");
        judgmentResultLabel_->setText(tr("%1（%2）：判定牌 %3；%4").arg(source, playerNameFor(v, judgment.playerId), cardLabel(judgment.card), result)); judgmentResultLabel_->setVisible(true);
    } else { judgmentResultLabel_->clear(); judgmentResultLabel_->setVisible(false); }
    const auto turn = std::find_if(v.players.begin(), v.players.end(), [&v](const auto& p) { return p.id == v.currentTurnPlayer; }); const QString turnName = turn == v.players.end() ? tr("未知玩家") : playerName(*turn); turnLabel_->setText(tr("第 %1 回合").arg(v.turnNumber)); phaseLabel_->setText(tr("【%1的%2】").arg(v.currentTurnPlayer == v.selfPlayerId ? tr("你") : turnName, phaseToDisplayName(v.currentPhase))); if (v.timeoutKind == TimeoutKind::None || v.gameOver) timeoutLabel_->clear(); else { const int seconds = std::max(0, (v.timeoutRemainingMs + 999) / 1000); const QString prefix = v.timeoutKind == TimeoutKind::Play ? tr("出牌剩余") : v.timeoutKind == TimeoutKind::Discard ? tr("弃牌剩余") : v.timeoutKind == TimeoutKind::Selection ? tr("选择剩余") : v.response && v.response->type == ResponseType::PeachRescue ? tr("救援响应剩余") : tr("响应剩余"); timeoutLabel_->setText(tr("%1：%2 秒").arg(prefix).arg(seconds)); }
    QStringList logs; QStringList recentLogs; for (const auto& entry : v.visibleLogs) { logs << logEntryToDisplay(entry); if (showLogEntryInRecentEvent(entry)) recentLogs << logs.back(); } battleLogWindow_->setEntries(logs); const QString newestLog = recentLogs.isEmpty() ? QString() : recentLogs.back(); if (v.visibleLogs.size() != lastVisibleLogCount_ || newestLog != lastRecentLog_) { lastVisibleLogCount_ = v.visibleLogs.size(); lastRecentLog_ = newestLog; const qsizetype first = std::max<qsizetype>(0, recentLogs.size() - 3); actionBanner_->setText(recentLogs.mid(first).join(QLatin1Char('\n'))); }
    QString signature = selectedCardId_ + '|' + QString::number(int(cardInteractionState_)) + "|p" + QString::number(int(v.currentPhase)) + "|t" + QString::number(v.currentTurnPlayer) + "|rc" + selectedResponseCardId_; for (const auto& c : v.ownHand) signature += '|' + QString::fromStdString(c.id); if (v.response) { signature += "|r" + QString::number(v.response->requestId); for (const auto& card : v.response->selectableCards) signature += "q" + QString::fromStdString(card.id); } if (v.cardSelection) { signature += "|s" + QString::number(v.cardSelection->requestId) + "x" + QString::number(v.cardSelection->options.size()); for (const auto& option : v.cardSelection->options) signature += "o" + QString::number(option.optionId); } if (v.harvest) for (const auto& card : v.harvest->pool) signature += "h" + QString::fromStdString(card.id); for (const auto& id : selectedTargetIds_) signature += "|t" + id; for (const auto& id : serpentSpearMaterialIds_) signature += "|m" + id;
    if (signature != interactionSignature_) { interactionSignature_ = signature; rebuildHand(); rebuildTargets(); rebuildCardSelection(); }
    updateControls(); updateInteractionOverlayGeometry();
}

void MainWindow::refreshLobbyPage()
{
    if (!hostServer_) {
        QString text = QString::fromStdString(client_.connectionStatus());
        if (const auto* remote = dynamic_cast<const network::NetworkGameClient*>(&client_)) text += lobbyRosterText(remote->lobbyState());
        lobbyStatusLabel_->setText(text);
        return;
    }

    lobbySection_->setVisible(true);
    playerCountBox_->setVisible(true);
    lobbySummaryLabel_->setVisible(true);
    startGameButton_->setVisible(true);
    playerCountBox_->setCurrentText(QString::number(hostServer_->targetPlayerCount()));
    playerCountBox_->setEnabled(!hostServer_->gameStarted());
    addAIButton_->setEnabled(!hostServer_->gameStarted() && hostServer_->targetPlayerCount() < 8);
    removeAIButton_->setEnabled(!hostServer_->gameStarted() && hostServer_->aiPlayerCount() > 0);
    lobbySummaryLabel_->setText(tr("真人玩家：%1   AI 补位：%2   当前模式：%3")
        .arg(hostServer_->connectedHumanCount())
        .arg(hostServer_->aiPlayerCount())
        .arg(hostServer_->lobbyState().gameMode == GameMode::Identity ? tr("身份模式：主公 / 忠臣 / 反贼 / 内奸") : tr("自由混战：所有玩家各自为战，最后存活者获胜")));
    startGameButton_->setEnabled(!hostServer_->gameStarted());
    const auto addresses = network::GameServer::discoverLanIpv4Addresses();
    QString addressText;
    for (const auto& address : addresses) addressText += tr("\n局域网地址：%1:%2").arg(address).arg(hostServer_->serverPort());
    if (addressText.isEmpty()) addressText = tr("\n未发现可用局域网 IPv4；仍可使用本机地址 127.0.0.1:%1。").arg(hostServer_->serverPort());
    const auto roster = lobbyRosterText(hostServer_->lobbyState());
    lobbyStatusLabel_->setText(tr("主机已启动，监听端口：%1。等待玩家加入；可由房主开始游戏。%2\n首次局域网连接时，请在 Windows 防火墙提示中允许“专用网络”。")
        .arg(hostServer_->serverPort()).arg(addressText) + roster);
    returnLobbyButton_->setVisible(false);
}

void MainWindow::rebuildPlayers()
{
    clearLayout(opponentsLayout_); clearLayout(selfPanelLayout_);
    const auto& view = client_.view(); const bool identityMode = view.gameMode == GameMode::Identity;
    int opponentIndex = 0;
    for (const auto& player : view.players) {
        auto* panel = new PlayerPanel(gamePage_);
        panel->setPlayer(player, identityMode, player.id == view.selfPlayerId, player.id == view.currentTurnPlayer);
        if (player.id == view.selfPlayerId) { selfPanelLayout_->addWidget(panel); selfPanelLayout_->addStretch(); }
        else { opponentsLayout_->addWidget(panel, opponentIndex / 4, opponentIndex % 4); ++opponentIndex; }
    }
}

void MainWindow::rebuildHand()
{
    clearLayout(handLayout_);
    const auto& v = client_.view();
    for (const auto& c : v.ownHand) {
        const QString id = QString::fromStdString(c.id);
        auto* b = new CardButton(c);
        b->setProperty("cardId", id);
        b->setToolTip(cardTooltip(c));
        if (serpentSpearModeActive()) {
            b->setEnabled(true);
            b->setCheckable(true);
            b->setChecked(serpentSpearMaterialIds_.contains(id));
            connect(b, &QPushButton::clicked, this, [this, id] {
                if (serpentSpearMaterialIds_.contains(id)) {
                    serpentSpearMaterialIds_.remove(id);
                    cardInteractionState_ = CardInteractionState::SelectingSerpentSpearCards;
                } else if (serpentSpearMaterialIds_.size() < 2) {
                    serpentSpearMaterialIds_.insert(id);
                    if (serpentSpearMaterialIds_.size() == 2 && !serpentSpearResponseActive()) cardInteractionState_ = CardInteractionState::SelectingSerpentSpearTarget;
                }
                refresh();
            });
        } else if (v.response && v.response->isResponder) {
            const bool ok = std::any_of(v.response->selectableCards.begin(), v.response->selectableCards.end(), [&c](const auto& item) { return item.id == c.id; });
            const bool nullification = v.response->type == ResponseType::Nullification;
            b->setEnabled(ok && !nullification);
            if (ok && !nullification) connect(b, &QPushButton::clicked, this, [this, id, requestId = v.response->requestId] { client_.submit(RespondAction {client_.selfPlayerId(), requestId, id.toStdString()}); refresh(); });
        } else if (v.currentPhase == Phase::Discard && v.currentTurnPlayer == v.selfPlayerId) {
            b->setCheckable(true); b->setChecked(selectedDiscardIds_.contains(id));
            connect(b, &QPushButton::toggled, this, [this, id](bool checked) { if (checked) selectedDiscardIds_.insert(id); else selectedDiscardIds_.remove(id); updateControls(); });
        } else {
            const bool ok = !v.response && !v.cardSelection && v.currentPhase == Phase::Play && v.currentTurnPlayer == v.selfPlayerId && cardCanBeActivelyUsed(c.type);
            b->setEnabled(ok); b->setCheckable(ok); b->setChecked(selectedCardId_ == id);
            if (ok) connect(b, &QPushButton::clicked, this, [this, id, c] { selectCard(id, c.type); refresh(); });
        }
        handLayout_->addWidget(b);
    }
    handLayout_->addStretch();
}

void MainWindow::rebuildTargets()
{
    interactionPanel_->reset();
    if (cardInteractionState_ == CardInteractionState::SelectingSerpentSpearTarget && serpentSpearMaterialIds_.size() == 2) {
        interactionPanel_->setContext(tr("丈八蛇矛"), tr("请选择【杀】的目标。"), tr("已选材料：2 / 2；目标：%1 / 1").arg(selectedTargetIds_.size()));
        const auto& legal = client_.view().serpentSpearSlashTargets;
        int index = 0;
        for (const auto& target : client_.view().players) {
            if (!target.alive || std::find(legal.begin(), legal.end(), target.id) == legal.end()) continue;
            const QString id = QString::number(target.id);
            auto* panel = new CompactTargetCard(target, client_.view().gameMode == GameMode::Identity, selectedTargetIds_.contains(id));
            connect(panel, &QPushButton::clicked, this, [this, id] { selectedTargetIds_.clear(); selectedTargetIds_.insert(id); refresh(); });
            interactionPanel_->addCandidate(panel, index / 4, index % 4); ++index;
        }
        interactionPanel_->setActions(true, canConfirmCardUse(), tr("确认使用"), [this] { confirmUseButton_->click(); }, true, [this] { cancelSelectionButton_->click(); });
        return;
    }
    if (serpentSpearModeActive()) {
        interactionPanel_->setContext(tr("丈八蛇矛"), tr("请选择两张手牌视为【杀】。"), tr("已选：%1 / 2").arg(serpentSpearMaterialIds_.size()));
        interactionPanel_->setActions(serpentSpearResponseActive(), canConfirmCardUse(), tr("确认响应"), [this] { confirmUseButton_->click(); }, true, [this] { cancelSelectionButton_->click(); });
        return;
    }
    if (!targetSelectionActive()) return;
    const bool borrowed = *selectedCardType_ == CardType::BorrowedSword;
    const int min = selectedCardMinTargets(), max = selectedCardMaxTargets();
    QString prompt = borrowed ? (selectedTargetIds_.isEmpty() ? tr("请选择持有武器的玩家。") : tr("请选择该玩家攻击范围内的目标。"))
                              : tr("请选择 %1 至 %2 名目标。").arg(min).arg(max);
    if (*selectedCardType_ == CardType::IronChain) prompt = tr("请选择 0 至 2 名角色；不选目标则重铸。");
    interactionPanel_->setContext(cardTypeToDisplayName(*selectedCardType_), prompt,
        tr("已选：%1 / %2").arg(selectedTargetIds_.size()).arg(max));
    int index = 0;
    for (const auto& target : client_.view().players) {
        const QString id = QString::number(target.id); const bool selected = selectedTargetIds_.contains(id);
        const bool valid = borrowed ? (selectedTargetIds_.isEmpty() ? client_.canPlayCardOnTarget(selectedCardId_.toStdString(), target.id) : (!selected && target.alive))
                                    : (selected || client_.canPlayCardOnTarget(selectedCardId_.toStdString(), target.id));
        if (!valid) continue;
        auto* panel = new CompactTargetCard(target, client_.view().gameMode == GameMode::Identity, selected);
        connect(panel, &QPushButton::clicked, this, [this, id, borrowed] {
            if (borrowed) {
                if (selectedTargetIds_.contains(id)) selectedTargetIds_.clear();
                else if (selectedTargetIds_.size() < 2) selectedTargetIds_.insert(id);
                else { selectedTargetIds_.clear(); selectedTargetIds_.insert(id); }
                cardInteractionState_ = selectedTargetIds_.size() == 2 ? CardInteractionState::ReadyToConfirm : CardInteractionState::SelectingTarget;
            } else {
                if (selectedTargetIds_.contains(id)) selectedTargetIds_.remove(id);
                else if (selectedTargetIds_.size() < selectedCardMaxTargets()) selectedTargetIds_.insert(id);
                cardInteractionState_ = selectedTargetIds_.isEmpty() ? CardInteractionState::SelectingTarget : CardInteractionState::ReadyToConfirm;
            }
            refresh();
        });
        interactionPanel_->addCandidate(panel, index / 4, index % 4); ++index;
    }
    interactionPanel_->setActions(true, canConfirmCardUse(), tr("确认使用"), [this] { confirmUseButton_->click(); }, true, [this] { cancelSelectionButton_->click(); });
}

void MainWindow::rebuildCardSelection()
{
    const auto& selection = client_.view().cardSelection;
    if (!selection) { selectedSelectionOptionIds_.clear(); activeCardSelectionRequestId_.reset(); rebuildNullificationInteraction(); return; }
    interactionPanel_->reset();
    if (activeCardSelectionRequestId_ != selection->requestId) { selectedSelectionOptionIds_.clear(); activeCardSelectionRequestId_ = selection->requestId; }
    QString title;
    switch (selection->purpose) {
    case CardSelectionPurpose::Dismantlement: title = tr("过河拆桥"); break;
    case CardSelectionPurpose::Snatch: title = tr("顺手牵羊"); break;
    case CardSelectionPurpose::Harvest: title = tr("五谷丰登"); break;
    case CardSelectionPurpose::FireAttackReveal:
    case CardSelectionPurpose::FireAttackDiscard: title = tr("火攻"); break;
    case CardSelectionPurpose::AxeDiscard: title = tr("贯石斧"); break;
    case CardSelectionPurpose::KirinBowMount: title = tr("麒麟弓"); break;
    case CardSelectionPurpose::DoubleSwordDiscard: title = tr("雌雄双股剑"); break;
    case CardSelectionPurpose::IceSwordPrompt:
    case CardSelectionPurpose::IceSwordDiscard: title = tr("寒冰剑"); break;
    }
    QString detail = tr("已选：%1；要求：%2 至 %3 项").arg(selectedSelectionOptionIds_.size()).arg(selection->minCount).arg(selection->maxCount);
    if (selection->targetId) detail += tr("；当前对象：%1").arg(playerNameFor(client_.view(), selection->targetId));
    if (client_.view().harvest) detail += tr("；当前选择者：%1").arg(playerNameFor(client_.view(), client_.view().harvest->currentPicker));
    if (client_.view().fireAttack) detail += tr("；已展示：%1").arg(cardLabel(client_.view().fireAttack->revealedCard));
    interactionPanel_->setContext(title, selection->prompt.empty() ? selectionPrompt(selection->purpose) : localizedPrompt(selection->prompt), detail);
    int hiddenIndex = 0;
    for (int index = 0; index < static_cast<int>(selection->options.size()); ++index) {
        const auto& option = selection->options[static_cast<std::size_t>(index)];
        QPushButton* candidate = nullptr;
        if (selection->purpose == CardSelectionPurpose::Harvest && client_.view().harvest && index < static_cast<int>(client_.view().harvest->pool.size())) {
            candidate = new CardButton(client_.view().harvest->pool[static_cast<std::size_t>(index)]);
            candidate->setProperty("interactionCard", true);
        } else {
            const QString label = option.hidden ? tr("匿名手牌 %1").arg(++hiddenIndex)
                : option.equipmentSlot ? tr("%1：%2").arg(equipmentSlotToDisplayName(*option.equipmentSlot), localizedOptionName(option.displayName))
                : option.cardType ? cardTypeToDisplayName(*option.cardType) : localizedOptionName(option.displayName);
            candidate = new QPushButton(label);
            candidate->setObjectName(QStringLiteral("optionTile"));
            candidate->setMinimumSize(112, 70); candidate->setMaximumWidth(180);
            candidate->setStyleSheet(QStringLiteral("QPushButton#optionTile{background:#fffaf0;border:2px solid #b59a72;border-radius:8px;padding:8px;} QPushButton#optionTile:checked{background:#ffe2a8;border-color:#a83226;}"));
        }
        candidate->setProperty("selectionOptionId", QVariant::fromValue<qulonglong>(option.optionId));
        candidate->setCheckable(true); candidate->setChecked(selectedSelectionOptionIds_.contains(option.optionId));
        connect(candidate, &QPushButton::clicked, this, [this, optionId = option.optionId, max = selection->maxCount] {
            if (selectedSelectionOptionIds_.contains(optionId)) selectedSelectionOptionIds_.remove(optionId);
            else if (selectedSelectionOptionIds_.size() < max) selectedSelectionOptionIds_.insert(optionId);
            rebuildCardSelection();
        });
        interactionPanel_->addCandidate(candidate, index / 7, index % 7);
    }
    const auto submit = [this, requestId = selection->requestId](bool empty) {
        std::vector<SelectionOptionId> ids; if (!empty) for (const auto id : selectedSelectionOptionIds_) ids.push_back(id);
        client_.submitCardSelection(requestId, ids); selectedSelectionOptionIds_.clear(); activeCardSelectionRequestId_.reset(); refresh();
    };
    const bool valid = selectedSelectionOptionIds_.size() >= selection->minCount && selectedSelectionOptionIds_.size() <= selection->maxCount;
    interactionPanel_->setActions(true, valid && (!selectedSelectionOptionIds_.isEmpty() || selection->minCount > 0), tr("确认选择"), [submit] { submit(false); },
        true, [this] { selectedSelectionOptionIds_.clear(); rebuildCardSelection(); }, selection->minCount == 0, [submit] { submit(true); });
}

void MainWindow::rebuildNullificationInteraction()
{
    const auto& response = client_.view().response;
    if (!response || !response->isResponder || response->type != ResponseType::Nullification
        || response->selectableCards.empty()) {
        selectedResponseCardId_.clear(); activeResponseRequestId_.reset(); return;
    }
    if (activeResponseRequestId_ != response->requestId) {
        activeResponseRequestId_ = response->requestId; selectedResponseCardId_.clear();
    }
    interactionPanel_->reset();
    QString detail;
    if (client_.view().nullification) {
        const auto& context = *client_.view().nullification;
        detail = tr("锦囊：【%1】；来源：%2；目标：%3；无懈链第 %4 轮")
            .arg(cardTypeToDisplayName(context.trickType), playerNameFor(client_.view(), context.sourceId),
                 context.targetId ? playerNameFor(client_.view(), *context.targetId) : tr("全体或无指定对象"))
            .arg(context.chainRound);
    }
    interactionPanel_->setContext(tr("无懈可击"), tr("是否使用【无懈可击】？"), detail);
    int index = 0;
    for (const auto& card : response->selectableCards) {
        const QString id = QString::fromStdString(card.id);
        auto* candidate = new CardButton(card);
        candidate->setSelected(selectedResponseCardId_ == id);
        connect(candidate, &QPushButton::clicked, this, [this, id] { selectedResponseCardId_ = id; rebuildNullificationInteraction(); updateControls(); });
        interactionPanel_->addCandidate(candidate, 0, index++);
    }
    interactionPanel_->setActions(true, !selectedResponseCardId_.isEmpty(), tr("使用无懈"), [this, requestId = response->requestId] {
        if (selectedResponseCardId_.isEmpty()) return;
        client_.submit(RespondAction {client_.selfPlayerId(), requestId, selectedResponseCardId_.toStdString()});
        selectedResponseCardId_.clear(); activeResponseRequestId_.reset(); refresh();
    }, false, {}, true, [this, requestId = response->requestId] {
        client_.submit(RespondAction {client_.selfPlayerId(), requestId, std::nullopt});
        selectedResponseCardId_.clear(); activeResponseRequestId_.reset(); refresh();
    });
}

void MainWindow::updateControls()
{
    const auto& v = client_.view(); const bool online = client_.connected() && !client_.actionPending(); const bool own = v.currentTurnPlayer == v.selfPlayerId; const bool discard = online && own && !v.response && !v.cardSelection && !v.gameOver && v.currentPhase == Phase::Discard; const bool selfWon = std::find(v.winningPlayers.begin(), v.winningPlayers.end(), v.selfPlayerId) != v.winningPlayers.end(); const bool spearAvailable = serpentSpearEquipped() && v.ownHand.size() >= 2 && !v.cardSelection && !v.gameOver && ((own && v.currentPhase == Phase::Play && !v.response) || serpentSpearResponseActive()); const bool complex = !interactionPanel_->isHidden(); interactionPanel_->setAvailable(online && !v.gameOver); endPlayButton_->setText(discard ? tr("确认弃牌") : tr("结束出牌阶段")); endPlayButton_->setVisible(!complex && own && !v.response && !v.cardSelection && !v.gameOver && (v.currentPhase == Phase::Play || v.currentPhase == Phase::Discard)); endPlayButton_->setEnabled(online && !serpentSpearModeActive() && ((own && !v.response && !v.cardSelection && !v.gameOver && v.currentPhase == Phase::Play) || (discard && selectedDiscardIds_.size() == client_.requiredDiscardCount()))); confirmUseButton_->setVisible(!complex && !selectedCardId_.isEmpty()); confirmUseButton_->setEnabled(online && canConfirmCardUse()); cancelSelectionButton_->setVisible(!complex && cardInteractionState_ != CardInteractionState::Idle); cancelSelectionButton_->setEnabled(!v.gameOver && !client_.actionPending() && (cardInteractionState_ != CardInteractionState::Idle || serpentSpearModeActive())); serpentSpearButton_->setVisible(!complex && serpentSpearEquipped() && !v.gameOver); serpentSpearButton_->setEnabled(online && spearAvailable && !serpentSpearModeActive()); declineButton_->setVisible(!complex && v.response && v.response->isResponder && v.response->type != ResponseType::Nullification); declineButton_->setEnabled(online && !v.gameOver && v.response && v.response->isResponder); restartButton_->setVisible(v.gameOver && selfWon);
        if (!online) { gameOverLabel_->clear(); instructionLabel_->setText(localizedPrompt(client_.connectionStatus())); return; } if (v.gameOver) { QStringList winners; for (const auto id : v.winningPlayers) winners << playerNameFor(v, id); gameOverLabel_->setText(tr("游戏结束\n获胜阵营：%1\n获胜玩家：%2").arg(winningSideToDisplayName(v.winningSide), winners.isEmpty() ? tr("无") : winners.join(QStringLiteral("、")))); instructionLabel_->setText(hostServer_ ? tr("请点击“结束游戏并返回大厅”。") : tr("游戏已结束，等待主机返回大厅。")); return; } gameOverLabel_->clear(); if (serpentSpearModeActive()) { instructionLabel_->setText(tr("当前正在进行丈八蛇矛选择，详情见交互面板。")); } else if (v.response) { const auto& r = *v.response; if (r.type == ResponseType::Nullification) { instructionLabel_->setText(r.isResponder && !r.selectableCards.empty() ? tr("你可以响应【无懈可击】，请在交互面板中选择。") : tr("锦囊结算中。")); } else instructionLabel_->setText(r.isResponder ? localizedPrompt(r.prompt) : tr("等待%1响应。 ").arg(playerNameFor(v, r.responder))); } else if (v.harvest) { instructionLabel_->setText(tr("五谷丰登：当前由 %1 选择，候选牌见交互面板。").arg(playerNameFor(v, v.harvest->currentPicker))); } else if (v.cardSelection) { instructionLabel_->setText(tr("当前选择详情见交互面板。")); } else if (!own) instructionLabel_->setText(tr("等待当前玩家操作。")); else if (discard) instructionLabel_->setText(tr("请选择 %1 张牌弃置，已选择 %2 张。").arg(client_.requiredDiscardCount()).arg(selectedDiscardIds_.size())); else if (cardInteractionState_ == CardInteractionState::SelectingTarget) instructionLabel_->setText(tr("请选择目标。")); else if (cardInteractionState_ == CardInteractionState::ReadyToConfirm) instructionLabel_->setText(tr("点击“确认使用”提交行动。")); else instructionLabel_->setText(tr("请选择一张可主动使用的手牌。"));
}

void MainWindow::updateInteractionOverlayGeometry()
{
    if (!battleScroll_ || !interactionPanel_) return;
    interactionPanel_->fitToViewport(battleScroll_->viewport()->size());
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (battleScroll_ && watched == battleScroll_->viewport() && event->type() == QEvent::Resize)
        updateInteractionOverlayGeometry();
    return QMainWindow::eventFilter(watched, event);
}

QSet<QString> MainWindow::selectedDiscardCardIdsFromUi() const { QSet<QString> ids; for (int index = 0; index < handLayout_->count(); ++index) if (auto* button = qobject_cast<QPushButton*>(handLayout_->itemAt(index)->widget()); button && button->isChecked()) ids.insert(button->property("cardId").toString()); return ids; }
void MainWindow::clearDiscardSelection() { selectedDiscardIds_.clear(); for (int index = 0; index < handLayout_->count(); ++index) if (auto* button = qobject_cast<QPushButton*>(handLayout_->itemAt(index)->widget()); button && button->isCheckable()) button->setChecked(false); }
int MainWindow::selectedCardMinTargets() const { for (const auto& card : client_.view().ownHand) if (QString::fromStdString(card.id) == selectedCardId_) return card.minTargets; return 0; }
int MainWindow::selectedCardMaxTargets() const { for (const auto& card : client_.view().ownHand) if (QString::fromStdString(card.id) == selectedCardId_) return card.maxTargets; return 1; }
bool MainWindow::canConfirmCardUse() const { const auto& v = client_.view(); if (serpentSpearModeActive()) { if (serpentSpearMaterialIds_.size() != 2) return false; if (serpentSpearResponseActive()) return serpentSpearResponseRequestId_ && v.response && v.response->requestId == *serpentSpearResponseRequestId_; return cardInteractionState_ == CardInteractionState::SelectingSerpentSpearTarget && selectedTargetIds_.size() == 1 && std::find(v.serpentSpearSlashTargets.begin(), v.serpentSpearSlashTargets.end(), (*selectedTargetIds_.cbegin()).toInt()) != v.serpentSpearSlashTargets.end(); } if (selectedCardId_.isEmpty() || !selectedCardType_ || v.response || v.cardSelection || v.gameOver || v.currentPhase != Phase::Play || v.currentTurnPlayer != v.selfPlayerId) return false; if (*selectedCardType_ == CardType::BorrowedSword) return selectedTargetIds_.size() == 2 && client_.canPlayCard(selectedCardId_.toStdString()); if (!cardRequiresTarget(*selectedCardType_)) return client_.canPlayCard(selectedCardId_.toStdString()); if (selectedTargetIds_.size() < selectedCardMinTargets() || selectedTargetIds_.size() > selectedCardMaxTargets()) return false; return std::all_of(selectedTargetIds_.begin(), selectedTargetIds_.end(), [this](const QString& id) { return client_.canPlayCardOnTarget(selectedCardId_.toStdString(), id.toInt()); }); }
bool MainWindow::targetSelectionActive() const { const auto& v = client_.view(); return !selectedCardId_.isEmpty() && selectedCardType_ && cardRequiresTarget(*selectedCardType_) && v.currentPhase == Phase::Play && v.currentTurnPlayer == v.selfPlayerId && !v.response && !v.cardSelection && !v.gameOver; }
void MainWindow::clearCardSelection() { selectedCardId_.clear(); selectedCardType_.reset(); selectedTargetIds_.clear(); if (!serpentSpearModeActive()) cardInteractionState_ = CardInteractionState::Idle; }
void MainWindow::beginSerpentSpear() { if (!serpentSpearEquipped()) return; const auto& v = client_.view(); const bool response = serpentSpearResponseActive(); if (!response && (v.currentTurnPlayer != v.selfPlayerId || v.currentPhase != Phase::Play || v.response || v.cardSelection)) return; clearCardSelection(); serpentSpearMaterialIds_.clear(); selectedTargetIds_.clear(); serpentSpearResponseRequestId_ = response ? std::optional<qulonglong>(v.response->requestId) : std::nullopt; cardInteractionState_ = CardInteractionState::SelectingSerpentSpearCards; }
void MainWindow::clearSerpentSpearSelection() { serpentSpearMaterialIds_.clear(); serpentSpearResponseRequestId_.reset(); if (cardInteractionState_ == CardInteractionState::SelectingSerpentSpearCards || cardInteractionState_ == CardInteractionState::SelectingSerpentSpearTarget) { selectedTargetIds_.clear(); cardInteractionState_ = CardInteractionState::Idle; } }
bool MainWindow::serpentSpearEquipped() const { const auto& v = client_.view(); const auto it = std::find_if(v.players.begin(), v.players.end(), [&v](const auto& player) { return player.id == v.selfPlayerId; }); return it != v.players.end() && it->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] && it->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]->displayName == "丈八蛇矛"; }
bool MainWindow::serpentSpearResponseActive() const { const auto& response = client_.view().response; return response && response->isResponder && (response->type == ResponseType::Slash || response->type == ResponseType::BorrowedSwordSlash); }
bool MainWindow::serpentSpearModeActive() const { return cardInteractionState_ == CardInteractionState::SelectingSerpentSpearCards || cardInteractionState_ == CardInteractionState::SelectingSerpentSpearTarget; }
void MainWindow::selectCard(const QString& id, CardType type) { if (selectedCardId_ == id) { clearCardSelection(); return; } clearCardSelection(); selectedCardId_ = id; selectedCardType_ = type; cardInteractionState_ = cardRequiresTarget(type) ? CardInteractionState::SelectingTarget : client_.canPlayCard(id.toStdString()) ? CardInteractionState::ReadyToConfirm : CardInteractionState::CardSelected; }
bool MainWindow::cardRequiresTarget(CardType type) { return type == CardType::Slash || type == CardType::FireSlash || type == CardType::ThunderSlash || type == CardType::Dismantlement || type == CardType::Snatch || type == CardType::Duel || type == CardType::BorrowedSword || type == CardType::IronChain || type == CardType::FireAttack || type == CardType::Indulgence || type == CardType::SupplyShortage || type == CardType::Lightning; }
bool MainWindow::cardCanBeActivelyUsed(CardType type) { return type != CardType::Dodge; }
} // namespace sanguosha::ui
