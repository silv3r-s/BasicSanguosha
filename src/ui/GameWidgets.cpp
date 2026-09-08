#include "ui/GameWidgets.h"

#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QVBoxLayout>
#include <QHBoxLayout>

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
    setMaximumHeight(150);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5); layout->setSpacing(7);
    portrait_ = new QLabel(QStringLiteral("将"), this); portrait_->setObjectName(QStringLiteral("portraitSlot")); portrait_->setAlignment(Qt::AlignCenter); portrait_->setFixedSize(54, 72); portrait_->setToolTip(QStringLiteral("角色立绘预留区域")); layout->addWidget(portrait_, 0, Qt::AlignTop);
    auto* details = new QVBoxLayout; details->setSpacing(1);
    title_ = new QLabel(this); identity_ = new QLabel(this); hp_ = new QLabel(this);
    state_ = new QLabel(this); chainBadge_ = new QLabel(QStringLiteral("【连环】"), this); equipment_ = new QLabel(this); judgment_ = new QLabel(this);
    chainBadge_->setObjectName(QStringLiteral("chainBadge"));
    chainBadge_->setToolTip(QStringLiteral("连环状态：受到属性伤害时会向其他连环角色传导"));
    title_->setStyleSheet(QStringLiteral("font-size:15px;font-weight:700;"));
    hp_->setStyleSheet(QStringLiteral("font-size:15px;font-weight:700;"));
    equipment_->setWordWrap(true); equipment_->setStyleSheet(QStringLiteral("font-size:11px;"));
    judgment_->setWordWrap(true); judgment_->setStyleSheet(QStringLiteral("font-size:11px;"));
    for (auto* label : {title_, identity_, hp_, chainBadge_, state_, equipment_, judgment_}) details->addWidget(label);
    layout->addLayout(details, 1);
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
    if (player.chained) states << QStringLiteral("连环状态");
    chainBadge_->setVisible(player.chained);
    setProperty("chained", player.chained);
    setProperty("dying", player.dying);
    setProperty("alive", player.alive);
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
    style()->unpolish(this);
    style()->polish(this);
    update();
}

} // namespace sanguosha::ui
