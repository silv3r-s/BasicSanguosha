#include "ui/GameText.h"

#include <QRegularExpression>

namespace sanguosha::ui {
namespace {
QString displayPlayer(QString name)
{
    static const QRegularExpression playerPattern("^Player (\\d+)$");
    const auto match = playerPattern.match(name);
    return match.hasMatch() ? QStringLiteral("玩家 %1").arg(match.captured(1)) : name;
}
}

QString phaseToDisplayName(Phase phase)
{
    switch (phase) {
    case Phase::Start: return QStringLiteral("开始阶段");
    case Phase::Judge: return QStringLiteral("判定阶段");
    case Phase::Draw: return QStringLiteral("摸牌阶段");
    case Phase::Play: return QStringLiteral("出牌阶段");
    case Phase::Discard: return QStringLiteral("弃牌阶段");
    case Phase::Finish: return QStringLiteral("结束阶段");
    }
    return QStringLiteral("未知阶段");
}

QString cardTypeToDisplayName(CardType type)
{
    switch (type) {
    case CardType::Slash: return QStringLiteral("杀");
    case CardType::FireSlash: return QStringLiteral("火杀");
    case CardType::ThunderSlash: return QStringLiteral("雷杀");
    case CardType::Dodge: return QStringLiteral("闪");
    case CardType::Peach: return QStringLiteral("桃");
    case CardType::Wine: return QStringLiteral("酒");
    case CardType::ExNihilo: return QStringLiteral("无中生有");
    case CardType::Dismantlement: return QStringLiteral("过河拆桥");
    case CardType::Snatch: return QStringLiteral("顺手牵羊");
    case CardType::Duel: return QStringLiteral("决斗");
    case CardType::BarbarianInvasion: return QStringLiteral("南蛮入侵");
    case CardType::ArrowBarrage: return QStringLiteral("万箭齐发");
    case CardType::PeachGarden: return QStringLiteral("桃园结义");
    case CardType::Harvest: return QStringLiteral("五谷丰登");
    case CardType::BorrowedSword: return QStringLiteral("借刀杀人");
    case CardType::Nullification: return QStringLiteral("无懈可击");
    case CardType::Indulgence: return QStringLiteral("乐不思蜀");
    case CardType::SupplyShortage: return QStringLiteral("兵粮寸断");
    case CardType::Lightning: return QStringLiteral("闪电");
    case CardType::IronChain: return QStringLiteral("铁索连环");
    case CardType::FireAttack: return QStringLiteral("火攻");
    case CardType::Weapon: return QStringLiteral("武器");
    case CardType::Armor: return QStringLiteral("防具");
    case CardType::OffensiveHorse: return QStringLiteral("进攻马");
    case CardType::DefensiveHorse: return QStringLiteral("防御马");
    }
    return QStringLiteral("未知牌");
}

QString formatEquipmentEffect(const EquipmentEffectEventView& event, const QString& targetName)
{
    const auto equipment = QString::fromStdString(event.equipment);
    const auto amount = QString::number(event.value);
    switch (event.effect) {
    case EquipmentEffectTypeView::IgnoreArmor: return QStringLiteral("【%1】生效：此次【杀】无视%2的防具。").arg(equipment, targetName);
    case EquipmentEffectTypeView::SlashConvertedToFire: return QStringLiteral("【%1】生效：此次【杀】视为火【杀】。").arg(equipment);
    case EquipmentEffectTypeView::BonusDamage: return QStringLiteral("【%1】生效：对%2造成的伤害 +%3。").arg(equipment, targetName, amount);
    case EquipmentEffectTypeView::DamagePrevented:
        return event.relatedCard == CardType::BarbarianInvasion ? QStringLiteral("【%1】生效：免疫此次【南蛮入侵】。").arg(equipment)
             : event.relatedCard == CardType::ArrowBarrage ? QStringLiteral("【%1】生效：免疫此次【万箭齐发】。").arg(equipment)
             : QStringLiteral("【%1】生效：此次【杀】无效。").arg(equipment);
    case EquipmentEffectTypeView::FireDamageIncreased: return QStringLiteral("【%1】生效：此次火焰伤害 +%2。").arg(equipment, amount);
    case EquipmentEffectTypeView::DamageCapped: return QStringLiteral("【%1】生效：此次伤害调整为 %2。").arg(equipment, amount);
    case EquipmentEffectTypeView::HealOnLeave: return QStringLiteral("【%1】离开装备区：回复 %2 点体力。").arg(equipment, amount);
    case EquipmentEffectTypeView::MultiTargetEnabled: return QStringLiteral("【%1】生效：此【杀】可以指定至多 %2 个目标。").arg(equipment, amount);
    }
    return {};
}

std::vector<EquipmentEffectEventView> newEquipmentEffects(const std::vector<EquipmentEffectEventView>& effects, std::uint64_t& lastDisplayedEventId)
{
    std::vector<EquipmentEffectEventView> result;
    for (const auto& event : effects) if (event.eventId > lastDisplayedEventId) {
        result.push_back(event);
        lastDisplayedEventId = event.eventId;
    }
    return result;
}

QString cardDisplayName(const Card &card)
{
    if (!equipmentSlotForCard(card.type())) return cardTypeToDisplayName(card.type());

    const auto name = QString::fromStdString(card.name());
    if (name == QStringLiteral("JueYing")) return QStringLiteral("绝影");
    if (name == QStringLiteral("ZhuaHuangFeiDian")) return QStringLiteral("爪黄飞电");
    if (name == QStringLiteral("HuaLiu")) return QStringLiteral("骅骝");
    if (name == QStringLiteral("DiLu")) return QStringLiteral("的卢");
    if (name == QStringLiteral("DaYuan")) return QStringLiteral("大宛");
    if (name == QStringLiteral("ChiTu")) return QStringLiteral("赤兔");
    if (name == QStringLiteral("ZiXing")) return QStringLiteral("紫骍");
    return name;
}

QString equipmentSlotToDisplayName(EquipmentSlot slot)
{
    switch (slot) {
    case EquipmentSlot::Weapon: return QStringLiteral("武器");
    case EquipmentSlot::Armor: return QStringLiteral("防具");
    case EquipmentSlot::OffensiveHorse: return QStringLiteral("进攻马");
    case EquipmentSlot::DefensiveHorse: return QStringLiteral("防御马");
    }
    return QStringLiteral("未知装备槽");
}

QString cardCategoryDisplayName(CardType type)
{
    switch (type) {
    case CardType::Slash:
    case CardType::FireSlash:
    case CardType::ThunderSlash:
    case CardType::Dodge:
    case CardType::Peach:
    case CardType::Wine:
        return QStringLiteral("基本牌");
    case CardType::ExNihilo:
    case CardType::Dismantlement:
    case CardType::Snatch:
    case CardType::Duel:
    case CardType::BarbarianInvasion:
    case CardType::ArrowBarrage:
    case CardType::PeachGarden:
    case CardType::Harvest:
    case CardType::BorrowedSword:
    case CardType::Nullification:
    case CardType::Indulgence:
    case CardType::SupplyShortage:
    case CardType::Lightning:
    case CardType::IronChain:
    case CardType::FireAttack:
        return QStringLiteral("普通锦囊");
    case CardType::Weapon:
    case CardType::Armor:
    case CardType::OffensiveHorse:
    case CardType::DefensiveHorse:
        return QStringLiteral("装备牌");
    }
    return QStringLiteral("未知类别");
}

QString cardDescription(const Card &card)
{
    switch (card.type()) {
    case CardType::Slash: return QStringLiteral("出牌阶段对攻击范围内的一名其他玩家使用；目标需使用【闪】，否则受到 1 点伤害。");
    case CardType::FireSlash: return QStringLiteral("规则同【杀】；命中时造成 1 点火焰伤害。");
    case CardType::ThunderSlash: return QStringLiteral("规则同【杀】；命中时造成 1 点雷电伤害。");
    case CardType::Dodge: return QStringLiteral("不能主动使用；用于响应【杀】。");
    case CardType::Peach: return QStringLiteral("出牌阶段回复自己 1 点体力；濒死时可用于救援。");
    case CardType::Wine: return QStringLiteral("出牌阶段使用后，本回合下一张【杀】伤害 +1；濒死时仅可由自己作为自救使用。");
    case CardType::ExNihilo: return QStringLiteral("摸 2 张牌。");
    case CardType::Dismantlement: return QStringLiteral("弃置一名其他玩家的一张手牌或装备。");
    case CardType::Snatch: return QStringLiteral("获得距离 1 内一名其他玩家的一张手牌或装备。");
    case CardType::Duel: return QStringLiteral("双方轮流打出【杀】；首先不出【杀】的一方受到 1 点伤害。");
    case CardType::BarbarianInvasion: return QStringLiteral("其他所有存活玩家依次打出【杀】，否则受到 1 点伤害。");
    case CardType::ArrowBarrage: return QStringLiteral("其他所有存活玩家依次打出【闪】，否则受到 1 点伤害。");
    case CardType::PeachGarden: return QStringLiteral("所有存活且受伤的玩家各回复 1 点体力。");
    case CardType::Harvest: return QStringLiteral("亮出等同存活玩家数的牌，所有存活玩家从使用者开始依次选择一张。");
    case CardType::BorrowedSword: return QStringLiteral("指定一名持武器玩家和其攻击范围内目标；其须对目标使用【杀】，否则交出武器。");
    case CardType::Nullification: return QStringLiteral("响应即将生效的锦囊；无懈链按张数奇偶决定原效果是否抵消。");
    case CardType::Indulgence: return QStringLiteral("延时锦囊：判定为红桃时正常出牌，否则跳过出牌阶段。");
    case CardType::SupplyShortage: return QStringLiteral("延时锦囊：判定为梅花时正常摸牌，否则跳过摸牌阶段。");
    case CardType::Lightning: return QStringLiteral("延时锦囊：判定为黑桃 2 至 9 时受到 3 点伤害，否则转移。");
    case CardType::IronChain: return QStringLiteral("选择一至两名角色，分别横置或重置其武将牌；无目标时可重铸，弃置此牌并摸一张牌。");
    case CardType::FireAttack: return QStringLiteral("目标展示一张手牌；你可弃置一张同花色手牌，令其受到 1 点火焰伤害。");
    case CardType::Weapon: {
        const auto name = QString::fromStdString(card.name());
        const auto range = card.equipmentData() ? card.equipmentData()->attackRange : 1;
        if (name == QStringLiteral("雌雄双股剑")) return QStringLiteral("攻击范围：%1。当你使用【杀】指定异性角色为目标后，该角色可弃置一张手牌，否则你摸一张牌。").arg(range);
        if (name == QStringLiteral("诸葛连弩")) return QStringLiteral("攻击范围：%1。出牌阶段使用【杀】次数不限。").arg(range);
        if (name == QStringLiteral("古锭刀")) return QStringLiteral("攻击范围：%1。你使用【杀】对没有手牌的目标造成的伤害+1。").arg(range);
        if (name == QStringLiteral("朱雀羽扇")) return QStringLiteral("攻击范围：%1。你使用普通【杀】时，视为火【杀】。").arg(range);
        if (name == QStringLiteral("青釭剑")) return QStringLiteral("攻击范围：%1。你使用【杀】指定目标后，此【杀】无视其防具。").arg(range);
        if (name == QStringLiteral("青龙偃月刀")) return QStringLiteral("攻击范围：%1。你使用的【杀】被【闪】抵消后，可对同一目标再使用一张【杀】。").arg(range);
        if (name == QStringLiteral("贯石斧")) return QStringLiteral("攻击范围：%1。当你使用的【杀】被【闪】抵消后，你可以弃置两张牌，使此【杀】依然造成伤害。").arg(range);
        if (name == QStringLiteral("丈八蛇矛")) return QStringLiteral("攻击范围：%1。你可以将两张手牌当【杀】使用或打出。").arg(range);
        if (name == QStringLiteral("方天画戟")) return QStringLiteral("攻击范围：%1。当你使用最后一张实体手牌【杀】时，至多可指定三名目标。").arg(range);
        if (name == QStringLiteral("麒麟弓")) return QStringLiteral("攻击范围：%1。当你使用【杀】对目标造成伤害后，你可以弃置其装备区里的一张坐骑牌。").arg(range);
        if (name == QStringLiteral("寒冰剑")) return QStringLiteral("攻击范围：%1。当你使用【杀】即将对目标造成伤害时，你可以防止此伤害，然后依次弃置其至多两张牌。").arg(range);
        return QStringLiteral("攻击范围：%1。特殊效果：当前版本暂未实现。").arg(range);
    }
    case CardType::Armor: {
        const auto name = QString::fromStdString(card.name());
        if (name == QStringLiteral("八卦阵")) return QStringLiteral("当你需要使用【闪】时，自动进行判定；红色视为使用【闪】，黑色后仍可正常响应。");
        if (name == QStringLiteral("仁王盾")) return QStringLiteral("黑色普通【杀】对你无效；火【杀】和雷【杀】不受此影响。");
        if (name == QStringLiteral("藤甲")) return QStringLiteral("普通【杀】、【南蛮入侵】和【万箭齐发】对你无效；你受到的火焰伤害+1。");
        if (name == QStringLiteral("白银狮子")) return QStringLiteral("你每次受到的伤害至多为 1 点；当它离开装备区时，若你已受伤，回复 1 点体力。");
        return QStringLiteral("占用防具槽。特殊效果：当前版本暂未实现。");
    }
    case CardType::OffensiveHorse: return QStringLiteral("你计算与其他玩家的距离 -1。");
    case CardType::DefensiveHorse: return QStringLiteral("其他玩家计算与你的距离 +1。");
    }
    return QStringLiteral("暂无说明。");
}

QString suitToDisplayName(Suit suit)
{
    switch (suit) {
    case Suit::Spade: return QStringLiteral("黑桃");
    case Suit::Heart: return QStringLiteral("红桃");
    case Suit::Club: return QStringLiteral("梅花");
    case Suit::Diamond: return QStringLiteral("方片");
    }
    return QStringLiteral("未知花色");
}

QString suitToSymbol(Suit suit)
{
    switch (suit) {
    case Suit::Spade: return QStringLiteral("♠");
    case Suit::Heart: return QStringLiteral("♥");
    case Suit::Club: return QStringLiteral("♣");
    case Suit::Diamond: return QStringLiteral("♦");
    }
    return QStringLiteral("?");
}

QString rankToDisplayName(int rank)
{
    if (rank == 1) return QStringLiteral("A");
    if (rank == 11) return QStringLiteral("J");
    if (rank == 12) return QStringLiteral("Q");
    if (rank == 13) return QStringLiteral("K");
    return QString::number(rank);
}

QString playerDisplayName(const Player &player) { return QStringLiteral("玩家 %1").arg(player.id()); }
QString playerDisplayName(const std::string &internalName) { return displayPlayer(QString::fromStdString(internalName)); }

QString playerStatusToDisplayName(PlayerStatus status)
{
    switch (status) {
    case PlayerStatus::Alive: return QStringLiteral("存活");
    case PlayerStatus::Dying: return QStringLiteral("濒死");
    case PlayerStatus::Dead: return QStringLiteral("死亡");
    }
    return QStringLiteral("未知状态");
}

QString playerIdentityToDisplayName(PlayerIdentity identity)
{
    switch (identity) {
    case PlayerIdentity::None: return QStringLiteral("无身份");
    case PlayerIdentity::Lord: return QStringLiteral("主公");
    case PlayerIdentity::Loyalist: return QStringLiteral("忠臣");
    case PlayerIdentity::Rebel: return QStringLiteral("反贼");
    case PlayerIdentity::Renegade: return QStringLiteral("内奸");
    }
    return QStringLiteral("未知");
}

QString winningSideToDisplayName(WinningSide side)
{
    switch (side) {
    case WinningSide::FreeForAll: return QStringLiteral("自由混战胜利");
    case WinningSide::LordSide: return QStringLiteral("主公与忠臣阵营胜利");
    case WinningSide::RebelSide: return QStringLiteral("反贼阵营胜利");
    case WinningSide::Renegade: return QStringLiteral("内奸胜利");
    case WinningSide::None: return QStringLiteral("未决出胜负");
    }
    return QStringLiteral("未决出胜负");
}

QString responseTypeToDisplayName(ResponseType type)
{
    switch (type) {
    case ResponseType::Dodge: return QStringLiteral("闪");
    case ResponseType::Slash: return QStringLiteral("杀");
    case ResponseType::PeachRescue: return QStringLiteral("桃救援");
    case ResponseType::BorrowedSwordSlash: return QStringLiteral("借刀杀人-杀");
    case ResponseType::Nullification: return QStringLiteral("无懈可击");
    case ResponseType::QinglongSlash: return QStringLiteral("青龙偃月刀追击-杀");
    }
    return QStringLiteral("未知响应");
}

QString actionErrorToDisplayName(ActionResult::Error error)
{
    switch (error) {
    case ActionResult::Error::None: return QString();
    case ActionResult::Error::UnauthorizedPlayer: return QStringLiteral("无权代表该玩家提交操作。");
    case ActionResult::Error::NotCurrentPlayer: return QStringLiteral("现在不是你的回合。");
    case ActionResult::Error::WrongPhase: return QStringLiteral("当前阶段不能执行此操作。");
    case ActionResult::Error::InvalidCard: return QStringLiteral("这张牌现在不能使用。");
    case ActionResult::Error::InvalidTarget: return QStringLiteral("请选择合法目标。");
    case ActionResult::Error::InvalidResponse: return QStringLiteral("当前需要响应指定的牌。");
    case ActionResult::Error::RequestMismatch: return QStringLiteral("响应请求已失效。");
    case ActionResult::Error::GameOver: return QStringLiteral("游戏已经结束。");
    }
    return QStringLiteral("操作失败。");
}

QString logEntryToDisplay(const std::string &entry)
{
    const QString text = QString::fromStdString(entry);
    if (text == "Game started.") return QStringLiteral("游戏开始。");
    const auto cardName = [](QString name) {
        static const std::pair<const char*, const char*> names[] = {
            {"Slash", "杀"}, {"Fire Slash", "火杀"}, {"Thunder Slash", "雷杀"}, {"Dodge", "闪"}, {"Peach", "桃"}, {"Wine", "酒"},
            {"Ex Nihilo", "无中生有"}, {"Dismantlement", "过河拆桥"}, {"Snatch", "顺手牵羊"}, {"Duel", "决斗"}, {"Barbarian Invasion", "南蛮入侵"},
            {"Arrow Barrage", "万箭齐发"}, {"Peach Garden", "桃园结义"}, {"Harvest", "五谷丰登"}, {"Borrowed Sword", "借刀杀人"},
            {"Nullification", "无懈可击"}, {"Indulgence", "乐不思蜀"}, {"Supply Shortage", "兵粮寸断"}, {"Lightning", "闪电"},
            {"Iron Chain", "铁索连环"}, {"Fire Attack", "火攻"}, {"Bagua", "八卦阵"}, {"Vine Armor", "藤甲"}, {"Renwang Shield", "仁王盾"},
            {"Silver Lion", "白银狮子"}, {"Qinglong Blade", "青龙偃月刀"}, {"Guding Blade", "古锭刀"}, {"Vermilion Fan", "朱雀羽扇"},
            {"Halberd", "方天画戟"}, {"Axe", "贯石斧"}, {"Kirin Bow", "麒麟弓"}, {"Ice Sword", "寒冰剑"}, {"Double Sword", "雌雄双股剑"}, {"Serpent Spear", "丈八蛇矛"}};
        for (const auto& item : names) if (name == QString::fromLatin1(item.first)) return QString::fromUtf8(item.second);
        return name;
    };
    QRegularExpressionMatch match;
    static const QRegularExpression drew("^(.+) drew (\\d+) card\\(s\\)\\.$");
    static const QRegularExpression turnStarted("^Turn (\\d+) started: (.+)\\.$");
    static const QRegularExpression entered("^(.+) entered (Start|Judge|Draw|Play|Discard|Finish) phase\\.$");
    static const QRegularExpression endedPlay("^(.+) ended Play phase\\.$");
    static const QRegularExpression discarded("^(.+) discarded (\\d+) card\\(s\\)\\.$");
    static const QRegularExpression usedSlash("^(.+) used \\[Slash\\] targeting (.+)\\.$");
    static const QRegularExpression usedWineSlash("^(.+) used \\[Wine\\] enhanced \\[Slash\\] targeting (.+)\\.$");
    static const QRegularExpression usedPeach("^(.+) used \\[Peach\\]\\.$");
    static const QRegularExpression usedWine("^(.+) used \\[Wine\\]\\.$");
    static const QRegularExpression usedExNihilo("^(.+) used \\[Ex Nihilo\\]\\.$");
    static const QRegularExpression usedDismantlement("^(.+) used \\[Dismantlement\\] targeting (.+)\\.$");
    static const QRegularExpression usedSnatch("^(.+) used \\[Snatch\\] targeting (.+)\\.$");
    static const QRegularExpression usedDuel("^(.+) used \\[Duel\\] targeting (.+)\\.$");
    static const QRegularExpression discardedHand("^(.+) discarded one hand card from (.+)\\.$");
    static const QRegularExpression obtainedHand("^(.+) obtained one hand card from (.+)\\.$");
    static const QRegularExpression equipped("^(.+) equipped \\[(.+)\\]\\.$");
    static const QRegularExpression replacedEquipment("^(.+) replaced equipped \\[(.+)\\]\\.$");
    static const QRegularExpression attackRange("^(.+) attack range is now (\\d+)\\.$");
    static const QRegularExpression discardedEquipment("^(.+) discarded equipped \\[(.+)\\] from (.+)\\.$");
    static const QRegularExpression obtainedEquipment("^(.+) obtained equipped \\[(.+)\\] from (.+)\\.$");
    static const QRegularExpression wineNext("^(.+)'s next \\[Slash\\] damage is \\+1\\.$");
    static const QRegularExpression wineExpired("^(.+)'s \\[Wine\\] effect expired at turn end\\.$");
    static const QRegularExpression waiting("^Waiting for (.+) to respond with \\[Dodge\\]\\.$");
    static const QRegularExpression waitingDuelSlash("^Waiting for (.+) to respond with \\[Slash\\] in \\[Duel\\]\\.$");
    static const QRegularExpression declined("^(.+) declined to use \\[Dodge\\]\\.$");
    static const QRegularExpression declinedDuelSlash("^(.+) declined to use \\[Slash\\]\\.$");
    static const QRegularExpression usedDodge("^(.+) used \\[Dodge\\]\\.$");
    static const QRegularExpression usedDuelSlash("^(.+) used \\[Slash\\] in \\[Duel\\]\\.$");
    static const QRegularExpression damaged("^(.+) took (\\d+) damage\\. HP: (-?\\d+) -> (-?\\d+)\\.$");
    static const QRegularExpression dying("^(.+) entered Dying state\\.$");
    static const QRegularExpression died("^(.+) died\\.$");
    static const QRegularExpression diedIdentity("^(.+) died\\. Identity: (Lord|Loyalist|Rebel|Renegade)\\.$");
    static const QRegularExpression won("^(.+) wins\\.$");
    static const QRegularExpression recovered("^(.+) recovered (\\d+) HP\\.$");
    static const QRegularExpression hpChanged("^(.+) HP: (-?\\d+) -> (-?\\d+)\\.$");
    static const QRegularExpression rescueNeed("^(.+) requires (\\d+) HP to be rescued\\.$");
    static const QRegularExpression waitingPeach("^Waiting for (.+) to use \\[Peach\\] to rescue (.+)\\.$");
    static const QRegularExpression declinedPeach("^(.+) declined to use \\[Peach\\]\\.$");
    static const QRegularExpression usedPeachRescue("^(.+) used \\[Peach\\] to rescue (.+)\\.$");
    static const QRegularExpression usedWineRescue("^(.+) used \\[Wine\\] to rescue (.+)\\.$");
    static const QRegularExpression leftDying("^(.+) left Dying state\\.$");
    static const QRegularExpression failedDying("^(.+) could not leave Dying state\\.$");
    static const QRegularExpression usedNamed("^(.+) used \\[(.+)\\](?: targeting (.+))?\\.$");
    static const QRegularExpression waitingNullification("^Waiting for (.+) to respond with \\[Nullification\\]\\.$");
    static const QRegularExpression delayedEntered("^\\[(.+)\\] entered (.+)'s judgment zone\\.$");
    static const QRegularExpression baguaTriggered("^(.+) triggered \\[Bagua\\]\\.$");
    static const QRegularExpression judged("^(.+) judged \\[(.+)\\] (Spade|Heart|Club|Diamond) (\\d+)\\.$");
    static const QRegularExpression baguaResult("^(.+) judged (red|black): \\[Bagua\\] (succeeded and counts as \\[Dodge\\]|failed)\\.$");
    static const QRegularExpression delayedResult("^(.+) (passed|failed) \\[(Indulgence|Supply Shortage)\\] judgment(?: and skips (Play|Draw) phase)?\\.$");
    static const QRegularExpression lightningHit("^(.+) was struck by \\[Lightning\\]\\.$");
    static const QRegularExpression lightningPassed("^\\[Lightning\\] passed to (.+)\\.$");
    static const QRegularExpression ironChainState("^(.+) was (chained|unchained) by \\[Iron Chain\\]\\.$");
    static const QRegularExpression fireReveal("^(.+) revealed a hand card for \\[Fire Attack\\]\\.$");
    static const QRegularExpression aoeWaiting("^Waiting for (.+) to respond to AOE\\.$");
    static const QRegularExpression aoeFailed("^(.+) failed to respond to AOE\\.$");
    static const QRegularExpression timedOut("^(.+) timed out (while responding|while selecting a card|in Play phase|in Discard phase)\\.$");
    static const QRegularExpression connection("^(.+) (disconnected; waiting to reconnect|reconnected)\\.$");
    static const QRegularExpression protectedArmor("^(.+) was protected by \\[Vine Armor\\]\\.$");
    static const QRegularExpression renwang("^(.+) blocked a black \\[Slash\\] with \\[Renwang Shield\\]\\.$");
    static const QRegularExpression chainPropagation("^(.+) propagated (\\d+) (Fire|Thunder) damage to (.+) through \\[Iron Chain\\]\\.$");
    if ((match = drew.match(text)).hasMatch()) return QStringLiteral("%1 摸了 %2 张牌。").arg(displayPlayer(match.captured(1)), match.captured(2));
    if ((match = turnStarted.match(text)).hasMatch()) return QStringLiteral("第 %1 回合开始，当前回合玩家：%2。").arg(match.captured(1), displayPlayer(match.captured(2)));
    if ((match = entered.match(text)).hasMatch()) {
        const QString phase = match.captured(2);
        const Phase phases[] = {Phase::Start, Phase::Judge, Phase::Draw, Phase::Play, Phase::Discard, Phase::Finish};
        for (const auto value : phases) if (phase == QString::fromLatin1(value == Phase::Start ? "Start" : value == Phase::Judge ? "Judge" : value == Phase::Draw ? "Draw" : value == Phase::Play ? "Play" : value == Phase::Discard ? "Discard" : "Finish")) return QStringLiteral("%1 进入%2。").arg(displayPlayer(match.captured(1)), phaseToDisplayName(value));
    }
    if ((match = endedPlay.match(text)).hasMatch()) return QStringLiteral("%1 结束了出牌阶段。").arg(displayPlayer(match.captured(1)));
    if ((match = discarded.match(text)).hasMatch()) return QStringLiteral("%1 弃置了 %2 张牌。").arg(displayPlayer(match.captured(1)), match.captured(2));
    if ((match = usedWineSlash.match(text)).hasMatch()) return QStringLiteral("%1 使用了酒强化的【杀】，目标为%2。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = usedSlash.match(text)).hasMatch()) return QStringLiteral("%1 使用了【杀】，目标为%2。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = usedPeach.match(text)).hasMatch()) return QStringLiteral("%1 使用了【桃】。").arg(displayPlayer(match.captured(1)));
    if ((match = usedWine.match(text)).hasMatch()) return QStringLiteral("%1 使用了【酒】。").arg(displayPlayer(match.captured(1)));
    if ((match = usedExNihilo.match(text)).hasMatch()) return QStringLiteral("%1 使用了【无中生有】。").arg(displayPlayer(match.captured(1)));
    if ((match = usedDismantlement.match(text)).hasMatch()) return QStringLiteral("%1 对%2使用了【过河拆桥】。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = usedSnatch.match(text)).hasMatch()) return QStringLiteral("%1 对%2使用了【顺手牵羊】。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = usedDuel.match(text)).hasMatch()) return QStringLiteral("%1 对%2使用了【决斗】。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = discardedHand.match(text)).hasMatch()) return QStringLiteral("%1 弃置了%2的一张手牌。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = obtainedHand.match(text)).hasMatch()) return QStringLiteral("%1 获得了%2的一张手牌。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = replacedEquipment.match(text)).hasMatch()) return QStringLiteral("%1 卸下了【%2】。").arg(displayPlayer(match.captured(1)), match.captured(2));
    if ((match = equipped.match(text)).hasMatch()) return QStringLiteral("%1 装备了【%2】。").arg(displayPlayer(match.captured(1)), match.captured(2));
    if ((match = attackRange.match(text)).hasMatch()) return QStringLiteral("%1 的攻击范围变为 %2。").arg(displayPlayer(match.captured(1)), match.captured(2));
    if ((match = discardedEquipment.match(text)).hasMatch()) return QStringLiteral("%1 弃置了%3的【%2】。").arg(displayPlayer(match.captured(1)), match.captured(2), displayPlayer(match.captured(3)));
    if ((match = obtainedEquipment.match(text)).hasMatch()) return QStringLiteral("%1 获得了%3的【%2】。").arg(displayPlayer(match.captured(1)), match.captured(2), displayPlayer(match.captured(3)));
    if ((match = wineNext.match(text)).hasMatch()) return QStringLiteral("%1 的下一张【杀】伤害 +1。").arg(displayPlayer(match.captured(1)));
    if ((match = wineExpired.match(text)).hasMatch()) return QStringLiteral("%1 的【酒】效果在回合结束时消失。").arg(displayPlayer(match.captured(1)));
    if ((match = waiting.match(text)).hasMatch()) return QStringLiteral("等待%1响应【闪】。").arg(displayPlayer(match.captured(1)));
    if ((match = waitingDuelSlash.match(text)).hasMatch()) return QStringLiteral("【决斗】中，等待%1打出【杀】。").arg(displayPlayer(match.captured(1)));
    if ((match = declined.match(text)).hasMatch()) return QStringLiteral("%1 选择不出【闪】。").arg(displayPlayer(match.captured(1)));
    if ((match = declinedDuelSlash.match(text)).hasMatch()) return QStringLiteral("%1 选择不出【杀】。").arg(displayPlayer(match.captured(1)));
    if ((match = usedDodge.match(text)).hasMatch()) return QStringLiteral("%1 使用了【闪】。").arg(displayPlayer(match.captured(1)));
    if ((match = usedDuelSlash.match(text)).hasMatch()) return QStringLiteral("%1 在【决斗】中打出了【杀】。").arg(displayPlayer(match.captured(1)));
    if ((match = waitingNullification.match(text)).hasMatch()) return QStringLiteral("等待%1响应【无懈可击】。").arg(displayPlayer(match.captured(1)));
    if (text == "[Trick] was nullified.") return QStringLiteral("当前锦囊被【无懈可击】抵消。");
    if ((match = usedNamed.match(text)).hasMatch()) return match.captured(3).isEmpty()
        ? QStringLiteral("%1 使用了【%2】。").arg(displayPlayer(match.captured(1)), cardName(match.captured(2)))
        : QStringLiteral("%1 使用了【%2】，目标为%3。").arg(displayPlayer(match.captured(1)), cardName(match.captured(2)), displayPlayer(match.captured(3)));
    if ((match = delayedEntered.match(text)).hasMatch()) return QStringLiteral("【%1】进入%2的判定区。").arg(cardName(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = baguaTriggered.match(text)).hasMatch()) return QStringLiteral("%1的【八卦阵】开始判定。").arg(displayPlayer(match.captured(1)));
    if ((match = judged.match(text)).hasMatch()) {
        const auto suit = match.captured(3) == "Spade" ? QStringLiteral("♠") : match.captured(3) == "Heart" ? QStringLiteral("♥") : match.captured(3) == "Club" ? QStringLiteral("♣") : QStringLiteral("♦");
        return QStringLiteral("%1的判定牌为【%2】%3%4。").arg(displayPlayer(match.captured(1)), cardName(match.captured(2)), suit, match.captured(4));
    }
    if ((match = baguaResult.match(text)).hasMatch()) return match.captured(2) == "red"
        ? QStringLiteral("%1的【八卦阵】判定成功，视为使用【闪】。").arg(displayPlayer(match.captured(1)))
        : QStringLiteral("%1的【八卦阵】判定失败。").arg(displayPlayer(match.captured(1)));
    if ((match = delayedResult.match(text)).hasMatch()) return match.captured(2) == "passed"
        ? QStringLiteral("%1的【%2】判定通过。").arg(displayPlayer(match.captured(1)), cardName(match.captured(3)))
        : QStringLiteral("%1的【%2】判定生效，跳过%3。").arg(displayPlayer(match.captured(1)), cardName(match.captured(3)), match.captured(4) == "Play" ? QStringLiteral("出牌阶段") : QStringLiteral("摸牌阶段"));
    if ((match = lightningHit.match(text)).hasMatch()) return QStringLiteral("%1的【闪电】判定生效，受到雷电伤害。").arg(displayPlayer(match.captured(1)));
    if ((match = lightningPassed.match(text)).hasMatch()) return QStringLiteral("【闪电】传递给%1。").arg(displayPlayer(match.captured(1)));
    if ((match = ironChainState.match(text)).hasMatch()) return QStringLiteral("%1%2。").arg(displayPlayer(match.captured(1)), match.captured(2) == "chained" ? QStringLiteral("被横置") : QStringLiteral("解除横置状态"));
    if ((match = fireReveal.match(text)).hasMatch()) return QStringLiteral("%1为【火攻】展示了一张手牌。").arg(displayPlayer(match.captured(1)));
    if ((match = aoeWaiting.match(text)).hasMatch()) return QStringLiteral("等待%1响应当前群体锦囊。").arg(displayPlayer(match.captured(1)));
    if ((match = aoeFailed.match(text)).hasMatch()) return QStringLiteral("%1未能响应群体锦囊，将受到伤害。").arg(displayPlayer(match.captured(1)));
    if ((match = timedOut.match(text)).hasMatch()) return QStringLiteral("%1%2超时，服务器已按规则处理。").arg(displayPlayer(match.captured(1)), match.captured(2) == "while responding" ? QStringLiteral("响应") : match.captured(2) == "while selecting a card" ? QStringLiteral("选牌") : match.captured(2) == "in Play phase" ? QStringLiteral("出牌阶段") : QStringLiteral("弃牌阶段"));
    if ((match = connection.match(text)).hasMatch()) return match.captured(2).startsWith("disconnected")
        ? QStringLiteral("%1已断开连接，等待重连。").arg(displayPlayer(match.captured(1)))
        : QStringLiteral("%1已重新连接。").arg(displayPlayer(match.captured(1)));
    if ((match = protectedArmor.match(text)).hasMatch()) return QStringLiteral("%1受到【藤甲】保护，本次效果无效。").arg(displayPlayer(match.captured(1)));
    if ((match = renwang.match(text)).hasMatch()) return QStringLiteral("%1的【仁王盾】抵消了黑色普通【杀】。").arg(displayPlayer(match.captured(1)));
    if ((match = chainPropagation.match(text)).hasMatch()) return QStringLiteral("%1受到的 %2 点%3伤害通过【铁索连环】传导至%4。").arg(displayPlayer(match.captured(1)), match.captured(2), match.captured(3) == "Fire" ? QStringLiteral("火焰") : QStringLiteral("雷电"), displayPlayer(match.captured(4)));
    if (text == "[Slash] was dodged.") return QStringLiteral("【杀】被抵消。");
    if ((match = damaged.match(text)).hasMatch()) return QStringLiteral("%1 受到 %2 点伤害，体力：%3 → %4。").arg(displayPlayer(match.captured(1)), match.captured(2), match.captured(3), match.captured(4));
    if ((match = dying.match(text)).hasMatch()) return QStringLiteral("%1 进入濒死状态。").arg(displayPlayer(match.captured(1)));
    if ((match = rescueNeed.match(text)).hasMatch()) return QStringLiteral("%1 当前还需要恢复 %2 点体力。").arg(displayPlayer(match.captured(1)), match.captured(2));
    if ((match = waitingPeach.match(text)).hasMatch()) return QStringLiteral("等待%1使用【桃】救援%2。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = declinedPeach.match(text)).hasMatch()) return QStringLiteral("%1 选择不使用【桃】。").arg(displayPlayer(match.captured(1)));
    if ((match = usedPeachRescue.match(text)).hasMatch()) return QStringLiteral("%1 对%2使用了【桃】进行救援。").arg(displayPlayer(match.captured(1)), displayPlayer(match.captured(2)));
    if ((match = usedWineRescue.match(text)).hasMatch()) return QStringLiteral("%1 使用【酒】进行自救。").arg(displayPlayer(match.captured(1)));
    if ((match = recovered.match(text)).hasMatch()) return QStringLiteral("%1 恢复了 %2 点体力。").arg(displayPlayer(match.captured(1)), match.captured(2));
    if ((match = hpChanged.match(text)).hasMatch()) return QStringLiteral("%1 的体力：%2 → %3。").arg(displayPlayer(match.captured(1)), match.captured(2), match.captured(3));
    if ((match = leftDying.match(text)).hasMatch()) return QStringLiteral("%1 脱离濒死状态。").arg(displayPlayer(match.captured(1)));
    if ((match = failedDying.match(text)).hasMatch()) return QStringLiteral("%1 未能脱离濒死状态。").arg(displayPlayer(match.captured(1)));
    if ((match = diedIdentity.match(text)).hasMatch()) {
        const auto identity = match.captured(2) == "Lord" ? PlayerIdentity::Lord : match.captured(2) == "Loyalist" ? PlayerIdentity::Loyalist : match.captured(2) == "Rebel" ? PlayerIdentity::Rebel : PlayerIdentity::Renegade;
        return QStringLiteral("%1 死亡，身份为%2。").arg(displayPlayer(match.captured(1)), playerIdentityToDisplayName(identity));
    }
    if ((match = died.match(text)).hasMatch()) return QStringLiteral("%1 死亡。").arg(displayPlayer(match.captured(1)));
    if ((match = won.match(text)).hasMatch()) return QStringLiteral("%1 获胜。").arg(displayPlayer(match.captured(1)));
    if (text == "[Duel] resolved.") return QStringLiteral("【决斗】结算结束。");
    if (text == "[Ex Nihilo] resolved.") return QStringLiteral("【无中生有】结算结束。");
    if (text == "[Iron Chain] resolved.") return QStringLiteral("【铁索连环】结算结束。");
    if ((match = QRegularExpression("^Turn (\\d+) ended\\.$").match(text)).hasMatch()) return QStringLiteral("第 %1 回合结束。").arg(match.captured(1));
    const auto actor = text.section(' ', 0, 0);
    const auto localizedEquipment = [&](const char* token, const char* name, const char* action) -> QString {
        return text.contains(QString::fromLatin1(token)) ? QStringLiteral("%1的【%2】%3。").arg(displayPlayer(actor), QString::fromUtf8(name), QString::fromUtf8(action)) : QString();
    };
    if (const auto value = localizedEquipment("Qinglong Blade", "青龙偃月刀", "触发追击" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Guding Blade", "古锭刀", "使此次伤害增加" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Vermilion Fan", "朱雀羽扇", "将普通杀转为火杀" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Halberd", "方天画戟", "触发多目标效果" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Renwang Shield", "仁王盾", "抵消了黑色普通杀" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Silver Lion", "白银狮子", "触发效果" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Serpent Spear", "丈八蛇矛", "将两张手牌当作杀使用" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Ice Sword", "寒冰剑", "触发效果" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Double Sword", "雌雄双股剑", "触发效果" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("Kirin Bow", "麒麟弓", "触发效果" ); !value.isEmpty()) return value;
    if (const auto value = localizedEquipment("[Axe]", "贯石斧", "触发效果" ); !value.isEmpty()) return value;
    return QStringLiteral("收到一条系统消息。");
}

bool showLogEntryInRecentEvent(const std::string& entry)
{
    static const QRegularExpression waitingNullification(
        QStringLiteral("^Waiting for .+ to respond with \\[Nullification\\]\\.$"));
    return !waitingNullification.match(QString::fromStdString(entry)).hasMatch();
}

} // namespace sanguosha::ui
