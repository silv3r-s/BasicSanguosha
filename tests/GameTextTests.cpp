#include <cstdlib>
#include <iostream>
#include <string>

#include "cards/Card.h"
#include "core/GameState.h"
#include "core/Player.h"
#include "core/ResponseRequest.h"
#include "ui/GameText.h"

using namespace sanguosha;

namespace {
[[noreturn]] void fail(const char* message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
void expect(bool value, const char* message) { if (!value) fail(message); }

bool isHealthy(const QString& text)
{
    return !text.isEmpty()
        && !text.contains(QChar::ReplacementCharacter)
        && !text.contains(QStringLiteral("锟斤拷"))
        && !text.contains(QStringLiteral("Ã"))
        && !text.contains(QStringLiteral("Â"));
}

void testEnumCoverage()
{
    for (const auto phase : {Phase::Start, Phase::Judge, Phase::Draw, Phase::Play, Phase::Discard, Phase::Finish})
        expect(isHealthy(ui::phaseToDisplayName(phase)), "every Phase has healthy display text");
    for (const auto identity : {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade})
        expect(isHealthy(ui::playerIdentityToDisplayName(identity)), "every identity has healthy display text");
    for (const auto suit : {Suit::Spade, Suit::Heart, Suit::Club, Suit::Diamond}) {
        expect(isHealthy(ui::suitToDisplayName(suit)), "every suit has a healthy name");
        expect(isHealthy(ui::suitToSymbol(suit)), "every suit has a healthy symbol");
    }
    for (const auto type : {CardType::Slash, CardType::FireSlash, CardType::ThunderSlash, CardType::Dodge, CardType::Peach, CardType::Wine,
             CardType::ExNihilo, CardType::Dismantlement, CardType::Snatch, CardType::Duel, CardType::BarbarianInvasion, CardType::ArrowBarrage,
             CardType::PeachGarden, CardType::Harvest, CardType::BorrowedSword, CardType::Nullification, CardType::Indulgence, CardType::SupplyShortage,
             CardType::Lightning, CardType::IronChain, CardType::FireAttack, CardType::Weapon, CardType::Armor, CardType::OffensiveHorse, CardType::DefensiveHorse})
        expect(isHealthy(ui::cardTypeToDisplayName(type)), "every CardType has healthy display text");
    for (const auto type : {ResponseType::Dodge, ResponseType::Slash, ResponseType::PeachRescue, ResponseType::BorrowedSwordSlash,
             ResponseType::Nullification, ResponseType::QinglongSlash})
        expect(isHealthy(ui::responseTypeToDisplayName(type)), "every ResponseType has healthy display text");
}

void testSemanticText()
{
    expect(ui::phaseToDisplayName(Phase::Play) == QStringLiteral("出牌阶段"), "Play phase is localized");
    expect(ui::playerIdentityToDisplayName(PlayerIdentity::Lord) == QStringLiteral("主公"), "Lord is localized");
    expect(ui::cardTypeToDisplayName(CardType::Dismantlement) == QStringLiteral("过河拆桥"), "Dismantlement is localized");
    expect(ui::rankToDisplayName(1) == QStringLiteral("A") && ui::rankToDisplayName(13) == QStringLiteral("K"), "rank endpoints are displayed correctly");

    const Card slash("slash", "Slash", CardType::Slash, Suit::Spade, 7);
    const Card fireAttack("fire", "Fire Attack", CardType::FireAttack, Suit::Heart, 2);
    const Card armor("armor", "藤甲", CardType::Armor, Suit::Spade, 2, EquipmentData {EquipmentSlot::Armor, 0});
    expect(ui::cardDescription(slash).contains(QStringLiteral("闪")), "Slash description retains Dodge response rule");
    expect(ui::cardDescription(fireAttack).contains(QStringLiteral("同花色")), "Fire Attack description retains suit-selection rule");
    expect(ui::cardDescription(armor).contains(QStringLiteral("火焰伤害")), "Vine Armor description retains fire-damage rule");
    for (const auto& horse : std::initializer_list<std::pair<const char*, const char*>> {
             {"JueYing", "绝影"}, {"ZhuaHuangFeiDian", "爪黄飞电"}, {"HuaLiu", "骅骝"}, {"DiLu", "的卢"},
             {"DaYuan", "大宛"}, {"ChiTu", "赤兔"}, {"ZiXing", "紫骍"}}) {
        const Card value("horse", horse.first, CardType::OffensiveHorse, Suit::Heart, 5,
                         EquipmentData {EquipmentSlot::OffensiveHorse, 0});
        expect(ui::cardDisplayName(value) == QString::fromUtf8(horse.second), "deck horse internal name is localized");
    }
    for (const auto type : {CardType::Slash, CardType::FireSlash, CardType::ThunderSlash, CardType::Dodge, CardType::Peach, CardType::Wine,
             CardType::ExNihilo, CardType::Dismantlement, CardType::Snatch, CardType::Duel, CardType::BarbarianInvasion, CardType::ArrowBarrage,
             CardType::PeachGarden, CardType::Harvest, CardType::BorrowedSword, CardType::Nullification, CardType::Indulgence, CardType::SupplyShortage,
             CardType::Lightning, CardType::IronChain, CardType::FireAttack, CardType::Weapon, CardType::Armor, CardType::OffensiveHorse, CardType::DefensiveHorse}) {
        const Card value("description", "测试牌", type, Suit::Spade, 1);
        expect(isHealthy(ui::cardDescription(value)), "every CardType has a healthy description");
    }
    expect(ui::logEntryToDisplay("Game started.") == QStringLiteral("游戏开始。"), "existing game-start log branch remains localized");
    expect(ui::logEntryToDisplay("[Iron Chain] resolved.") == QStringLiteral("【铁索连环】结算结束。"), "Iron Chain resolution is localized");
    expect(ui::logEntryToDisplay("Unexpected internal fallback.") == QStringLiteral("收到一条系统消息。"), "unknown player-facing fallback never leaks English");
}

void testLogLocalization()
{
    struct Case { const char* entry; const char* required; };
    const Case cases[] = {
        {"Player 2 used [Nullification].", "无懈可击"},
        {"Waiting for Player 2 to respond with [Nullification].", "等待玩家 2"},
        {"Player 2 triggered [Bagua].", "八卦阵"},
        {"Player 2 judged [Peach] Heart 7.", "♥7"},
        {"Player 2 failed [Indulgence] judgment and skips Play phase.", "乐不思蜀"},
        {"[Lightning] passed to Player 2.", "闪电"},
        {"Player 2 revealed a hand card for [Fire Attack].", "火攻"},
        {"Player 2 revealed [Dodge] Diamond 12 for [Fire Attack].", "【闪 ♦Q】"},
        {"Player 3 obtained [Peach] Heart 12 from [Harvest].", "从【五谷丰登】获得【桃 ♥Q】"},
        {"Player 2 was chained by [Iron Chain].", "横置"},
        {"Player 2 propagated 1 Fire damage to Player 3 through [Iron Chain].", "传导至玩家 3"},
        {"Player 2 propagated 1 Thunder damage to Player 3 through [Iron Chain].", "雷电伤害"},
        {"Player 2 used [Barbarian Invasion].", "南蛮入侵"},
        {"Player 2 used [Arrow Barrage].", "万箭齐发"},
        {"Player 2 was protected by [Vine Armor].", "藤甲"},
        {"Player 2 timed out while responding.", "响应超时"},
        {"Player 2 disconnected; waiting to reconnect.", "等待重连"},
        {"Player 2 reconnected.", "重新连接"},
        {"Player 2 discarded one hand card from Player 3.", "一张手牌"},
    };
    for (const auto& value : cases) {
        const auto display = ui::logEntryToDisplay(value.entry);
        expect(isHealthy(display) && display.contains(QString::fromUtf8(value.required)), "visible log is localized and healthy");
        expect(!display.contains(QStringLiteral("Nullification")) && !display.contains(QStringLiteral("requestId")), "visible log does not expose internal tokens");
    }
    const auto hiddenHand = ui::logEntryToDisplay("Player 2 obtained one hand card from Player 3.");
    expect(!hiddenHand.contains(QStringLiteral("Peach")) && !hiddenHand.contains(QStringLiteral("桃")), "hidden-hand selection does not reveal the card name");
}
} // namespace

int main()
{
    testEnumCoverage();
    testSemanticText();
    testLogLocalization();
    std::cout << "BasicSanguoshaGameTextTests PASS\n";
}
