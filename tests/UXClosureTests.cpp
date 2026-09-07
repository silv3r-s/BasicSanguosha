#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QGridLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "ui/GameWidgets.h"
#include "ui/GameText.h"
#include "ui/BattleLogWindow.h"
#include "ui/InteractionPanel.h"

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

    QFile mainWindowSource(QStringLiteral(SANGUOSHA_SOURCE_DIR "/src/ui/MainWindow.cpp"));
    expect(mainWindowSource.open(QIODevice::ReadOnly), "main window source is available for layout contract audit");
    const QString architecture = QString::fromUtf8(mainWindowSource.readAll());
    expect(architecture.contains(QStringLiteral("battleLogButton_ = new QPushButton"))
               && !architecture.contains(QStringLiteral("logColumn"))
               && !architecture.contains(QStringLiteral("logView_")),
           "main layout has a battle-log button and no permanent log column");
    expect(architecture.contains(QStringLiteral("opponentIndex / 4"))
               && architecture.contains(QStringLiteral("actionBar"))
               && architecture.contains(QStringLiteral("mainBattleScroll"))
               && architecture.contains(QStringLiteral("new InteractionPanel(battleScroll_->viewport())")),
           "player grid, ActionBar, and small-window fallback scroll remain separate layout regions");
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
