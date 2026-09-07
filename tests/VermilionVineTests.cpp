#include <algorithm>
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

std::shared_ptr<Card> makeCard(const std::string& id, const std::string& name, CardType type,
                               Suit suit = Suit::Spade, int rank = 1)
{
    return std::make_shared<Card>(id, name, type, suit, rank);
}

std::shared_ptr<Card> makeEquipment(const std::string& id, const std::string& name,
                                    EquipmentSlot slot, int range = 0)
{
    const auto type = slot == EquipmentSlot::Weapon ? CardType::Weapon
        : slot == EquipmentSlot::Armor ? CardType::Armor
        : slot == EquipmentSlot::OffensiveHorse ? CardType::OffensiveHorse : CardType::DefensiveHorse;
    return std::make_shared<Card>(id, name, type, Suit::Spade, 1, EquipmentData {slot, range});
}

void clearHand(const std::shared_ptr<Player>& player)
{
    std::vector<CardId> ids;
    for (const auto& card : player->handCards()) ids.push_back(card->id());
    for (const auto& id : ids) player->removeCard(id);
}

GameEngine twoPlayerGame()
{
    GameEngine game;
    game.createGame({"Attacker", "Target"});
    game.startGame();
    clearHand(game.players().at(0));
    clearHand(game.players().at(1));
    return game;
}

void passDodge(GameEngine& game)
{
    const auto request = game.pendingResponse();
    expect(request.has_value(), "Slash must create a Dodge request");
    expect(request->type == ResponseType::Dodge, "expected a Dodge request");
    expect(game.submitAction(RespondAction {request->responder, request->requestId, std::nullopt}).accepted,
           "Dodge pass must be accepted");
}

void playSlash(GameEngine& game, const std::shared_ptr<Card>& slash)
{
    game.players().at(0)->addCard(slash);
    expect(game.submitAction(PlayCardAction {1, slash->id(), {2}}).accepted, "Slash play must be accepted");
}

bool logContains(const GameEngine& game, const std::string& fragment)
{
    return std::any_of(game.logEntries().begin(), game.logEntries().end(), [&fragment](const auto& entry) {
        return entry.find(fragment) != std::string::npos;
    });
}

void testVermilionConvertsSlashAndVineAmplifies()
{
    auto game = twoPlayerGame();
    game.testingEquip(1, makeEquipment("fan", "朱雀羽扇", EquipmentSlot::Weapon, 4));
    game.testingEquip(2, makeEquipment("vine", "藤甲", EquipmentSlot::Armor));
    playSlash(game, makeCard("slash", "Slash", CardType::Slash));
    passDodge(game);
    expect(game.player(2)->hp() == 2, "Vermilion Fan Slash against Vine must deal 2 Fire damage");
    expect(logContains(game, "converted [Slash] to [Fire Slash] with [Vermilion Fan]"), "conversion must be logged");
    expect(logContains(game, "increased Fire damage by 1 with [Vine Armor]"), "Vine Fire increase must be logged");
    expect(!game.pendingResponse(), "resolved Slash response context must be cleared");
}

void testNormalSlashIsImmuneToVine()
{
    auto game = twoPlayerGame();
    game.testingEquip(2, makeEquipment("vine", "藤甲", EquipmentSlot::Armor));
    playSlash(game, makeCard("slash", "Slash", CardType::Slash));
    expect(game.player(2)->hp() == 4, "normal Slash must be immune to Vine");
    expect(!game.pendingResponse(), "Vine immunity must not leave a response request");
}

void testNativeFireAndThunderSlash()
{
    {
        auto game = twoPlayerGame();
        game.testingEquip(2, makeEquipment("vine", "藤甲", EquipmentSlot::Armor));
        playSlash(game, makeCard("fire", "Fire Slash", CardType::FireSlash, Suit::Heart));
        passDodge(game);
        expect(game.player(2)->hp() == 2, "native Fire Slash must receive Vine +1");
    }
    {
        auto game = twoPlayerGame();
        game.testingEquip(2, makeEquipment("vine", "藤甲", EquipmentSlot::Armor));
        playSlash(game, makeCard("thunder", "Thunder Slash", CardType::ThunderSlash, Suit::Spade));
        passDodge(game);
        expect(game.player(2)->hp() == 3, "Thunder Slash must not receive Vine +1");
    }
}

void testIgnoreArmorBypassesAllVineEffects()
{
    auto game = twoPlayerGame();
    game.testingEquip(1, makeEquipment("fan", "朱雀羽扇", EquipmentSlot::Weapon, 4));
    game.testingEquip(2, makeEquipment("vine", "藤甲", EquipmentSlot::Armor));
    game.testingSetIgnoreArmorForNextSlash(true);
    playSlash(game, makeCard("slash", "Slash", CardType::Slash));
    passDodge(game);
    expect(game.player(2)->hp() == 3, "ignoreArmor Fire Slash must bypass Vine immunity and Fire +1");
}

void testRemovingOrReplacingEquipmentClearsEffects()
{
    {
        auto game = twoPlayerGame();
        game.testingEquip(2, makeEquipment("vine", "藤甲", EquipmentSlot::Armor));
        game.testingRemoveEquipment(2, EquipmentSlot::Armor);
        playSlash(game, makeCard("slash", "Slash", CardType::Slash));
        passDodge(game);
        expect(game.player(2)->hp() == 3, "removed Vine must not retain immunity");
    }
    {
        auto game = twoPlayerGame();
        game.testingEquip(2, makeEquipment("vine", "藤甲", EquipmentSlot::Armor));
        game.testingEquip(2, makeEquipment("silver-lion", "白银狮子", EquipmentSlot::Armor));
        playSlash(game, makeCard("slash", "Slash", CardType::Slash));
        passDodge(game);
        expect(game.player(2)->hp() == 3, "replaced Vine must not retain immunity");
    }
    {
        auto game = twoPlayerGame();
        game.testingEquip(1, makeEquipment("fan", "朱雀羽扇", EquipmentSlot::Weapon, 4));
        game.testingRemoveEquipment(1, EquipmentSlot::Weapon);
        playSlash(game, makeCard("slash", "Slash", CardType::Slash));
        passDodge(game);
        expect(game.player(2)->hp() == 3, "removed Vermilion Fan must not retain Fire conversion");
        expect(!logContains(game, "converted [Slash] to [Fire Slash] with [Vermilion Fan]"), "removed Fan must not log conversion");
    }
}

} // namespace

int main()
{
    testVermilionConvertsSlashAndVineAmplifies();
    testNormalSlashIsImmuneToVine();
    testNativeFireAndThunderSlash();
    testIgnoreArmorBypassesAllVineEffects();
    testRemovingOrReplacingEquipmentClearsEffects();
    std::cout << "BasicSanguoshaVermilionVineTests PASS\n";
    return 0;
}
