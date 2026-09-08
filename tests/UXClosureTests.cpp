#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "ui/GameWidgets.h"
#include "ui/GameText.h"
#include "ui/BattleLogWindow.h"
#include "ui/InteractionPanel.h"
#include "ui/ActionReadability.h"
#include "ui/AppSettings.h"
#include "ui/MainWindow.h"
#include "client/GameClientController.h"
#include "network/GameServer.h"

using namespace sanguosha;
using namespace sanguosha::ui;

namespace {
void expect(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BasicSanguoshaTests"));
    QCoreApplication::setApplicationName(QStringLiteral("UXClosure"));

    QTemporaryDir settingsDirectory;
    QSettings persisted(settingsDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    expect(AppSettings::readTheme(persisted) == ThemeMode::System
               && AppSettings::readAISpeed(persisted) == ai::AISpeedPreset::Standard,
           "theme defaults to System and AI speed defaults to Standard");
    AppSettings::writeTheme(persisted, ThemeMode::Dark);
    AppSettings::writeAISpeed(persisted, ai::AISpeedPreset::Slow);
    persisted.sync();
    QSettings reloaded(settingsDirectory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    expect(AppSettings::readTheme(reloaded) == ThemeMode::Dark
               && AppSettings::readAISpeed(reloaded) == ai::AISpeedPreset::Slow,
           "theme and AI speed survive a settings reload");
    expect(AppSettings::themeLabel(ThemeMode::System) == QStringLiteral("跟随系统")
               && AppSettings::aiSpeedLabel(ai::AISpeedPreset::Test) == QStringLiteral("测试"),
           "settings expose the required localized labels");
    AppSettings::applyThemeForTesting(ThemeMode::Light); const auto lightSheet = app.styleSheet();
    AppSettings::applyThemeForTesting(ThemeMode::Dark); const auto darkSheet = app.styleSheet();
    expect(!lightSheet.isEmpty() && !darkSheet.isEmpty() && lightSheet != darkSheet
               && darkSheet.contains(QStringLiteral("QFrame#interactionPanel"))
               && darkSheet.contains(QStringLiteral("QPlainTextEdit"))
               && lightSheet.contains(QStringLiteral("#8a5900")) && lightSheet.contains(QStringLiteral("#165c9c")) && lightSheet.contains(QStringLiteral("#9d201d"))
               && darkSheet.contains(QStringLiteral("#ffd15c")) && darkSheet.contains(QStringLiteral("#66b8ff")) && darkSheet.contains(QStringLiteral("#ff716c")),
           "light and dark themes cover overlays, logs, cards, disabled and selected states through one application stylesheet");

    CardView card {"ux-slash", CardType::Slash, Suit::Spade, 7, "Slash", {}, 1, 1};
    CardButton cardButton(card);
    expect(cardButton.objectName() == QStringLiteral("handCard") && cardButton.isCheckable()
               && cardButton.minimumWidth() >= 100 && cardButton.minimumHeight() >= 120,
           "hand card uses a fixed card-like interactive widget");
    expect(cardButton.text().contains(QStringLiteral("杀")) && cardButton.text().contains(QStringLiteral("♠")),
           "hand card visibly separates localized name, suit, and rank");
    cardButton.setSelected(true); expect(cardButton.isChecked(), "selected card state is explicit");
    cardButton.setEnabled(false); expect(!cardButton.isEnabled(), "unavailable card state is explicit");
    CardButton mountButton(CardView {"mount", CardType::DefensiveHorse, Suit::Spade, 5, "JueYing"});
    expect(mountButton.text().contains(QStringLiteral("绝影")) && !mountButton.text().contains(QStringLiteral("JueYing")),
           "card face uses the localized equipment name rather than an internal mount token");

    PublicPlayerView player {2, "AI-2", 3, 4, true, false, true, false, 5, {}};
    player.seat = 1; player.controlType = PlayerControlType::AI;
    player.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::DefensiveHorse)] =
        CardView {"mount", CardType::DefensiveHorse, Suit::Spade, 5, "JueYing"};
    PlayerPanel panel; panel.setPlayer(player, true, false, true);
    expect(panel.objectName() == QStringLiteral("playerPanel") && panel.property("playerId").toInt() == 2
               && panel.property("currentPlayer").toBool(),
           "player panel carries authoritative seat identity and current-player highlight state");
    QString visible;
    for (const auto* label : panel.findChildren<QLabel*>()) visible += label->text();
    expect(visible.contains(QStringLiteral("身份：未知")) && visible.contains(QStringLiteral("手牌 5"))
               && visible.contains(QStringLiteral("装备：")) && !visible.contains(QStringLiteral("武器 —"))
               && !visible.contains(QStringLiteral("判定区：—")),
           "player panel keeps a compact equipment summary and hides an empty judgment row");
    expect(!visible.contains(QStringLiteral("Slash")), "opponent panel never renders hidden hand faces");
    const auto labels = panel.findChildren<QLabel*>();
    expect(visible.contains(QStringLiteral("绝影"))
               && std::any_of(labels.begin(), labels.end(), [](const auto* label) { return label->toolTip().contains(QStringLiteral("绝影")); })
               && std::none_of(labels.begin(), labels.end(), [](const auto* label) { return label->toolTip().contains(QStringLiteral("JueYing")); }),
           "equipment area shows localized mount details without exposing internal names");

    player.chained = true;
    panel.setPlayer(player, true, false, true);
    auto* chainBadge = panel.findChild<QLabel*>(QStringLiteral("chainBadge"));
    expect(chainBadge && !chainBadge->isHidden() && chainBadge->text() == QStringLiteral("【连环】")
               && chainBadge->toolTip().contains(QStringLiteral("属性伤害")) && panel.property("chained").toBool(),
           "chained player has an explicit badge, tooltip, and themeable secondary status");
    panel.setSelectable(true, true);
    expect(panel.property("selected").toBool() && panel.property("currentPlayer").toBool()
               && panel.property("chained").toBool(),
           "chain status coexists with selected and current-player priority states");
    PlayerPanel secondChainedPanel; secondChainedPanel.setPlayer(player, true, false, false);
    expect(secondChainedPanel.property("chained").toBool()
               && !secondChainedPanel.findChild<QLabel*>(QStringLiteral("chainBadge"))->isHidden(),
           "multiple players can display independent chain badges simultaneously");
    player.chained = false; panel.setPlayer(player, true, false, false);
    expect(chainBadge->isHidden() && !panel.property("chained").toBool(),
           "chain badge clears from authoritative state for reconnect or a second game");

    panel.resize(220, 150); panel.show(); app.processEvents();
    expect(panel.size().width() >= 172 && panel.minimumHeight() <= 96,
           "player panel remains readable in compact 1080p-oriented layout");

    PlayerPanel targetPanel; targetPanel.setPlayer(player, true, false, false);
    targetPanel.setSelectable(true, true);
    expect(targetPanel.property("selectable").toBool() && targetPanel.property("selected").toBool(),
           "multi-target interaction uses explicit selectable PlayerPanel state");

    QWidget targetGridHost; auto* targetGrid = new QGridLayout(&targetGridHost);
    for (int index = 0; index < 7; ++index) targetGrid->addWidget(new CompactTargetCard(player, true, index == 0), index / 4, index % 4);
    const auto targetButtons = targetGridHost.findChildren<QPushButton*>();
    expect(targetGrid->rowCount() == 2 && targetGrid->columnCount() == 4
               && std::count_if(targetButtons.begin(), targetButtons.end(), [](const auto* button) { return button->objectName() == QStringLiteral("compactTargetCard"); }) == 7,
           "seven compact target cards fit a stable 4 plus 3 grid");

    BattleLogWindow battleLog;
    expect(battleLog.objectName() == QStringLiteral("battleLogWindow")
               && battleLog.windowModality() == Qt::NonModal && battleLog.isWindow(),
           "battle log is an independent non-modal window instead of a permanent table column");
    expect(battleLog.logView()->isReadOnly(), "battle log is read-only");
    battleLog.setEntries({QStringLiteral("玩家1使用【杀】"), QStringLiteral("玩家2打出【闪】")});
    expect(battleLog.logView()->toPlainText().contains(QStringLiteral("玩家2打出【闪】")),
           "battle log receives live visible-log updates");
    battleLog.show(); app.processEvents(); battleLog.close(); app.processEvents();
    expect(!battleLog.isVisible(), "closing the battle log only hides the reusable window");
    battleLog.show(); app.processEvents();
    expect(battleLog.isVisible(), "the same battle log window can reopen");
    battleLog.clearEntries();
    expect(battleLog.logView()->toPlainText().isEmpty(), "lobby lifecycle can clear the battle log display");

    ActionReadability readability;
    readability.resize(1000, 420); readability.show(); app.processEvents();
    readability.setTestingBypass(true);
    PlayerViewState actionView {1, 1, 1, Phase::Play};
    actionView.players = {{1, "AI-1", 4, 4, true, false, true, false, 3, {}}, {2, "AI-2", 3, 4, true, false, true, false, 2, {}}};
    actionView.publicEvents = {
        {GameEventType::CardUsed, 1, 2, "Slash", 1},
        {GameEventType::CardResponded, 2, 1, "Dodge", 2},
        {GameEventType::DamageReceived, 1, 2, "1", 3}};
    readability.ingest(actionView);
    app.processEvents();
    expect(readability.objectName() == QStringLiteral("actionReadability") && readability.recentView()->isReadOnly()
               && readability.recentView()->isHidden() && readability.recentView()->objectName() == QStringLiteral("recentActionModelView")
               && readability.actionStage()->minimumHeight() >= 230 && readability.actionStage()->maximumHeight() <= 360,
           "RecentAction data remains internal while the enlarged ActionStage is the visible center");
    expect(readability.compactView()->isReadOnly() && readability.compactView()->minimumWidth() >= 240
               && readability.compactView()->maximumWidth() <= 320 && readability.compactView()->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
           "compact battle log has an independent bounded scroll surface");
    expect(readability.recentView()->toPlainText().contains(QStringLiteral("AI-1 → AI-2 使用【杀】"))
               && readability.compactView()->toPlainText().contains(QStringLiteral("AI-2 打出【闪】"))
               && readability.compactView()->toPlainText().contains(QStringLiteral("AI-2 受到 1 点伤害")),
           "structured events resolve card use, source, target, response, and damage summaries");
    expect(readability.stageSource() != 0 && readability.stageTarget() != 0 && readability.stageSource() != readability.stageTarget(),
           "ActionStage resolves the source and target line from structured events");
    expect(readability.actionStage()->findChild<QLabel*>(QStringLiteral("actionSource"))
               && readability.actionStage()->findChild<QLabel*>(QStringLiteral("displayCard"))
               && readability.actionStage()->findChild<QLabel*>(QStringLiteral("actionTarget"))
               && !readability.actionStage()->findChild<QLabel*>(QStringLiteral("actionSource"))->text().isEmpty()
               && !readability.actionStage()->findChild<QLabel*>(QStringLiteral("actionTarget"))->text().isEmpty(),
           "ActionStage renders a source to display-card to target route");
    ActionReadability publicCardStage; publicCardStage.setTestingBypass(true);
    PlayerViewState publicCardView {1, 1, 1, Phase::Play}; publicCardView.players = actionView.players;
    GameEvent publicSlash {GameEventType::CardUsed, 1, 2, "Slash", 1}; publicSlash.card = PublicEventCard {"Slash", CardType::Slash, Suit::Spade, 7}; publicCardView.publicEvents.push_back(publicSlash);
    publicCardStage.ingest(publicCardView); app.processEvents();
    const auto publicCardText = publicCardStage.actionStage()->findChild<QLabel*>(QStringLiteral("displayCard"))->text();
    expect(publicCardText.contains(QStringLiteral("杀")) && publicCardText.contains(QStringLiteral("♠"))
               && publicCardText.contains(QStringLiteral("7")) && publicCardText.contains(QStringLiteral("基本牌")),
           "display-only action card is driven by public name, suit, rank, and type metadata");
    const auto beforeVerbose = readability.compactView()->toPlainText();
    actionView.publicEvents.push_back({GameEventType::InteractionRequested, 1, 2, "selection:SECRET_CARD", 4});
    actionView.publicEvents.push_back({GameEventType::NullificationContext, 1, 2, "Harmful:0", 5});
    readability.ingest(actionView); app.processEvents();
    expect(readability.compactView()->toPlainText() == beforeVerbose
               && !readability.compactView()->toPlainText().contains(QStringLiteral("SECRET_CARD")),
           "verbose queries and hidden identifiers never enter RecentAction or compact log");
    actionView.harvest = HarvestView {2, {}, {{1, CardView {"public-harvest", CardType::Peach, Suit::Heart, 12, "Peach"}}}};
    actionView.fireAttack = FireAttackView {2, CardView {"public-fire", CardType::Dodge, Suit::Diamond, 9, "Dodge"}};
    actionView.judgment = JudgmentView {2, "indulgence", CardType::Indulgence, CardView {"public-judge", CardType::Dodge, Suit::Diamond, 9, "Dodge"}, false};
    actionView.equipmentEffects = {{1, "朱雀羽扇", EquipmentEffectTypeView::SlashConvertedToFire, 1, 2, 0, CardType::Slash}};
    readability.ingest(actionView); app.processEvents();
    expect(readability.compactView()->toPlainText().contains(QStringLiteral("五谷丰登"))
               && readability.compactView()->toPlainText().contains(QStringLiteral("火攻"))
               && readability.compactView()->toPlainText().contains(QStringLiteral("判定"))
               && readability.compactView()->toPlainText().contains(QStringLiteral("朱雀羽扇")),
           "Harvest, FireAttack, judgment, and equipment public state integrate with action presentation");
    for (std::uint64_t id = 6; id < 64; ++id) actionView.publicEvents.push_back({GameEventType::CardUsed, 1, 2, "FireAttack", id});
    readability.ingest(actionView); app.processEvents();
    expect(readability.recentView()->document()->blockCount() <= 40 && readability.compactView()->document()->blockCount() <= 80,
           "RecentAction and compact log histories remain bounded");
    auto* recentBar = readability.compactView()->verticalScrollBar(); recentBar->setValue(0); const int manualPosition = recentBar->value();
    actionView.publicEvents.push_back({GameEventType::CardResponded, 2, 1, "Nullification", 64}); readability.ingest(actionView); app.processEvents();
    expect(recentBar->value() == manualPosition,
           "manual Compact Log history review prevents forced auto-scroll");
    expect(readability.queuedCount() <= 16, "animation queue stays bounded under event bursts");
    bool timerAdvanced = false; QTimer::singleShot(0, [&] { timerAdvanced = true; }); app.processEvents();
    expect(timerAdvanced, "non-blocking animation leaves the Qt event loop and timers responsive");
    readability.resetForLobby();
    expect(readability.recentView()->toPlainText().isEmpty() && readability.compactView()->toPlainText().isEmpty()
               && readability.queuedCount() == 0 && readability.lastConsumedEventId() == 0,
           "Return to Lobby and second-game cleanup clear action presentation state");
    readability.ingest(actionView, true); app.processEvents();
    expect(readability.queuedCount() == 0 && !readability.recentView()->toPlainText().isEmpty(),
           "reconnect restores bounded histories without replaying old animations");
    actionView.publicEvents.push_back({GameEventType::GameEnded, 1, {}, "", 65}); readability.ingest(actionView); app.processEvents();
    expect(readability.queuedCount() == 0 && readability.actionStage()->findChild<QLabel*>(QStringLiteral("actionStageTitle"))->text() == QStringLiteral("游戏结束"),
           "GameOver clears stale queued actions and presents the terminal state");

    QWidget overlayHost; overlayHost.resize(1200, 760); auto* normalLayout = new QVBoxLayout(&overlayHost);
    auto* normalContent = new QLabel(QStringLiteral("固定主战场"), &overlayHost); normalLayout->addWidget(normalContent);
    const QSize baseHint = normalLayout->sizeHint();
    InteractionPanel interaction(&overlayHost);
    interaction.setContext(QStringLiteral("五谷丰登"), QStringLiteral("请选择一张牌。"), QStringLiteral("已选：0 / 1"));
    for (int i = 0; i < 20; ++i) interaction.addCandidate(new QPushButton(QString::number(i)), i / 7, i % 7);
    interaction.setActions(true, false, QStringLiteral("确认选择"), {}, true, {});
    interaction.fitToViewport(overlayHost.size());
    expect(!interaction.isHidden() && interaction.property("overlay").toBool()
               && interaction.height() <= overlayHost.height() * 55 / 100
               && interaction.width() <= overlayHost.width() * 85 / 100
               && interaction.candidateScroll()->widgetResizable() && interaction.candidateCount() == 20,
           "Harvest candidates stay in a bounded internally scrollable InteractionPanel");
    expect(normalLayout->sizeHint() == baseHint && interaction.parentWidget() == &overlayHost,
           "overlay is outside the normal layout and does not increase main content height");
    expect(!interaction.confirmButton()->isHidden(), "overlay action bar remains visible outside candidate scrolling");
    for (const QSize resolution : {QSize(1920, 1080), QSize(1920, 1200), QSize(2560, 1440)}) {
        interaction.fitToViewport(resolution);
        expect(interaction.width() <= resolution.width() * 85 / 100
                   && interaction.height() <= resolution.height() * 55 / 100
                   && interaction.geometry().left() >= 0 && interaction.geometry().top() >= 0,
               "responsive overlay stays bounded at a supported 16:9 or 16:10 resolution");
    }
    expect(!interaction.confirmButton()->isEnabled(), "interaction confirmation respects selection bounds");
    interaction.reset();
    expect(!interaction.isVisible() && interaction.candidateCount() == 0,
           "completed or cancelled interaction clears candidates and hides the panel");

    network::GameServer layoutServer;
    expect(layoutServer.setTargetPlayerCount(8) && layoutServer.startLobbyGame(),
           "real eight-player battle screen fixture starts");
    GameClientController layoutClient(layoutServer.session(), 1);
    MainWindow battleWindow(layoutClient, &layoutServer);
    battleWindow.setMinimumSize(0, 0);
    auto* topPlayers = battleWindow.findChild<QWidget*>(QStringLiteral("topPlayers"));
    auto* actionStage = battleWindow.findChild<QWidget*>(QStringLiteral("actionStage"));
    auto* compactLog = battleWindow.findChild<QWidget*>(QStringLiteral("compactBattleLog"));
    auto* localPlayer = battleWindow.findChild<QWidget*>(QStringLiteral("localPlayerPanel"));
    auto* handBar = battleWindow.findChild<QWidget*>(QStringLiteral("handBar"));
    auto* actionBar = battleWindow.findChild<QWidget*>(QStringLiteral("actionBar"));
    expect(topPlayers && actionStage && compactLog && localPlayer && handBar && actionBar
               && !battleWindow.findChild<QWidget*>(QStringLiteral("mainBattleScroll")),
           "real battle screen exposes every required region without a main vertical scroll");
    const auto within = [&battleWindow](QWidget* widget) {
        const QRect area = battleWindow.centralWidget()->rect();
        const QRect child(widget->mapTo(battleWindow.centralWidget(), QPoint(0, 0)), widget->size());
        return area.contains(child.topLeft()) && area.contains(child.bottomRight());
    };
    const auto mapped = [&battleWindow](QWidget* widget) { return QRect(widget->mapTo(battleWindow.centralWidget(), QPoint(0, 0)), widget->size()); };
    for (const QSize resolution : {QSize(1920, 1080), QSize(1920, 1200), QSize(2560, 1440)}) {
        battleWindow.resize(resolution); battleWindow.show(); app.processEvents();
        expect(within(topPlayers) && within(actionStage) && within(compactLog)
                   && within(localPlayer) && within(handBar) && within(actionBar),
               "all battle regions remain inside each supported viewport");
        expect(!mapped(actionStage).intersects(mapped(compactLog))
                   && !mapped(localPlayer).intersects(mapped(handBar))
                   && !mapped(handBar).intersects(mapped(actionBar)),
               "battle regions do not overlap at supported resolutions");
    }
    const auto countPlayerPanels = [](QWidget* root) { const auto children = root->findChildren<QFrame*>(); return std::count_if(children.begin(), children.end(), [](const auto* child) { return child->objectName() == QStringLiteral("playerPanel"); }); };
    expect(countPlayerPanels(&battleWindow) == 8
               && countPlayerPanels(topPlayers) == 7,
           "eight-player screen uses a compact four-plus-three opponent grid and separate local panel");
    auto* settingsButton = battleWindow.findChild<QPushButton*>(QStringLiteral("battleSettingsButton"));
    auto* returnButton = battleWindow.findChild<QPushButton*>(QStringLiteral("returnLobbyButton"));
    auto* logButton = battleWindow.findChild<QPushButton*>(QStringLiteral("battleLogButton"));
    expect(settingsButton && returnButton && logButton && returnButton->text() == QStringLiteral("返回大厅"),
           "battle toolbar contains settings, authoritative host return, and full log buttons");
    settingsButton->click(); app.processEvents();
    auto* settingsDialog = battleWindow.findChild<QDialog*>(QStringLiteral("battleSettingsDialog"));
    auto* battleTheme = battleWindow.findChild<QComboBox*>(QStringLiteral("battleThemeBox"));
    auto* battleSpeed = battleWindow.findChild<QComboBox*>(QStringLiteral("battleAISpeedBox"));
    expect(settingsDialog && settingsDialog->isVisible() && battleTheme && battleSpeed,
           "battle settings opens as a non-modal live panel");
    battleTheme->setCurrentIndex(battleTheme->findData(int(ThemeMode::Light)));
    battleSpeed->setCurrentIndex(battleSpeed->findData(int(ai::AISpeedPreset::Fast)));
    app.processEvents();
    expect(AppSettings::themeMode() == ThemeMode::Light && AppSettings::aiSpeedPreset() == ai::AISpeedPreset::Fast,
           "in-game theme and AI speed selections apply immediately to their supported boundaries");
    battleTheme->setCurrentIndex(battleTheme->findData(int(ThemeMode::System)));
    battleSpeed->setCurrentIndex(battleSpeed->findData(int(ai::AISpeedPreset::Standard)));
    settingsDialog->hide();
    QTimer::singleShot(0, [] { for (auto* widget : QApplication::topLevelWidgets()) if (auto* box = qobject_cast<QMessageBox*>(widget)) if (auto* yes = box->button(QMessageBox::Yes)) yes->click(); });
    returnButton->click(); app.processEvents();
    expect(!layoutServer.gameStarted() && !battleWindow.findChild<QWidget*>(QStringLiteral("interactionPanel"))->isVisible()
               && battleWindow.findChild<QPlainTextEdit*>(QStringLiteral("compactBattleLog"))->toPlainText().isEmpty(),
           "confirmed host Return to Lobby uses the authoritative flow and clears overlay and compact history");
    expect(layoutServer.startLobbyGame(), "a clean second game starts through the existing server lifecycle");
    app.processEvents();
    expect(layoutServer.gameStarted() && countPlayerPanels(topPlayers) == 7,
           "second game rebuilds the reflowed player layout without stale widgets");
    battleWindow.hide();

    QFile mainWindowSource(QStringLiteral(SANGUOSHA_SOURCE_DIR "/src/ui/MainWindow.cpp"));
    expect(mainWindowSource.open(QIODevice::ReadOnly), "main window source is available for layout contract audit");
    const QString architecture = QString::fromUtf8(mainWindowSource.readAll());
    expect(architecture.contains(QStringLiteral("battleLogButton_ = new QPushButton"))
               && !architecture.contains(QStringLiteral("logColumn"))
               && !architecture.contains(QStringLiteral("logView_")),
           "main layout has a battle-log button and no permanent log column");
    expect(architecture.contains(QStringLiteral("opponentIndex / 4"))
               && architecture.contains(QStringLiteral("actionBar"))
               && architecture.contains(QStringLiteral("topPlayers"))
               && architecture.contains(QStringLiteral("localPlayerPanel"))
               && architecture.contains(QStringLiteral("handBar"))
               && architecture.contains(QStringLiteral("new InteractionPanel(battleViewport_)"))
               && !architecture.contains(QStringLiteral("mainBattleScroll")),
           "top players, local player, hand bar, ActionBar, and overlay use a no-main-scroll battle viewport");
    expect(architecture.contains(QStringLiteral("battleSettingsButton"))
               && architecture.contains(QStringLiteral("returnLobbyButton"))
               && architecture.contains(QStringLiteral("battleLogButton"))
               && architecture.contains(QStringLiteral("battleSettingsDialog"))
               && architecture.contains(QStringLiteral("QMessageBox::question"))
               && architecture.contains(QStringLiteral("hostServer_->returnToLobby()")),
           "battle toolbar exposes log, authoritative Return to Lobby, and live settings");
    expect(architecture.contains(QStringLiteral("hostServer_ ? tr(\"返回大厅\") : tr(\"离开对局\")"))
               && architecture.contains(QStringLiteral("if (!hostServer_) connect(returnLobbyButton_")),
           "remote battle clients get leave behavior without host Return-to-Lobby authority");
    expect(!architecture.contains(QStringLiteral("recentActionView"))
               && architecture.contains(QStringLiteral("compactBattleLog")) == false,
           "MainWindow does not add a visible RecentAction duplicate");
    for (const auto purpose : {QStringLiteral("CardSelectionPurpose::Harvest"), QStringLiteral("CardSelectionPurpose::FireAttackDiscard"),
                              QStringLiteral("CardSelectionPurpose::AxeDiscard"), QStringLiteral("CardSelectionPurpose::Dismantlement"),
                              QStringLiteral("CardSelectionPurpose::Snatch")})
        expect(architecture.contains(purpose), "complex card-selection purpose maps into the unified presentation path");
    expect(architecture.contains(QStringLiteral("new CompactTargetCard"))
               && architecture.contains(QStringLiteral("serpentSpearModeActive()")),
           "multi-target and Zhangba flows use selectable player cards and the unified interaction state");
    expect(!showLogEntryInRecentEvent("Waiting for AI-3 to respond with [Nullification].")
               && showLogEntryInRecentEvent("AI-3 used [Nullification]."),
           "AI Nullification polling is hidden while an actual Nullification remains banner-worthy");
    battleLog.setEntries({QStringLiteral("等待 AI-3 响应【无懈可击】。"), QStringLiteral("AI-3 使用了【无懈可击】。")});
    expect(battleLog.logView()->toPlainText().contains(QStringLiteral("等待 AI-3"))
               && battleLog.logView()->toPlainText().contains(QStringLiteral("使用了【无懈可击】")),
           "battle log retains the complete Nullification query and actual-play history");
    expect(architecture.contains(QStringLiteral("response->selectableCards.empty()"))
               && architecture.contains(QStringLiteral("是否使用【无懈可击】？"))
               && architecture.contains(QStringLiteral("rebuildNullificationInteraction")),
           "empty human Nullification auto-skips while a legal response gets an explicit overlay");
    std::cout << "BasicSanguoshaUXTests PASS\n";
    return 0;
}
