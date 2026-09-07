#include "ui/GameWidgets.h"

#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

#include "ui/GameText.h"

namespace sanguosha::ui {
namespace {
QString visibleName(const PublicPlayerView& player)
{
    return playerDisplayName(player.displayName) + (player.connected ? QString() : QStringLiteral("（已断开）"));
}

QString equipmentText(const PublicPlayerView& player, EquipmentSlot slot)
{
    const auto& card = player.equipment.cardsBySlot[static_cast<std::size_t>(slot)];
    if (!card) return QStringLiteral("—");
    Card value(card->id, card->displayName, card->type, card->suit, card->rank);
    return QStringLiteral("%1 %2%3").arg(cardDisplayName(value), suitToSymbol(card->suit), rankToDisplayName(card->rank));
}

QString equipmentTooltip(const PublicPlayerView& player, EquipmentSlot slot)
{
    const auto& card = player.equipment.cardsBySlot[static_cast<std::size_t>(slot)];
    if (!card) return QString();
    Card value(card->id, card->displayName, card->type, card->suit, card->rank);
    return QStringLiteral("%1：%2\n%3").arg(equipmentSlotToDisplayName(slot), equipmentText(player, slot), cardDescription(value));
}
}

CardButton::CardButton(const CardView& card, QWidget* parent) : QPushButton(parent)
{
    setObjectName(QStringLiteral("handCard"));
    setCheckable(true);
    setMinimumSize(104, 126);
    setMaximumSize(116, 138);
    Card value(card.id, card.displayName, card.type, card.suit, card.rank);
    setText(QStringLiteral("%1\n\n%2 %3\n%4")
        .arg(cardDisplayName(value), suitToSymbol(card.suit), rankToDisplayName(card.rank),
             cardCategoryDisplayName(card.type)));
    setToolTip(QStringLiteral("%1\n%2").arg(cardDisplayName(value), cardDescription(value)));
    setStyleSheet(QStringLiteral(
        "QPushButton#handCard{background:#fffaf0;border:2px solid #b59a72;border-radius:10px;"
        "padding:8px;font-size:14px;font-weight:600;color:#33271c;text-align:center;}"
        "QPushButton#handCard:hover{background:#fff1cf;border-color:#b23a2b;}"
        "QPushButton#handCard:checked{background:#ffe2a8;border:3px solid #b3261e;}"
        "QPushButton#handCard:disabled{background:#e5e3df;border-color:#aaa;color:#888;}"));
}

void CardButton::setSelected(bool selected) { setChecked(selected); }

CompactTargetCard::CompactTargetCard(const PublicPlayerView& player, bool identityMode, bool selected, QWidget* parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("compactTargetCard"));
    setProperty("playerId", player.id);
    setProperty("selectable", true);
    setCheckable(true);
    setMinimumSize(158, 62);
    setMaximumHeight(72);
    const QString identity = identityMode
        ? (player.identity ? playerIdentityToDisplayName(*player.identity) : QStringLiteral("未知"))
        : QStringLiteral("自由混战");
    setText(QStringLiteral("%1 · ♥ %2/%3\n手牌 %4 · 身份：%5")
        .arg(visibleName(player)).arg(player.hp).arg(player.maxHp).arg(player.handCardCount).arg(identity));
    QStringList details;
    for (const auto slot : {EquipmentSlot::Weapon, EquipmentSlot::Armor, EquipmentSlot::OffensiveHorse, EquipmentSlot::DefensiveHorse}) {
        const auto value = equipmentTooltip(player, slot);
        if (!value.isEmpty()) details << value;
    }
    if (!player.judgmentCards.empty()) details << QStringLiteral("判定区：%1 张").arg(player.judgmentCards.size());
    setToolTip(details.isEmpty() ? QStringLiteral("无装备与判定牌") : details.join(QStringLiteral("\n\n")));
    setStyleSheet(QStringLiteral(
        "QPushButton#compactTargetCard{background:#f8f3e8;border:2px solid #b79b72;border-radius:8px;padding:6px;text-align:left;font-weight:600;}"
        "QPushButton#compactTargetCard:hover{background:#fff1cf;border-color:#c17b25;}"
        "QPushButton#compactTargetCard:checked{background:#ffe1a6;border:3px solid #a83226;}"));
    setSelected(selected);
}

void CompactTargetCard::setSelected(bool selected)
{
    setChecked(selected);
    setProperty("selected", selected);
}

PlayerPanel::PlayerPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("playerPanel"));
    setMinimumSize(172, 96);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5); layout->setSpacing(2);
    title_ = new QLabel(this); identity_ = new QLabel(this); hp_ = new QLabel(this);
    state_ = new QLabel(this); equipment_ = new QLabel(this); judgment_ = new QLabel(this);
    title_->setStyleSheet(QStringLiteral("font-size:15px;font-weight:700;"));
    hp_->setStyleSheet(QStringLiteral("font-size:15px;color:#a22620;font-weight:700;"));
    equipment_->setWordWrap(true); equipment_->setStyleSheet(QStringLiteral("font-size:11px;color:#4d4034;"));
    judgment_->setWordWrap(true); judgment_->setStyleSheet(QStringLiteral("font-size:11px;color:#8a5414;"));
    for (auto* label : {title_, identity_, hp_, state_, equipment_, judgment_}) layout->addWidget(label);
}

