#ifdef NDEBUG
#undef NDEBUG
#endif

#include "HalberdTests.h"
#include "TestHelpers.h"

#include <cassert>
#include <memory>
#include <optional>
#include <string>

using namespace sanguosha;

void runHalberdTests()
{
{ // Stage 3.3: Halberd uses a last physical hand Slash for up to three independent targets.
    auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3];
    game.testingEquip(a.id(), std::make_shared<Card>("halberd", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4}));
    a.addCard(card("halberd-slash", CardType::Slash));
    assert(game.submitAction(PlayCardAction {a.id(), "halberd-slash", {b.id(), c.id(), d.id()}}).accepted);
    auto request = *game.pendingResponse(); assert(request.responder == b.id()); assert(game.submitAction(RespondAction {b.id(), request.requestId, CardId("missing")}).accepted == false);
    assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted);
    request = *game.pendingResponse(); assert(request.responder == c.id()); c.addCard(card("halberd-dodge", CardType::Dodge)); assert(game.submitAction(RespondAction {c.id(), request.requestId, CardId("halberd-dodge")}).accepted);
    request = *game.pendingResponse(); assert(request.responder == d.id()); assert(game.submitAction(RespondAction {d.id(), request.requestId, std::nullopt}).accepted);
    assert(b.hp() == 3 && c.hp() == 4 && d.hp() == 3 && !game.pendingResponse() && game.slashUsedThisPhase() == 1);
}
{ // Halberd validation happens before consuming the physical Slash.
    auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3];
    a.addCard(card("normal-multi", CardType::Slash)); const auto before = a.handCards().size();
    assert(!game.submitAction(PlayCardAction {a.id(), "normal-multi", {b.id(), c.id()}}).accepted && a.handCards().size() == before && game.slashUsedThisPhase() == 0);
    game.testingEquip(a.id(), std::make_shared<Card>("halberd-2", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4})); a.addCard(card("extra", CardType::Peach));
    assert(!game.submitAction(PlayCardAction {a.id(), "normal-multi", {b.id(), c.id()}}).accepted);
    a.removeCard("extra"); assert(!game.submitAction(PlayCardAction {a.id(), "normal-multi", {b.id(), c.id(), d.id(), 99}}).accepted);
    assert(!game.submitAction(PlayCardAction {a.id(), "normal-multi", {b.id(), b.id()}}).accepted);
}
{ // Stage 3.3 regression: Bagua is evaluated only at its own Halberd target, then resolution continues.
    auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3];
    game.testingEquip(a.id(), std::make_shared<Card>("h-bagua-halberd", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4}));
    game.testingEquip(c.id(), std::make_shared<Card>("h-bagua", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0}));
    game.testingAddToDrawPile(std::make_shared<Card>("h-bagua-red", "judge", CardType::Slash, Suit::Heart, 1)); a.addCard(card("h-bagua-slash", CardType::Slash));
    assert(game.submitAction(PlayCardAction {a.id(), "h-bagua-slash", {b.id(), c.id(), d.id()}}).accepted);
    auto request = *game.pendingResponse(); assert(request.responder == b.id()); assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted);
    assert(game.latestJudgment() && game.latestJudgment()->player == c.id());
    assert(c.hp() == 4 && game.deck().discardPileSize() >= 1 && game.submitAction(EndPlayPhaseAction {a.id()}).accepted == false);
    // Bagua's successful automatic Dodge advances immediately to the final target.
    assert(game.pendingResponse() && game.pendingResponse()->responder == d.id()); request = *game.pendingResponse(); assert(game.submitAction(RespondAction {d.id(), request.requestId, std::nullopt}).accepted && d.hp() == 3 && !game.pendingResponse());
}
{ // Fire and Thunder Halberd contexts keep nature and per-target damage independent.
    const auto run = [](CardType type, int vineHp, const char* id) { auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3]; game.testingEquip(a.id(), std::make_shared<Card>(std::string(id)+"-h", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4})); game.testingEquip(c.id(), std::make_shared<Card>(std::string(id)+"-v", "藤甲", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); a.addCard(card(id, type)); assert(game.submitAction(PlayCardAction {a.id(), id, {b.id(), c.id(), d.id()}}).accepted); for (const auto responder : {b.id(), c.id(), d.id()}) { const auto request = *game.pendingResponse(); assert(request.responder == responder && game.submitAction(RespondAction {responder, request.requestId, std::nullopt}).accepted); } assert(b.hp() == 3 && c.hp() == vineHp && d.hp() == 3 && !game.pendingResponse()); };
    run(CardType::FireSlash, 2, "h-fire"); run(CardType::ThunderSlash, 3, "h-thunder");
}
{ // A Dodge timeout is a pass for one target and still advances the Halberd queue.
    auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3]; game.testingEquip(a.id(), std::make_shared<Card>("h-timeout-h", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4})); a.addCard(card("h-timeout", CardType::Slash)); assert(game.submitAction(PlayCardAction {a.id(), "h-timeout", {b.id(), c.id(), d.id()}}).accepted); auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted); assert(game.pendingResponse()->responder == c.id() && game.handleTimeout().accepted && c.hp() == 3 && game.pendingResponse()->responder == d.id()); request = *game.pendingResponse(); assert(game.submitAction(RespondAction {d.id(), request.requestId, std::nullopt}).accepted && !game.pendingResponse());
}
{ // Death and already-dead targets neither lose nor corrupt the remaining Halberd queue.
    auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3]; game.testingEquip(a.id(), std::make_shared<Card>("h-death-h", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4})); game.testingSetHp(b.id(), 1); a.addCard(card("h-death", CardType::Slash)); assert(game.submitAction(PlayCardAction {a.id(), "h-death", {b.id(), c.id(), d.id()}}).accepted); auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted); while (game.pendingResponse() && game.pendingResponse()->type == ResponseType::PeachRescue) { request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); } assert(!b.isAlive() && game.pendingResponse() && game.pendingResponse()->responder == c.id()); game.testingKillPlayer(c.id()); assert(game.pendingResponse() && game.pendingResponse()->responder == d.id()); request = *game.pendingResponse(); assert(game.submitAction(RespondAction {d.id(), request.requestId, std::nullopt}).accepted && d.hp() == 3 && !game.pendingResponse());
}
{ // A black Bagua judgment creates its owner's normal Dodge request before continuing.
    auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3]; game.testingEquip(a.id(), std::make_shared<Card>("h-bagua-black-h", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4})); game.testingEquip(c.id(), std::make_shared<Card>("h-bagua-black", "八卦阵", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); game.testingAddToDrawPile(std::make_shared<Card>("h-bagua-black-judge", "judge", CardType::Slash, Suit::Club, 1)); c.addCard(card("h-bagua-black-dodge", CardType::Dodge)); a.addCard(card("h-bagua-black-slash", CardType::Slash)); assert(game.submitAction(PlayCardAction {a.id(), "h-bagua-black-slash", {b.id(), c.id(), d.id()}}).accepted); auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted); request = *game.pendingResponse(); assert(request.responder == c.id() && game.latestJudgment() && game.latestJudgment()->player == c.id()); assert(game.submitAction(RespondAction {c.id(), request.requestId, "h-bagua-black-dodge"}).accepted && game.pendingResponse()->responder == d.id());
}
{ // ignoreArmor is retained while a multi-target context advances, then a new Slash starts cleanly.
    auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3]; game.testingEquip(a.id(), std::make_shared<Card>("h-ignore-h", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4})); game.testingEquip(b.id(), std::make_shared<Card>("h-ignore-r", "仁王盾", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); game.testingEquip(c.id(), std::make_shared<Card>("h-ignore-v", "藤甲", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); game.testingEquip(d.id(), std::make_shared<Card>("h-ignore-s", "白银狮子", CardType::Armor, Suit::Spade, 1, EquipmentData {EquipmentSlot::Armor, 0})); a.addCard(card("h-ignore", CardType::Slash)); game.testingSetIgnoreArmorForNextSlash(true); assert(game.submitAction(PlayCardAction {a.id(), "h-ignore", {b.id(), c.id(), d.id()}}).accepted); for (const auto id : {b.id(), c.id(), d.id()}) { const auto request = *game.pendingResponse(); assert(request.responder == id && game.submitAction(RespondAction {id, request.requestId, std::nullopt}).accepted); } assert(b.hp() == 3 && c.hp() == 3 && d.hp() == 3 && !game.pendingResponse()); for (int i = 0; i < 4; ++i) finishCurrentTurn(game); assert(game.currentPlayer()->id() == a.id() && game.currentPhase() == Phase::Play); clearHand(a); clearHand(b); a.addCard(std::make_shared<Card>("h-clean", "h-clean", CardType::Slash, Suit::Heart, 1)); assert(game.slashUsedThisPhase() == 0); assert(game.canPlayCardOnTarget(a.id(), "h-clean", b.id())); assert(game.submitAction(PlayCardAction {a.id(), "h-clean", {b.id()}}).accepted); assert(game.pendingResponse() && game.pendingResponse()->responder == b.id());
}
{ // A middle target's death does not terminate the remaining Halberd target queue.
    auto game = fourPlayerGame(); auto& a = *game.players()[0]; auto& b = *game.players()[1]; auto& c = *game.players()[2]; auto& d = *game.players()[3]; game.testingEquip(a.id(), std::make_shared<Card>("h-middle-h", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4})); game.testingSetHp(c.id(), 1); a.addCard(card("h-middle", CardType::Slash)); assert(game.submitAction(PlayCardAction {a.id(), "h-middle", {b.id(), c.id(), d.id()}}).accepted); auto request = *game.pendingResponse(); assert(game.submitAction(RespondAction {b.id(), request.requestId, std::nullopt}).accepted && game.pendingResponse()->responder == c.id()); request = *game.pendingResponse(); assert(game.submitAction(RespondAction {c.id(), request.requestId, std::nullopt}).accepted); while (game.pendingResponse() && game.pendingResponse()->type == ResponseType::PeachRescue) { request = *game.pendingResponse(); assert(game.submitAction(RespondAction {request.responder, request.requestId, std::nullopt}).accepted); } assert(!c.isAlive() && game.pendingResponse() && game.pendingResponse()->responder == d.id()); request = *game.pendingResponse(); assert(game.submitAction(RespondAction {d.id(), request.requestId, std::nullopt}).accepted && !game.pendingResponse());
}
}
