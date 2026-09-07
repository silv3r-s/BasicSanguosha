#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cards/Card.h"
#include "session/GameSession.h"

using namespace sanguosha;
namespace {
[[noreturn]] void fail(const std::string& m) { std::cerr << "FAIL: " << m << '\n'; std::exit(1); }
void expect(bool v, const std::string& m) { if (!v) fail(m); }
std::shared_ptr<Card> card(const std::string& id, CardType type, const std::string& name = "test") { return std::make_shared<Card>(id, name, type, Suit::Heart, 7); }
void clearHand(const std::shared_ptr<Player>& p) { std::vector<CardId> ids; for (const auto& c : p->handCards()) ids.push_back(c->id()); for (const auto& id : ids) p->removeCard(id); }
struct Fixture { GameSession session {{"A", "B", "C", "D"}}; SessionClientId a {session.addClient(1)}, b {session.addClient(2)}, c {session.addClient(3)}, d {session.addClient(4)}; Fixture() { for (const auto& p : session.testingEngine().players()) clearHand(p); } };
SessionClientId client(PlayerId id) { return SessionClientId(id); }
void addNullifications(Fixture& f) { for (const auto& p : f.session.testingEngine().players()) for (int i = 0; i < 4; ++i) p->addCard(card("n" + std::to_string(p->id()) + "-" + std::to_string(i), CardType::Nullification)); }
std::optional<CardId> firstNullification(const std::shared_ptr<Player>& p) { for (const auto& c : p->handCards()) if (c->type() == CardType::Nullification) return c->id(); return {}; }
void driveNullifications(Fixture& f, int plays)
{
    int made = 0;
    for (int guard = 0; guard < 80 && f.session.testingEngine().pendingResponse(); ++guard) {
        const auto request = *f.session.testingEngine().pendingResponse();
        if (request.type != ResponseType::Nullification) break;
        std::optional<CardId> selected;
        if (made < plays) { selected = firstNullification(f.session.testingEngine().players().at(request.responder - 1)); expect(selected.has_value(), "scheduled responder has Nullification"); ++made; }
        expect(f.session.submitAction(client(request.responder), RespondAction {request.responder, request.requestId, selected}).accepted, "Nullification response or pass is accepted");
    }
    expect(made == plays, "requested number of Nullifications was played");
}

void testParityAndContinuation()
{
    for (int count = 0; count <= 4; ++count) {
        Fixture f; addNullifications(f); auto source = f.session.testingEngine().players().at(0); auto target = f.session.testingEngine().players().at(1);
        source->addCard(card("dismantle", CardType::Dismantlement)); target->addCard(card("target-card", CardType::Dodge));
        expect(f.session.submitAction(f.a, PlayCardAction {1, "dismantle", {2}}).accepted, "Dismantlement opens a Nullification chain");
        driveNullifications(f, count);
        const bool resolves = count % 2 == 0;
        expect(bool(f.session.testingEngine().pendingCardSelection()) == resolves, "Nullification parity resumes or cancels Dismantlement correctly");
        expect(!f.session.testingEngine().nullificationChain() && !f.session.testingEngine().trickResolution(), "chain context clears at continuation boundary");
    }
}

void testTrickCoverage()
{
    { Fixture f; addNullifications(f); auto a=f.session.testingEngine().players().at(0); auto b=f.session.testingEngine().players().at(1); a->addCard(card("snatch", CardType::Snatch)); b->addCard(card("x", CardType::Dodge)); expect(f.session.submitAction(f.a, PlayCardAction{1,"snatch",{2}}).accepted,"Snatch starts chain"); driveNullifications(f,2); expect(f.session.testingEngine().pendingCardSelection() && f.session.testingEngine().pendingCardSelection()->purpose==CardSelectionPurpose::Snatch,"Snatch resumes hidden selection after even chain"); }
    { Fixture f; addNullifications(f); auto a=f.session.testingEngine().players().at(0); a->addCard(card("duel", CardType::Duel)); expect(f.session.submitAction(f.a, PlayCardAction{1,"duel",{2}}).accepted,"Duel starts chain"); driveNullifications(f,2); expect(f.session.testingEngine().pendingResponse() && f.session.testingEngine().pendingResponse()->type==ResponseType::Slash,"Duel resumes Slash response"); }
    { Fixture f; addNullifications(f); auto a=f.session.testingEngine().players().at(0); a->addCard(card("aoe", CardType::BarbarianInvasion)); expect(f.session.submitAction(f.a, PlayCardAction{1,"aoe",{}}).accepted,"AOE starts chain"); driveNullifications(f,0); const auto& response=f.session.testingEngine().pendingResponse(); expect(response && response->type==ResponseType::Slash, response ? "AOE response type is not Slash" : "AOE did not create a response"); }
    { Fixture f; addNullifications(f); auto a=f.session.testingEngine().players().at(0); a->addCard(card("harvest", CardType::Harvest)); expect(f.session.submitAction(f.a, PlayCardAction{1,"harvest",{}}).accepted,"Harvest starts chain"); driveNullifications(f,0); expect(f.session.testingEngine().pendingCardSelection() && f.session.testingEngine().pendingCardSelection()->purpose==CardSelectionPurpose::Harvest,"Harvest resumes selection pool"); }
    { Fixture f; addNullifications(f); auto a=f.session.testingEngine().players().at(0); a->addCard(card("borrow", CardType::BorrowedSword)); f.session.testingEngine().testingEquip(2, std::make_shared<Card>("weapon","丈八蛇矛",CardType::Weapon,Suit::Spade,12,EquipmentData{EquipmentSlot::Weapon,3})); expect(f.session.submitAction(f.a, PlayCardAction{1,"borrow",{2,3}}).accepted,"Borrowed Sword starts chain"); driveNullifications(f,2); expect(f.session.testingEngine().pendingResponse() && f.session.testingEngine().pendingResponse()->type==ResponseType::BorrowedSwordSlash,"Borrowed Sword resumes forced Slash"); }
    { Fixture f; addNullifications(f); auto a=f.session.testingEngine().players().at(0); auto b=f.session.testingEngine().players().at(1); a->addCard(card("fire", CardType::FireAttack)); b->addCard(card("shown",CardType::Dodge)); expect(f.session.submitAction(f.a, PlayCardAction{1,"fire",{2}}).accepted,"Fire Attack starts chain"); driveNullifications(f,2); expect(f.session.testingEngine().pendingCardSelection() && f.session.testingEngine().pendingCardSelection()->purpose==CardSelectionPurpose::FireAttackReveal,"Fire Attack resumes reveal selection"); }
    { Fixture f; addNullifications(f); auto a=f.session.testingEngine().players().at(0); a->addCard(card("chain",CardType::IronChain)); expect(f.session.submitAction(f.a, PlayCardAction{1,"chain",{2}}).accepted,"Iron Chain starts chain"); driveNullifications(f,0); expect(f.session.testingEngine().players().at(1)->isChained(),"Iron Chain changes state only after chain resolves"); }
}

void testDelayedAndRequestSafety()
{
    { Fixture f; auto a=f.session.testingEngine().players().at(0); a->addCard(card("indulgence",CardType::Indulgence)); expect(f.session.submitAction(f.a,PlayCardAction{1,"indulgence",{2}}).accepted,"delayed trick enters judgment zone immediately"); expect(f.session.testingEngine().players().at(1)->hasJudgmentCard(CardType::Indulgence),"delayed trick has no immediate Nullification window"); expect(!f.session.testingEngine().pendingResponse(),"no response exists until Judgment phase"); }
    Fixture f; addNullifications(f); auto a=f.session.testingEngine().players().at(0); a->addCard(card("d",CardType::Dismantlement)); f.session.testingEngine().players().at(1)->addCard(card("x",CardType::Dodge)); expect(f.session.submitAction(f.a,PlayCardAction{1,"d",{2}}).accepted,"request test starts"); const auto old=*f.session.testingEngine().pendingResponse(); expect(f.session.submitAction(client(old.responder),RespondAction{old.responder,old.requestId,std::nullopt}).accepted,"first pass accepted"); const auto current=*f.session.testingEngine().pendingResponse(); const auto null=firstNullification(f.session.testingEngine().players().at(old.responder-1)); const auto before=f.session.testingEngine().players().at(old.responder-1)->handCards().size(); expect(!f.session.submitAction(client(old.responder),RespondAction{old.responder,old.requestId,null}).accepted,"old request is rejected"); expect(f.session.testingEngine().players().at(old.responder-1)->handCards().size()==before && f.session.testingEngine().pendingResponse()->requestId==current.requestId,"old request does not consume or alter chain");
}

void testScopedNullificationRules()
{
    { // Every eligible player receives the same window; empty private options mean Pass only.
        Fixture f; auto a=f.session.testingEngine().players().at(0); auto b=f.session.testingEngine().players().at(1); auto c=f.session.testingEngine().players().at(2);
        a->addCard(card("skip-dismantle", CardType::Dismantlement)); c->addCard(card("skip-null", CardType::Nullification)); b->addCard(card("skip-target", CardType::Dodge));
        expect(f.session.submitAction(f.a, PlayCardAction {1, "skip-dismantle", {2}}).accepted, "trick starts");
        const auto first = *f.session.testingEngine().pendingResponse(); expect(first.responder == 2, "player without Nullification receives a uniform response window");
        expect(f.session.submitAction(f.b, RespondAction {2, first.requestId, std::nullopt}).accepted, "empty Nullification hand passes legally");
        const auto request = *f.session.testingEngine().pendingResponse(); expect(request.responder == 3, "next eligible responder receives the following window");
    }
    { // The original trick user does not receive the first window, but does receive a later counter window.
        Fixture f; auto a=f.session.testingEngine().players().at(0); auto b=f.session.testingEngine().players().at(1); auto c=f.session.testingEngine().players().at(2);
        a->addCard(card("origin-dismantle", CardType::Dismantlement)); a->addCard(card("origin-null", CardType::Nullification)); b->addCard(card("first-null", CardType::Nullification)); c->addCard(card("origin-target", CardType::Dodge));
        expect(f.session.submitAction(f.a, PlayCardAction {1, "origin-dismantle", {3}}).accepted, "trick starts");
        auto first=*f.session.testingEngine().pendingResponse(); expect(first.responder == 2, "original trick user cannot Nullify their own trick initially");
        expect(f.session.submitAction(f.b, RespondAction {2, first.requestId, CardId("first-null")}).accepted, "first Nullification is accepted");
        auto later=*f.session.testingEngine().pendingResponse(); expect(later.responder == 3, "later chain asks the next eligible player even without a card");
        expect(f.session.submitAction(f.c, RespondAction {3, later.requestId, std::nullopt}).accepted, "intermediate empty-hand pass accepted");
        later=*f.session.testingEngine().pendingResponse();
        if (later.responder != 1) expect(f.session.submitAction(client(later.responder), RespondAction {later.responder, later.requestId, std::nullopt}).accepted, "additional uniform pass advances counter window");
        later=*f.session.testingEngine().pendingResponse(); expect(later.responder == 1, "original user joins later Nullification chain");
    }
}
} // namespace
int main() { testParityAndContinuation(); testTrickCoverage(); testDelayedAndRequestSafety(); testScopedNullificationRules(); std::cout << "BasicSanguoshaNullificationCoverageTests PASS\n"; }
