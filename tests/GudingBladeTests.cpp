#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cards/Card.h"
#include "core/GameEngine.h"

using namespace sanguosha;

namespace {

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message)
{
    if (!condition) fail(message);
}

std::shared_ptr<Card> card(const std::string& id, CardType type = CardType::Slash)
{
    return std::make_shared<Card>(id, "test", type, Suit::Spade, 7);
}

std::shared_ptr<Card> armor(const std::string& id, const std::string& name)
{
    return std::make_shared<Card>(id, name, CardType::Armor, Suit::Spade, 1,
                                  EquipmentData {EquipmentSlot::Armor, 0});
}

std::shared_ptr<Card> gudingBlade()
{
    return std::make_shared<Card>("guding", "古锭刀", CardType::Weapon, Suit::Spade, 1,
                                  EquipmentData {EquipmentSlot::Weapon, 2});
}

std::shared_ptr<Card> replacementWeapon()
{
    return std::make_shared<Card>("replacement", "青釭剑", CardType::Weapon, Suit::Spade, 6,
                                  EquipmentData {EquipmentSlot::Weapon, 2});
}

void clearHand(const std::shared_ptr<Player>& player)
{
    std::vector<CardId> ids;
    for (const auto& item : player->handCards()) ids.push_back(item->id());
    for (const auto& id : ids) player->removeCard(id);
}

GameEngine threePlayerGame()
{
    GameEngine game;
    game.createGame({"Source", "A", "B"});
    game.startGame();
    for (const auto& player : game.players()) clearHand(player);
    game.testingEquip(1, gudingBlade());
    return game;
}

void passDodge(GameEngine& game)
{
    const auto request = game.pendingResponse();
    expect(request.has_value() && request->type == ResponseType::Dodge, "expected Dodge request");
    expect(game.submitAction(RespondAction {request->responder, request->requestId, std::nullopt}).accepted,
           "Dodge pass must be accepted");
}

void resolveTargets(GameEngine& game, const std::vector<PlayerId>& targets,
                    DamageNature nature = DamageNature::Normal)
{
    game.testingStartSlash(1, targets, 1, nature);
    for (std::size_t index = 0; index < targets.size(); ++index) passDodge(game);
    expect(!game.pendingResponse(), "all Slash responses must be cleared after final target");
}

void setHandCount(GameEngine& game, PlayerId id, int count)
{
    auto player = game.players().at(static_cast<std::size_t>(id - 1));
    clearHand(player);
    for (int index = 0; index < count; ++index) player->addCard(card("hand-" + std::to_string(id) + "-" + std::to_string(index), CardType::Dodge));
}

void testSingleTargets()
{
    {
        auto game = threePlayerGame();
        resolveTargets(game, {2});
        expect(game.player(2)->hp() == 2, "empty-handed single target must take 2");
    }
    {
        auto game = threePlayerGame();
        setHandCount(game, 2, 1);
        resolveTargets(game, {2});
        expect(game.player(2)->hp() == 3, "single target with a hand card must take 1");
    }
}

void testMultiTargetIndependence()
{
    struct Scenario { int aHand; int bHand; int aHp; int bHp; const char* name; };
    const std::vector<Scenario> scenarios {
        {0, 1, 2, 3, "A empty B has card"},
        {1, 0, 3, 2, "A has card B empty"},
        {0, 0, 2, 2, "both empty"},
        {1, 1, 3, 3, "both have cards"},
    };
    for (const auto& scenario : scenarios) {
        auto game = threePlayerGame();
        setHandCount(game, 2, scenario.aHand);
        setHandCount(game, 3, scenario.bHand);
        resolveTargets(game, {2, 3});
        expect(game.player(2)->hp() == scenario.aHp, std::string(scenario.name) + ": wrong A damage");
        expect(game.player(3)->hp() == scenario.bHp, std::string(scenario.name) + ": wrong B damage");
    }
}

void testCurrentHandStateAndContextCleanup()
{
    auto game = threePlayerGame();
    setHandCount(game, 2, 1);
    game.testingStartSlash(1, {2});
    clearHand(game.players().at(1));
    passDodge(game);
    expect(game.player(2)->hp() == 2, "damage must use hand state at damage resolution, not Slash use time");
    game.testingSetHp(2, 4);
    game.testingStartSlash(1, {2});
    game.players().at(1)->addCard(card("new-hand", CardType::Dodge));
    passDodge(game);
    expect(game.player(2)->hp() == 3, "damage must not add Guding bonus when a card is gained before resolution");
    game.testingSetHp(2, 4);
    clearHand(game.players().at(1));
    resolveTargets(game, {2});
    expect(game.player(2)->hp() == 2, "empty-handed target must receive Guding bonus on a fresh Slash");
    game.testingSetHp(2, 4);
    game.players().at(1)->addCard(card("later-hand", CardType::Dodge));
    resolveTargets(game, {2});
    expect(game.player(2)->hp() == 3, "next Slash must not inherit the prior Guding bonus");
}

void testEquipmentChangesAndModifierComposition()
{
    {
        auto game = threePlayerGame();
        game.testingRemoveEquipment(1, EquipmentSlot::Weapon);
        resolveTargets(game, {2});
        expect(game.player(2)->hp() == 3, "removed Guding Blade must immediately stop adding damage");
    }
    {
        auto game = threePlayerGame();
        game.testingEquip(1, replacementWeapon());
        resolveTargets(game, {2});
        expect(game.player(2)->hp() == 3, "replaced Guding Blade must immediately stop adding damage");
    }
    {
        auto game = threePlayerGame();
        game.testingEquip(2, armor("vine", "藤甲"));
        resolveTargets(game, {2}, DamageNature::Fire);
        expect(game.player(2)->hp() == 1, "Guding + Fire + Vine must compose as 1 + 1 + 1 damage");
    }
}

} // namespace

int main()
{
    testSingleTargets();
    testMultiTargetIndependence();
    testCurrentHandStateAndContextCleanup();
    testEquipmentChangesAndModifierComposition();
    std::cout << "BasicSanguoshaGudingBladeTests PASS\n";
    return 0;
}