void PlayerPanel::setPlayer(const PublicPlayerView& player, bool identityMode, bool self, bool current)
{
    self_ = self; current_ = current;
    setMinimumWidth(self ? 360 : 172);
    setProperty("playerId", player.id); setProperty("currentPlayer", current); setProperty("selfPlayer", self);
    title_->setText(QStringLiteral("%1%2  ·  座位 %3").arg(visibleName(player), self ? QStringLiteral("（你）") : QString()).arg(player.seat + 1));
    const QString identity = identityMode
        ? (player.identity ? playerIdentityToDisplayName(*player.identity) : QStringLiteral("未知"))
        : QStringLiteral("自由混战");
    const QString life = player.dying ? QStringLiteral("濒死") : player.alive ? QStringLiteral("存活") : QStringLiteral("死亡");
    identity_->setText(QStringLiteral("身份：%1  ·  %2  ·  %3").arg(identity, player.controlType == PlayerControlType::AI ? QStringLiteral("AI") : QStringLiteral("真人"), life));
    hp_->setText(QStringLiteral("♥ %1 / %2    手牌 %3").arg(player.hp).arg(player.maxHp).arg(player.handCardCount));
    QStringList states;
    if (player.chained) states << QStringLiteral("横置");
    state_->setText(states.join(QStringLiteral("  ·  "))); state_->setVisible(!states.isEmpty());
    QStringList equipmentSummary;
    for (const auto slot : {EquipmentSlot::Weapon, EquipmentSlot::Armor, EquipmentSlot::OffensiveHorse, EquipmentSlot::DefensiveHorse}) {
        const auto& card = player.equipment.cardsBySlot[static_cast<std::size_t>(slot)];
        if (card) {
            Card value(card->id, card->displayName, card->type, card->suit, card->rank);
            equipmentSummary << QStringLiteral("[%1]").arg(cardDisplayName(value));
        }
    }
    equipment_->setText(QStringLiteral("装备：%1").arg(equipmentSummary.isEmpty() ? QStringLiteral("—") : equipmentSummary.join(QLatin1Char(' '))));
    QStringList equipmentTips;
    for (const auto slot : {EquipmentSlot::Weapon, EquipmentSlot::Armor, EquipmentSlot::OffensiveHorse, EquipmentSlot::DefensiveHorse}) {
        const auto tip = equipmentTooltip(player, slot);
        if (!tip.isEmpty()) equipmentTips << tip;
    }
    equipment_->setToolTip(equipmentTips.join(QStringLiteral("\n\n")));
    QStringList judgments; QStringList judgmentTips;
    for (const auto& card : player.judgmentCards) {
        Card value(card.id, card.displayName, card.type, card.suit, card.rank);
        judgments << QStringLiteral("[%1]").arg(cardTypeToDisplayName(card.type));
        judgmentTips << QStringLiteral("%1 %2%3").arg(cardDisplayName(value), suitToSymbol(card.suit), rankToDisplayName(card.rank));
    }
    judgment_->setText(QStringLiteral("判定：%1").arg(judgments.join(QLatin1Char(' '))));
    judgment_->setToolTip(judgmentTips.join(QLatin1Char('\n')));
    judgment_->setVisible(!judgments.isEmpty());
    updateStyle();
}

void PlayerPanel::setSelectable(bool selectable, bool selected, std::function<void()> activate)
{
    selectable_ = selectable; selected_ = selected; activate_ = std::move(activate);
    setProperty("selectable", selectable); setProperty("selected", selected);
    setCursor(selectable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    updateStyle();
}

void PlayerPanel::mousePressEvent(QMouseEvent* event)
{
    if (selectable_ && event->button() == Qt::LeftButton && activate_) { activate_(); event->accept(); return; }
    QFrame::mousePressEvent(event);
}

void PlayerPanel::updateStyle()
{
    const QString frame = selected_
        ? QStringLiteral("background:#ffe1a6;border:3px solid #a83226;")
        : selectable_ ? QStringLiteral("background:#fff6dc;border:2px solid #c17b25;")
        : current_ ? QStringLiteral("background:#fff2c7;border:3px solid #d28b18;")
        : self_ ? QStringLiteral("background:#eef6ff;border:2px solid #4f78a8;")
                : QStringLiteral("background:#f7f4ee;border:1px solid #b7aa98;");
    setStyleSheet(QStringLiteral("QFrame#playerPanel{%1border-radius:10px;}").arg(frame));
}

} // namespace sanguosha::ui
