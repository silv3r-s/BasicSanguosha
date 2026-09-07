#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cards/Card.h"
#include "core/GameEngine.h"

using namespace sanguosha;

namespace {

[[noreturn]] void fail(const std::string& message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
void expect(bool condition, const std::string& message) { if (!condition) fail(message); }

std::shared_ptr<Card> equipment(const std::string& id, const std::string& name, EquipmentSlot slot)
{
    const auto type = slot == EquipmentSlot::Weapon ? CardType::Weapon : CardType::Armor;
    return std::make_shared<Card>(id, name, type, Suit::Spade, 1, EquipmentData {slot, 2});
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
    game.createGame({"Source", "Target"});
    game.startGame();
    for (const auto& player : game.players()) clearHand(player);
    game.testingEquip(1, equipment("axe", "贯石斧", EquipmentSlot::Weapon));
    return game;
}

void dodge(GameEngine& game)
{
    const auto request = game.pendingResponse();
    expect(request && request->type == ResponseType::Dodge, "Slash must create a Dodge request");
    const auto dodgeId = "dodge-" + std::to_string(request->requestId);
    game.players().at(static_cast<std::size_t>(request->responder - 1))->addCard(
        std::make_shared<Card>(dodgeId, "Dodge", CardType::Dodge, Suit::Heart, 2));
    expect(game.submitAction(RespondAction {request->responder, request->requestId, dodgeId}).accepted,
           "Dodge response must be accepted");
}

void testSnapshotAndResumePreserveFireAndIgnoreArmor()
{
    auto game = twoPlayerGame();
    game.testingEquip(2, equipment("vine", "藤甲", EquipmentSlot::Armor));
    game.testingStartSlash(1, {2}, 1, DamageNature::Fire, true);
    dodge(game);
    const auto snapshot = game.testingDeferredSlashDamage();
    expect(snapshot.has_value(), "a real Dodge must establish a damage snapshot");
    expect(snapshot->source == 1 && snapshot->target == 2, "snapshot must preserve source and target");
    expect(snapshot->damageAmount == 1 && snapshot->damageNature == DamageNature::Fire && snapshot->ignoreArmor,
           "snapshot must preserve base damage, Fire nature, and ignoreArmor");
    expect(game.testingResumeDeferredSlashDamage(), "authorized resume must accept a valid snapshot");
    expect(game.player(2)->hp() == 3, "ignoreArmor resume must bypass Vine and deal exactly one Fire damage");
    expect(!game.pendingResponse(), "resume must not create a second Dodge request");
    expect(!game.testingDeferredSlashDamage(), "snapshot must clear after exactly one resume");
    expect(!game.testingResumeDeferredSlashDamage(), "a consumed snapshot must not be resumable twice");
}

void testRealSlashIsNotUsedTwiceWhenResumed()
{
    auto game = twoPlayerGame();
    game.players().at(0)->addCard(std::make_shared<Card>("real-slash", "Slash", CardType::Slash, Suit::Spade, 7));
    expect(game.submitAction(PlayCardAction {1, "real-slash", {2}}).accepted, "real Slash must be accepted");
    dodge(game);
    expect(game.slashUsedThisPhase() == 1 && game.players().at(0)->handCards().empty(),
           "the original Slash must be consumed exactly once before resume");
    const auto discardBeforeResume = game.deck().discardPileSize();
    expect(game.testingResumeDeferredSlashDamage(), "real Slash snapshot must resume");
    expect(game.slashUsedThisPhase() == 1 && game.players().at(0)->handCards().empty(),
           "resume must neither consume another Slash nor increment Slash use count");
    expect(game.deck().discardPileSize() == discardBeforeResume + 1, "resume must discard only the already-used original Slash");
}

void testResumeUsesNormalDamagePipeline()
{
    auto game = twoPlayerGame();
    game.testingEquip(2, equipment("vine", "藤甲", EquipmentSlot::Armor));
    game.testingStartSlash(1, {2}, 1, DamageNature::Fire);
    dodge(game);
    expect(game.testingResumeDeferredSlashDamage(), "Fire snapshot must resume");
    expect(game.player(2)->hp() == 2, "resumed Fire damage must receive Vine +1 through normal damage processing");
}

void testMultiTargetSnapshotKeepsDodgedTargetIndependent()
{
    GameEngine game;
    game.createGame({"Source", "A", "B"});
    game.startGame();
    for (const auto& player : game.players()) clearHand(player);
    game.testingEquip(1, equipment("axe", "贯石斧", EquipmentSlot::Weapon));
    game.testingStartSlash(1, {2, 3});
    dodge(game);
    expect(game.testingResumeDeferredSlashDamage(), "first target snapshot must resume before continuation");
    const auto request = game.pendingResponse();
    expect(request && request->type == ResponseType::Dodge && request->responder == 3,
           "a dodged first target must advance normally to the second target");
    expect(game.submitAction(RespondAction {3, request->requestId, std::nullopt}).accepted,
           "second target pass must be accepted");
    expect(game.player(3)->hp() == 3, "second target must resolve independently before resume");
    expect(game.player(2)->hp() == 3, "resume must damage only the original dodged target");
}

void testSnapshotClearsOnDeathAndRestart()
{
    {
        auto game = twoPlayerGame();
        game.testingStartSlash(1, {2});
        dodge(game);
        expect(game.testingDeferredSlashDamage().has_value(), "Dodge must establish snapshot before cleanup");
        game.testingKillPlayer(1);
        expect(!game.testingDeferredSlashDamage(), "source death must clear snapshot");
        expect(!game.testingResumeDeferredSlashDamage(), "dead source snapshot must be rejected");
        expect(game.gameOver(), "source death in the two-player game must also exercise GameOver cleanup");
    }
    {
        GameEngine game;
        game.createGame({"Source", "Target", "Other"});
        game.startGame();
        for (const auto& player : game.players()) clearHand(player);
        game.testingEquip(1, equipment("axe", "贯石斧", EquipmentSlot::Weapon));
        game.testingStartSlash(1, {2});
        dodge(game);
        game.testingKillPlayer(2);
        expect(!game.testingDeferredSlashDamage(), "target death must clear snapshot");
        expect(!game.gameOver(), "target death with another opponent alive must not require GameOver");
    }
    {
        auto game = twoPlayerGame();
        game.testingStartSlash(1, {2}, 3);
        dodge(game);
        expect(game.testingDeferredSlashDamage()->damageAmount == 3, "snapshot must preserve a pre-modified damage amount");
        game.restart();
        expect(!game.testingDeferredSlashDamage(), "restart must clear snapshot");
    }
}

} // namespace

int main()
{
    testSnapshotAndResumePreserveFireAndIgnoreArmor();
    testRealSlashIsNotUsedTwiceWhenResumed();
    testResumeUsesNormalDamagePipeline();
    testMultiTargetSnapshotKeepsDodgedTargetIndependent();
    testSnapshotClearsOnDeathAndRestart();
    std::cout << "BasicSanguoshaSlashDamageResumeTests PASS\n";
    return 0;
}
