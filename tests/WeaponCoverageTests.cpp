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
std::shared_ptr<Card> card(std::string id, CardType type, Suit suit = Suit::Spade) { return std::make_shared<Card>(std::move(id), "test", type, suit, 7); }
std::shared_ptr<Card> weapon(std::string id, std::string name, int range, bool unlimited = false) { return std::make_shared<Card>(std::move(id), std::move(name), CardType::Weapon, Suit::Spade, 1, EquipmentData {EquipmentSlot::Weapon, range, unlimited}); }
void clear(const std::shared_ptr<Player>& p) { std::vector<CardId> ids; for (const auto& c:p->handCards()) ids.push_back(c->id()); for (const auto& id:ids) p->removeCard(id); }
struct F { GameSession s{{"A","B"}}; SessionClientId a{s.addClient(1)}, b{s.addClient(2)}; F(){for(const auto&p:s.testingEngine().players())clear(p);} };
void passDodge(F& f) { const auto r=*f.s.testingEngine().pendingResponse(); expect(f.s.submitAction(f.b,RespondAction{2,r.requestId,std::nullopt}).accepted,"Dodge pass accepted"); }
void testRepeatingCrossbow()
{
    { F f; auto a=f.s.testingEngine().players().at(0); a->addCard(card("s1",CardType::Slash)); a->addCard(card("s2",CardType::FireSlash)); expect(f.s.submitAction(f.a,PlayCardAction{1,"s1",{2}}).accepted,"first Slash accepted"); passDodge(f); expect(!f.s.submitAction(f.a,PlayCardAction{1,"s2",{2}}).accepted,"second Slash rejected without Crossbow"); }
    { F f; auto a=f.s.testingEngine().players().at(0); f.s.testingEngine().testingEquip(1,weapon("crossbow","诸葛连弩",1,true)); a->addCard(card("s1",CardType::Slash)); a->addCard(card("s2",CardType::FireSlash)); a->addCard(card("s3",CardType::ThunderSlash)); for(const auto& id:{"s1","s2","s3"}) { expect(f.s.submitAction(f.a,PlayCardAction{1,id,{2}}).accepted,"Crossbow permits repeated Slash"); passDodge(f); } f.s.testingEngine().testingEquip(1,weapon("other","青釭剑",2)); a->addCard(card("s4",CardType::Slash)); expect(!f.s.submitAction(f.a,PlayCardAction{1,"s4",{2}}).accepted,"replacing Crossbow restores existing Slash limit"); }
}
void testQinggangScope()
{
    F f; auto a=f.s.testingEngine().players().at(0); auto b=f.s.testingEngine().players().at(1); f.s.testingEngine().testingEquip(1,weapon("qinggang","青釭剑",2)); f.s.testingEngine().testingEquip(2,std::make_shared<Card>("vine","藤甲",CardType::Armor,Suit::Spade,2,EquipmentData{EquipmentSlot::Armor,0})); a->addCard(card("black",CardType::Slash,Suit::Spade)); expect(f.s.submitAction(f.a,PlayCardAction{1,"black",{2}}).accepted,"Qinggang Slash accepted"); expect(f.s.testingEngine().pendingResponse() && f.s.testingEngine().pendingResponse()->type==ResponseType::Dodge,"Qinggang bypasses Vine Slash immunity rather than ending Slash"); passDodge(f); }
}
void testQinglongFollowUp()
{
    F f; auto a=f.s.testingEngine().players().at(0); auto b=f.s.testingEngine().players().at(1);
    f.s.testingEngine().testingEquip(1, weapon("dragon", "青龙偃月刀", 3));
    a->addCard(card("s1", CardType::Slash)); a->addCard(card("s2", CardType::FireSlash)); a->addCard(card("s3", CardType::ThunderSlash)); b->addCard(card("d1", CardType::Dodge)); b->addCard(card("d2", CardType::Dodge));
    expect(f.s.submitAction(f.a, PlayCardAction {1, "s1", {2}}).accepted, "initial Slash accepted");
    auto request=*f.s.testingEngine().pendingResponse(); expect(f.s.submitAction(f.b, RespondAction {2, request.requestId, "d1"}).accepted, "Dodge accepted");
    request=*f.s.testingEngine().pendingResponse(); expect(request.type==ResponseType::QinglongSlash && request.responder==1, "Qinglong request belongs to original attacker");
    expect(f.s.submitAction(f.a, RespondAction {1, request.requestId, "s2"}).accepted, "Fire Slash follow-up accepted despite normal Slash limit");
    request=*f.s.testingEngine().pendingResponse(); expect(request.type==ResponseType::Dodge && request.responder==2, "follow-up is locked to original target and enters normal Dodge flow");
    expect(f.s.submitAction(f.b, RespondAction {2, request.requestId, "d2"}).accepted, "second Dodge accepted");
    expect(f.s.testingEngine().pendingResponse() && f.s.testingEngine().pendingResponse()->type==ResponseType::QinglongSlash, "second Dodge creates a fresh Qinglong request");
    const auto stale = *f.s.testingEngine().pendingResponse(); const auto remaining = a->handCards().size();
    f.s.testingEngine().testingKillPlayer(2);
    expect(!f.s.testingEngine().pendingResponse() && !f.s.testingEngine().qinglongContext(), "target death clears Qinglong request and context");
    expect(!f.s.submitAction(f.a, RespondAction {1, stale.requestId, "s3"}).accepted && a->handCards().size() == remaining, "stale Qinglong request after death cannot consume Slash");
}
int main() { testRepeatingCrossbow(); testQinggangScope(); testQinglongFollowUp(); std::cout << "BasicSanguoshaWeaponCoverageTests PASS\n"; }
