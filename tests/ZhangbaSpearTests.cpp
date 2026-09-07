#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cards/Card.h"
#include "session/GameSession.h"

using namespace sanguosha;

namespace {
[[noreturn]] void fail(const std::string& message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
void expect(bool value, const std::string& message) { if (!value) fail(message); }
std::shared_ptr<Card> card(const std::string& id, CardType type) { return std::make_shared<Card>(id, "test", type, Suit::Heart, 7); }
std::shared_ptr<Card> spear() { return std::make_shared<Card>("spear", "丈八蛇矛", CardType::Weapon, Suit::Spade, 12, EquipmentData {EquipmentSlot::Weapon, 3}); }
void clearHand(const std::shared_ptr<Player>& player) { std::vector<CardId> ids; for (const auto& item : player->handCards()) ids.push_back(item->id()); for (const auto& id : ids) player->removeCard(id); }
struct Fixture {
    GameSession session {{"A", "B", "C"}};
    SessionClientId a {session.addClient(1)}; SessionClientId b {session.addClient(2)}; SessionClientId c {session.addClient(3)};
    Fixture() { for (const auto& player : session.testingEngine().players()) clearHand(player); }
};
void equipSpear(Fixture& f, PlayerId id = 1) { f.session.testingEngine().testingEquip(id, spear()); }
void passNullificationWindows(Fixture& f) { while (true) { const auto request = f.session.testingEngine().pendingResponse(); if (!request || request->type != ResponseType::Nullification) return; expect(f.session.submitAction(request->responder == 1 ? f.a : request->responder == 2 ? f.b : f.c, RespondAction {request->responder, request->requestId, std::nullopt}).accepted, "Nullification pass accepted"); } }

void testActiveValidationAndConsumption()
{
    Fixture f; equipSpear(f);
    auto actor = f.session.testingEngine().players().at(0);
    actor->addCard(card("one", CardType::Peach)); actor->addCard(card("two", CardType::Dodge));
    expect(f.session.viewFor(f.a).serpentSpearSlashTargets == std::vector<PlayerId> {2, 3}, "authoritative view exposes virtual Slash targets only to owner");
    expect(!f.session.submitAction(f.a, PlayVirtualSlashAction {1, {"one"}, {2}}).accepted, "one material is rejected");
    expect(!f.session.submitAction(f.a, PlayVirtualSlashAction {1, {"one", "one"}, {2}}).accepted, "duplicate material is rejected");
    expect(!f.session.submitAction(f.a, PlayVirtualSlashAction {1, {"one", "foreign"}, {2}}).accepted, "foreign material is rejected without consuming own card");
    expect(actor->handCards().size() == 2, "rejected actions do not consume materials");
    const auto discardBefore = f.session.testingEngine().deck().discardPileSize();
    expect(f.session.submitAction(f.a, PlayVirtualSlashAction {1, {"one", "two"}, {2}}).accepted, "two arbitrary hand cards create a virtual Slash");
    expect(actor->handCards().empty() && f.session.testingEngine().deck().discardPileSize() == discardBefore + 2, "both materials are consumed exactly once");
    expect(f.session.testingEngine().pendingResponse() && f.session.testingEngine().pendingResponse()->type == ResponseType::Dodge, "virtual Slash enters ordinary Dodge flow");
}

void testEquipmentRemovalAndSlashResponses()
{
    { Fixture f; auto actor = f.session.testingEngine().players().at(0); actor->addCard(card("a", CardType::Peach)); actor->addCard(card("b", CardType::Dodge)); expect(!f.session.submitAction(f.a, PlayVirtualSlashAction {1, {"a", "b"}, {2}}).accepted, "Spear is required"); equipSpear(f); f.session.testingEngine().testingRemoveEquipment(1, EquipmentSlot::Weapon); expect(!f.session.submitAction(f.a, PlayVirtualSlashAction {1, {"a", "b"}, {2}}).accepted, "removing Spear immediately disables it"); equipSpear(f); f.session.testingEngine().testingEquip(1, std::make_shared<Card>("other-weapon", "青釭剑", CardType::Weapon, Suit::Spade, 6, EquipmentData {EquipmentSlot::Weapon, 2})); expect(!f.session.submitAction(f.a, PlayVirtualSlashAction {1, {"a", "b"}, {2}}).accepted, "replacing Spear immediately disables it"); }
    { Fixture f; equipSpear(f, 2); auto source = f.session.testingEngine().players().at(0); auto responder = f.session.testingEngine().players().at(1); source->addCard(card("aoe", CardType::BarbarianInvasion)); responder->addCard(card("x", CardType::Peach)); responder->addCard(card("y", CardType::Dodge)); expect(f.session.submitAction(f.a, PlayCardAction {1, "aoe", {}}).accepted, "AOE starts"); passNullificationWindows(f); const auto request = *f.session.testingEngine().pendingResponse(); expect(request.responder == 2 && f.session.submitAction(f.b, RespondVirtualSlashAction {2, request.requestId, {"x", "y"}}).accepted, "Spear responds to Barbarian Invasion"); }
    { Fixture f; equipSpear(f, 2); auto source = f.session.testingEngine().players().at(0); auto responder = f.session.testingEngine().players().at(1); source->addCard(card("duel", CardType::Duel)); responder->addCard(card("x", CardType::Peach)); responder->addCard(card("y", CardType::Dodge)); expect(f.session.submitAction(f.a, PlayCardAction {1, "duel", {2}}).accepted, "Duel starts"); passNullificationWindows(f); const auto request = *f.session.testingEngine().pendingResponse(); expect(f.session.submitAction(f.b, RespondVirtualSlashAction {2, request.requestId, {"x", "y"}}).accepted, "Spear responds to Duel"); }
    { Fixture f; equipSpear(f, 2); auto source = f.session.testingEngine().players().at(0); auto responder = f.session.testingEngine().players().at(1); source->addCard(card("borrow", CardType::BorrowedSword)); responder->addCard(card("x", CardType::Peach)); responder->addCard(card("y", CardType::Dodge)); expect(f.session.submitAction(f.a, PlayCardAction {1, "borrow", {2, 3}}).accepted, "Borrowed Sword starts"); passNullificationWindows(f); const auto request = *f.session.testingEngine().pendingResponse(); expect(request.type == ResponseType::BorrowedSwordSlash && f.session.submitAction(f.b, RespondVirtualSlashAction {2, request.requestId, {"x", "y"}}).accepted, "Spear responds to Borrowed Sword"); expect(f.session.testingEngine().pendingResponse() && f.session.testingEngine().pendingResponse()->type == ResponseType::Dodge, "Borrowed Sword virtual Slash enters ordinary Dodge flow"); }
}

void testRequestBindingAndPrivateView()
{
    Fixture f; equipSpear(f, 2);
    auto source = f.session.testingEngine().players().at(0); auto responder = f.session.testingEngine().players().at(1);
    source->addCard(card("aoe", CardType::BarbarianInvasion)); responder->addCard(card("x", CardType::Peach)); responder->addCard(card("y", CardType::Dodge));
    expect(f.session.submitAction(f.a, PlayCardAction {1, "aoe", {}}).accepted, "AOE starts for request binding test"); passNullificationWindows(f);
    const auto response = *f.session.testingEngine().pendingResponse();
    expect(!f.session.submitAction(f.b, RespondVirtualSlashAction {2, response.requestId + 1, {"x", "y"}}).accepted, "stale response request is rejected");
    expect(responder->handCards().size() == 2, "stale response cannot consume private materials");
    const auto owner = f.session.viewFor(f.b); const auto observer = f.session.viewFor(f.c);
    expect(owner.ownHand.size() == 2 && observer.ownHand.empty(), "only owner receives hand-card identities for Serpent Spear selection");
    expect(observer.response && observer.response->selectableCards.empty(), "observer receives no response material choices");
    expect(owner.serpentSpearSlashTargets.empty(), "response state never exposes a stale active-play virtual target list");
}
} // namespace

int main()
{
    testActiveValidationAndConsumption();
    testEquipmentRemovalAndSlashResponses();
    testRequestBindingAndPrivateView();
    std::cout << "BasicSanguoshaZhangbaSpearTests PASS\n";
}
