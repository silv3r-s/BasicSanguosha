#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cards/Card.h"
#include "core/GameEngine.h"

using namespace sanguosha;
namespace {
[[noreturn]] void fail(const std::string& m) { std::cerr << "FAIL: " << m << '\n'; std::exit(1); }
void expect(bool v, const std::string& m) { if (!v) fail(m); }
std::shared_ptr<Card> card(const std::string& id, CardType type = CardType::Dodge) { return std::make_shared<Card>(id, "test", type, Suit::Heart, 7); }
std::shared_ptr<Card> equip(const std::string& id, const std::string& name, EquipmentSlot slot) { const auto type = slot == EquipmentSlot::Weapon ? CardType::Weapon : slot == EquipmentSlot::Armor ? CardType::Armor : slot == EquipmentSlot::OffensiveHorse ? CardType::OffensiveHorse : CardType::DefensiveHorse; return std::make_shared<Card>(id, name, type, Suit::Spade, 1, EquipmentData {slot, 3}); }
void clear(const std::shared_ptr<Player>& p) { std::vector<CardId> ids; for (const auto& c : p->handCards()) ids.push_back(c->id()); for (const auto& id : ids) p->removeCard(id); }
GameEngine game() { GameEngine g; g.createGame({"A", "B", "C"}); g.startGame(); for (const auto& p : g.players()) clear(p); g.testingEquip(1, equip("axe", "贯石斧", EquipmentSlot::Weapon)); return g; }
void realDodge(GameEngine& g) { const auto r = g.pendingResponse(); expect(r && r->type == ResponseType::Dodge, "Dodge request required"); const auto id = "d" + std::to_string(r->requestId); g.players().at(size_t(r->responder - 1))->addCard(card(id)); expect(g.submitAction(RespondAction {r->responder, r->requestId, id}).accepted, "real Dodge accepted"); }
const CardSelectionRequest& axeRequest(GameEngine& g) { expect(g.axeContext().has_value(), "Axe context required"); expect(g.pendingCardSelection() && g.pendingCardSelection()->purpose == CardSelectionPurpose::AxeDiscard, "Axe selection required"); return *g.pendingCardSelection(); }
void startDodged(GameEngine& g, DamageNature n = DamageNature::Normal, int amount = 1, bool ignore = false) { g.testingStartSlash(1, {2}, amount, n, ignore); realDodge(g); }

void testCostsAndAtomicity()
{
    auto g = game(); g.players()[0]->addCard(card("a")); g.players()[0]->addCard(card("b")); startDodged(g); const auto request = axeRequest(g); const auto before = g.deck().discardPileSize();
    expect(!g.submitAction(SelectCardsAction {1, request.requestId, {"a"}}).accepted, "one cost rejected");
    expect(!g.submitAction(SelectCardsAction {1, request.requestId, {"a", "foreign"}}).accepted, "mixed invalid costs rejected atomically");
    expect(g.players()[0]->handCards().size() == 2 && g.deck().discardPileSize() == before, "invalid cost consumes nothing");
    expect(g.submitAction(SelectCardsAction {1, request.requestId, {"a", "b"}}).accepted, "two hand cards accepted");
    expect(g.player(2)->hp() == 3 && !g.axeContext() && !g.testingDeferredSlashDamage(), "resume damages fixed target and clears contexts");
}

void testOnlyRealDodgesTrigger()
{
    { auto g = game(); g.testingRemoveEquipment(1, EquipmentSlot::Weapon); g.testingStartSlash(1, {2}); realDodge(g); expect(!g.axeContext() && !g.testingDeferredSlashDamage(), "no Axe means no retained post-Dodge request"); }
    { auto g = game(); g.testingStartSlash(1, {2}); const auto r = g.pendingResponse(); expect(g.submitAction(RespondAction {2, r->requestId, std::nullopt}).accepted, "Dodge pass accepted"); expect(!g.axeContext(), "an unanswered Slash must not trigger Axe"); }
}

void testEquipmentAndAxeSelfCost()
{
    auto g = game(); g.players()[0]->addCard(card("h")); startDodged(g); const auto request = axeRequest(g);
    expect(g.submitAction(SelectCardsAction {1, request.requestId, {"axe", "h"}}).accepted, "Axe itself plus a hand card is a valid cost");
    expect(!g.player(1)->equipment(EquipmentSlot::Weapon) && g.player(2)->hp() == 3, "discarded Axe still completes the locked effect");
}

void testPassTimeoutAndLifecycle()
{
    { auto g = game(); g.players()[0]->addCard(card("a")); g.players()[0]->addCard(card("b")); startDodged(g); const auto before = g.deck().discardPileSize(); const auto request = axeRequest(g); expect(g.submitAction(SelectCardsAction {1, request.requestId, {}}).accepted, "Pass accepted"); expect(g.player(2)->hp() == 4 && g.deck().discardPileSize() == before && !g.axeContext(), "Pass consumes nothing"); }
    { auto g = game(); g.players()[0]->addCard(card("a")); g.players()[0]->addCard(card("b")); startDodged(g); const auto before = g.deck().discardPileSize(); expect(g.handleTimeout().accepted, "Axe timeout handled"); expect(g.player(2)->hp() == 4 && g.deck().discardPileSize() == before && !g.axeContext(), "timeout equals Pass"); }
    { auto g = game(); startDodged(g); g.testingRemoveEquipment(1, EquipmentSlot::Weapon); expect(!g.axeContext() && !g.testingDeferredSlashDamage(), "external Axe removal clears pending request"); }
    { auto g = game(); startDodged(g); g.testingKillPlayer(2); expect(!g.axeContext() && !g.testingDeferredSlashDamage(), "target death clears Axe request"); }
}

void testDamageProperties()
{
    { auto g = game(); g.players()[0]->addCard(card("a")); g.players()[0]->addCard(card("b")); g.testingEquip(2, equip("vine", "藤甲", EquipmentSlot::Armor)); startDodged(g, DamageNature::Fire); const auto r = axeRequest(g); expect(g.submitAction(SelectCardsAction {1, r.requestId, {"a", "b"}}).accepted && g.player(2)->hp() == 2, "Fire retains Vine +1 exactly once"); }
    { auto g = game(); g.players()[0]->addCard(card("a")); g.players()[0]->addCard(card("b")); g.testingEquip(2, equip("vine", "藤甲", EquipmentSlot::Armor)); startDodged(g, DamageNature::Thunder); const auto r = axeRequest(g); expect(g.submitAction(SelectCardsAction {1, r.requestId, {"a", "b"}}).accepted && g.player(2)->hp() == 3, "Thunder remains unamplified by Vine"); }
    { auto g = game(); g.players()[0]->addCard(card("a")); g.players()[0]->addCard(card("b")); startDodged(g, DamageNature::Fire, 2, true); const auto r = axeRequest(g); expect(g.submitAction(SelectCardsAction {1, r.requestId, {"a", "b"}}).accepted && g.player(2)->hp() == 2, "base damage and ignoreArmor survive resume"); }
}
}
int main() { testOnlyRealDodgesTrigger(); testCostsAndAtomicity(); testEquipmentAndAxeSelfCost(); testPassTimeoutAndLifecycle(); testDamageProperties(); std::cout << "BasicSanguoshaAxeTests PASS\n"; }
