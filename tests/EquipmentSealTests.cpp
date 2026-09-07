#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cards/StandardDeckDefinition.h"
#include "core/GameEngine.h"
#include "ui/GameText.h"

using namespace sanguosha;

namespace {
[[noreturn]] void fail(const char* message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
void expect(bool value, const char* message) { if (!value) fail(message); }

std::shared_ptr<Card> equipment(const CardId& id, const std::string& name, CardType type, EquipmentSlot slot, int range = 0)
{
    return std::make_shared<Card>(id, name, type, Suit::Spade, 1, EquipmentData {slot, range});
}

std::shared_ptr<Card> card(const CardId& id, CardType type, Suit suit = Suit::Spade)
{
    return std::make_shared<Card>(id, id, type, suit, 1);
}

void clear(Player& player)
{
    std::vector<CardId> ids;
    for (const auto& value : player.handCards()) ids.push_back(value->id());
    for (const auto& id : ids) player.removeCard(id);
}

GameEngine game(int playerCount = 2)
{
    GameEngine result;
    std::vector<std::string> names;
    for (int index = 1; index <= playerCount; ++index) names.push_back("P" + std::to_string(index));
    result.createGame(names);
    result.startGame();
    for (const auto& player : result.players()) clear(*player);
    return result;
}

void passResponse(GameEngine& engine)
{
    const auto request = engine.pendingResponse();
    expect(request.has_value(), "expected a response request");
    expect(engine.submitAction(RespondAction {request->responder, request->requestId, std::nullopt}).accepted,
           "response pass is accepted");
}

void passNullificationWindows(GameEngine& engine)
{
    while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) passResponse(engine);
}

void testDeckMetadataAndDescriptions()
{
    const auto& deck = standardDeckDefinition();
    const auto count = [&deck](CardType type) {
        return std::count_if(deck.begin(), deck.end(), [type](const auto& definition) { return definition.type == type; });
    };
    expect(count(CardType::Weapon) == 13 && count(CardType::Armor) == 6
               && count(CardType::OffensiveHorse) == 3 && count(CardType::DefensiveHorse) == 4,
           "standard deck has 13 weapons, 6 armor cards, and 7 horses");
    expect(deck.size() == 161, "standard deck remains the authoritative 161-card deck");

    const auto hasHorse = [&deck](const char* name, CardType type, Suit suit, int rank) {
        return std::any_of(deck.begin(), deck.end(), [&](const auto& definition) {
            return definition.name == name && definition.type == type && definition.suit == suit && definition.rank == rank
                && definition.equipmentData && definition.equipmentData->slot
                    == (type == CardType::OffensiveHorse ? EquipmentSlot::OffensiveHorse : EquipmentSlot::DefensiveHorse);
        });
    };
    expect(hasHorse("JueYing", CardType::DefensiveHorse, Suit::Spade, 5)
               && hasHorse("ZhuaHuangFeiDian", CardType::DefensiveHorse, Suit::Heart, 13)
               && hasHorse("HuaLiu", CardType::DefensiveHorse, Suit::Diamond, 13)
               && hasHorse("DiLu", CardType::DefensiveHorse, Suit::Club, 5)
               && hasHorse("DaYuan", CardType::OffensiveHorse, Suit::Spade, 13)
               && hasHorse("ChiTu", CardType::OffensiveHorse, Suit::Heart, 5)
               && hasHorse("ZiXing", CardType::OffensiveHorse, Suit::Diamond, 13),
           "all seven official horses retain type, suit, rank, and slot metadata");

    for (const auto& definition : deck) {
        if (!definition.equipmentData || definition.name == "银月枪") continue;
        const Card value("seal-description", definition.name, definition.type, definition.suit, definition.rank,
                         definition.equipmentData, definition.variant, definition.horseName);
        const auto description = ui::cardDescription(value);
        expect(!description.isEmpty() && !description.contains(QStringLiteral("暂未实现")),
               "implemented equipment has a non-placeholder description");
    }
}

void testArmorResolutionAndLifecycle()
{
    { auto engine = game(); engine.testingEquip(2, equipment("shield", "仁王盾", CardType::Armor, EquipmentSlot::Armor));
        engine.players()[0]->addCard(card("black", CardType::Slash, Suit::Spade));
        expect(engine.submitAction(PlayCardAction {1, "black", {2}}).accepted, "black Slash is playable against Renwang Shield");
        passNullificationWindows(engine);
        passResponse(engine);
        expect(engine.player(2)->hp() == 4 && !engine.pendingResponse(),
               "Renwang Shield blocks black normal Slash"); }
    { auto engine = game(); engine.testingEquip(2, equipment("shield", "仁王盾", CardType::Armor, EquipmentSlot::Armor));
        engine.players()[0]->addCard(card("red", CardType::Slash, Suit::Heart));
        expect(engine.submitAction(PlayCardAction {1, "red", {2}}).accepted, "red Slash is playable against Renwang Shield");
        passNullificationWindows(engine); passResponse(engine); expect(engine.player(2)->hp() == 3, "Renwang Shield does not block red normal Slash"); }
    { auto engine = game(); engine.testingEquip(2, equipment("shield", "仁王盾", CardType::Armor, EquipmentSlot::Armor));
        engine.testingStartSlash(1, {2}, 1, DamageNature::Fire); passResponse(engine); expect(engine.player(2)->hp() == 3, "Renwang Shield does not block Fire Slash"); }
    { auto engine = game(); engine.testingEquip(2, equipment("shield", "仁王盾", CardType::Armor, EquipmentSlot::Armor));
        engine.testingStartSlash(1, {2}, 1, DamageNature::Normal, true); passResponse(engine); expect(engine.player(2)->hp() == 3, "ignoreArmor bypasses Renwang Shield"); }

    { auto engine = game(); engine.testingEquip(2, equipment("vine", "藤甲", CardType::Armor, EquipmentSlot::Armor));
        engine.testingStartSlash(1, {2}); passResponse(engine); expect(engine.player(2)->hp() == 4 && !engine.pendingResponse(), "Vine Armor blocks normal Slash");
        engine.testingStartSlash(1, {2}, 1, DamageNature::Fire); passResponse(engine); expect(engine.player(2)->hp() == 2, "Vine Armor amplifies Fire Slash exactly once"); }
    { auto engine = game(); engine.testingEquip(2, equipment("vine", "藤甲", CardType::Armor, EquipmentSlot::Armor));
        engine.testingStartSlash(1, {2}, 1, DamageNature::Thunder); passResponse(engine); expect(engine.player(2)->hp() == 3, "Vine Armor does not amplify Thunder Slash");
        engine.testingStartSlash(1, {2}, 1, DamageNature::Normal, true); passResponse(engine); expect(engine.player(2)->hp() == 2, "ignoreArmor bypasses all Vine Armor effects"); }
    for (const auto aoe : {CardType::BarbarianInvasion, CardType::ArrowBarrage}) {
        auto engine = game(); engine.testingEquip(2, equipment("vine", "藤甲", CardType::Armor, EquipmentSlot::Armor));
        const CardId id = aoe == CardType::BarbarianInvasion ? "barbarian" : "arrows";
        engine.players()[0]->addCard(card(id, aoe));
        expect(engine.submitAction(PlayCardAction {1, id, {}}).accepted, "AOE is playable against Vine Armor");
        passNullificationWindows(engine);
        expect(engine.player(2)->hp() == 4 && !engine.pendingResponse(), "Vine Armor blocks the corresponding AOE without a response");
    }

    { auto engine = game(); engine.testingEquip(2, equipment("lion", "白银狮子", CardType::Armor, EquipmentSlot::Armor));
        engine.testingStartSlash(1, {2}, 3); passResponse(engine); expect(engine.player(2)->hp() == 3, "Silver Lion limits each damage instance to one");
        engine.testingRemoveEquipment(2, EquipmentSlot::Armor); expect(engine.player(2)->hp() == 4, "removing wounded Silver Lion recovers one HP");
        engine.testingRemoveEquipment(2, EquipmentSlot::Armor); expect(engine.player(2)->hp() == 4, "removing no armor cannot over-heal"); }
    { auto engine = game(); engine.testingEquip(1, equipment("lion", "白银狮子", CardType::Armor, EquipmentSlot::Armor));
        engine.testingApplyDamage(Damage {2, 1, 2, DamageNature::Normal, true}); expect(engine.player(1)->hp() == 2, "ignoreArmor bypasses Silver Lion limit");
        engine.players()[0]->addCard(equipment("vine", "藤甲", CardType::Armor, EquipmentSlot::Armor));
        expect(engine.submitAction(PlayCardAction {1, "vine", {}}).accepted && engine.player(1)->hp() == 3,
               "formal armor replacement performs Silver Lion leave-play recovery"); }
}

void testBaguaAndHorseDistance()
{
    { auto engine = game(); engine.testingEquip(2, equipment("bagua", "八卦阵", CardType::Armor, EquipmentSlot::Armor));
        engine.testingAddToDrawPile(card("red-judge", CardType::Peach, Suit::Heart)); engine.testingStartSlash(1, {2});
        expect(!engine.pendingResponse() && engine.player(2)->hp() == 4 && engine.latestJudgment() && engine.latestJudgment()->succeeded,
               "red Bagua judgment supplies Dodge before manual response"); }
    { auto engine = game(); engine.testingEquip(2, equipment("bagua", "八卦阵", CardType::Armor, EquipmentSlot::Armor));
        engine.testingAddToDrawPile(card("black-judge", CardType::Slash, Suit::Spade)); engine.testingStartSlash(1, {2});
        expect(engine.pendingResponse() && engine.latestJudgment() && !engine.latestJudgment()->succeeded,
               "black Bagua judgment creates the manual Dodge response"); passResponse(engine); }
    { auto engine = game(); engine.testingEquip(2, equipment("bagua", "八卦阵", CardType::Armor, EquipmentSlot::Armor));
        engine.testingStartSlash(1, {2}, 1, DamageNature::Normal, true);
        expect(engine.pendingResponse() && !engine.latestJudgment(), "ignoreArmor bypasses Bagua judgment"); passResponse(engine); }

    auto engine = game(4);
    expect(engine.distanceBetween(1, 3) == 2 && engine.attackRange(1) == 1, "base distance and attack range are correct");
    engine.players()[0]->addCard(card("far-slash", CardType::Slash));
    engine.players()[0]->addCard(card("far-snatch", CardType::Snatch));
    engine.players()[2]->addCard(card("snatchable", CardType::Peach));
    expect(!engine.canPlayCardOnTarget(1, "far-slash", 3) && !engine.canPlayCardOnTarget(1, "far-snatch", 3),
           "base range rejects distant Slash and Snatch");
    engine.testingEquip(1, equipment("off", "赤兔", CardType::OffensiveHorse, EquipmentSlot::OffensiveHorse));
    engine.testingEquip(3, equipment("def", "绝影", CardType::DefensiveHorse, EquipmentSlot::DefensiveHorse));
    expect(engine.distanceBetween(1, 3) == 2, "offensive and defensive horse modifiers compose correctly");
    engine.testingRemoveEquipment(3, EquipmentSlot::DefensiveHorse); expect(engine.distanceBetween(1, 3) == 1 && engine.canPlayCardOnTarget(1, "far-slash", 3)
                && engine.canPlayCardOnTarget(1, "far-snatch", 3), "removing defensive horse updates Slash and Snatch distance immediately");
    engine.testingEquip(1, equipment("off-replacement", "大宛", CardType::OffensiveHorse, EquipmentSlot::OffensiveHorse));
    expect(engine.player(1)->equipment(EquipmentSlot::OffensiveHorse)->id() == "off-replacement", "same-slot horse replacement is authoritative");
    engine.testingRemoveEquipment(1, EquipmentSlot::OffensiveHorse); expect(engine.distanceBetween(1, 3) == 2, "removing offensive horse restores distance immediately");
}
void testEquipmentEffectText()
{
    using View = EquipmentEffectEventView;
    expect(ui::formatEquipmentEffect(View {10, "朱雀羽扇", EquipmentEffectTypeView::SlashConvertedToFire, 1, 0, 0, CardType::Slash}, {}).contains("火【杀】"), "Zhuque effect has structured UI text");
    expect(ui::formatEquipmentEffect(View {11, "藤甲", EquipmentEffectTypeView::DamagePrevented, 2, 2, 0, CardType::BarbarianInvasion}, {}).contains("南蛮入侵"), "Vine AOE effect has structured UI text");
    expect(ui::formatEquipmentEffect(View {12, "白银狮子", EquipmentEffectTypeView::HealOnLeave, 2, 2, 1, CardType::Slash}, {}).contains("回复 1"), "Silver Lion healing uses event value");
    std::uint64_t last = 0;
    const std::vector<View> first {{10, "朱雀羽扇", EquipmentEffectTypeView::SlashConvertedToFire, 1, 0, 0, CardType::Slash}};
    expect(ui::newEquipmentEffects(first, last).size() == 1 && last == 10 && ui::newEquipmentEffects(first, last).empty(), "same snapshot does not redisplay an equipment effect");
    const std::vector<View> incremental {first.front(), {11, "藤甲", EquipmentEffectTypeView::FireDamageIncreased, 2, 2, 1, CardType::FireSlash}};
    const auto newer = ui::newEquipmentEffects(incremental, last);
    expect(newer.size() == 1 && newer.front().eventId == 11 && last == 11, "incremental snapshot consumes only the newer event in order");
    last = 0; expect(ui::newEquipmentEffects(first, last).size() == 1, "new game reset permits event ID one sequence again");
}
} // namespace

int main()
{
    testDeckMetadataAndDescriptions();
    testArmorResolutionAndLifecycle();
    testBaguaAndHorseDistance();
    testEquipmentEffectText();
    std::cout << "BasicSanguoshaEquipmentSealTests PASS\n";
}
