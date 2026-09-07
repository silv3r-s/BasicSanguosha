#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cards/Card.h"
#include "session/GameSession.h"

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

std::shared_ptr<Card> card(const std::string& id, CardType type)
{
    return std::make_shared<Card>(id, "test", type, Suit::Heart, 7);
}
std::shared_ptr<Card> equipment(const std::string& id, const std::string& name, EquipmentSlot slot)
{ const auto type = slot == EquipmentSlot::Weapon ? CardType::Weapon : CardType::OffensiveHorse; return std::make_shared<Card>(id, name, type, Suit::Spade, 1, EquipmentData {slot, 3}); }

void clearHand(const std::shared_ptr<Player>& player)
{
    std::vector<CardId> ids;
    for (const auto& item : player->handCards()) ids.push_back(item->id());
    for (const auto& id : ids) player->removeCard(id);
}

struct SessionFixture {
    GameSession session {{"A", "B", "C"}};
    SessionClientId a {session.addClient(1)};
    SessionClientId b {session.addClient(2)};
    SessionClientId c {session.addClient(3)};

    SessionFixture()
    {
        for (const auto& player : session.testingEngine().players()) clearHand(player);
    }
};

const CardView& findCard(const PlayerViewState& view, const CardId& id)
{
    for (const auto& item : view.ownHand) if (item.id == id) return item;
    fail("expected own-hand card in authoritative view");
}
void passResponse(SessionFixture& fixture, SessionClientId client, PlayerId player);
void passNullificationChain(SessionFixture& fixture)
{
    while (const auto& request = fixture.session.testingEngine().pendingResponse()) {
        if (request->type != ResponseType::Nullification) return;
        const auto player = request->responder;
        expect(fixture.session.submitAction(SessionClientId(player), RespondAction {player, request->requestId, std::nullopt}).accepted,
               "authoritative Nullification pass must be accepted");
    }
}

void testTargetConstraintsAreAuthoritative()
{
    {
        SessionFixture fixture;
        fixture.session.testingEngine().players().at(0)->addCard(card("chain", CardType::IronChain));
        const auto view = fixture.session.viewFor(fixture.a);
        const auto& chain = findCard(view, "chain");
        expect(chain.minTargets == 0 && chain.maxTargets == 2, "Iron Chain view must expose 0..2 authoritative targets");
        expect(fixture.session.canPlayCardOnTarget(fixture.a, "chain", 1), "Iron Chain may target self");
        expect(fixture.session.canPlayCardOnTarget(fixture.a, "chain", 2), "Iron Chain may target another living player");
    }
    {
        SessionFixture fixture;
        fixture.session.testingEngine().players().at(0)->addCard(card("fire", CardType::FireAttack));
        fixture.session.testingEngine().players().at(1)->addCard(card("target-hand", CardType::Dodge));
        const auto view = fixture.session.viewFor(fixture.a);
        const auto& fire = findCard(view, "fire");
        expect(fire.minTargets == 1 && fire.maxTargets == 1, "Fire Attack view must expose exactly one target");
        expect(fixture.session.canPlayCardOnTarget(fixture.a, "fire", 2), "Fire Attack target with a hand card must be offered");
        expect(!fixture.session.canPlayCardOnTarget(fixture.a, "fire", 1), "Fire Attack must not offer self");
    }
    {
        SessionFixture fixture;
        fixture.session.testingEngine().players().at(0)->addCard(card("indulgence", CardType::Indulgence));
        const auto view = fixture.session.viewFor(fixture.a);
        const auto& indulgence = findCard(view, "indulgence");
        expect(indulgence.minTargets == 1 && indulgence.maxTargets == 1, "delayed trick view must expose exactly one target");
        expect(fixture.session.canPlayCardOnTarget(fixture.a, "indulgence", 2), "delayed trick must offer a legal target");
    }
}

void testResponsePromptsAndPrivacy()
{
    {
        SessionFixture fixture;
        fixture.session.testingEngine().players().at(0)->addCard(card("barbarian", CardType::BarbarianInvasion));
        expect(fixture.session.submitAction(fixture.a, PlayCardAction {1, "barbarian", {}}).accepted, "Barbarian Invasion play must succeed");
        passNullificationChain(fixture);
        const auto responder = fixture.session.viewFor(fixture.b); const auto observer = fixture.session.viewFor(fixture.c);
        expect(responder.response && responder.response->isResponder && responder.response->prompt.find("南蛮入侵") != std::string::npos, "AOE Slash response follows Nullification window");
        expect(observer.response && !observer.response->isResponder && observer.response->selectableCards.empty(), "non-responder must not receive private response cards");
    }
    {
        SessionFixture fixture;
        fixture.session.testingEngine().players().at(0)->addCard(card("duel", CardType::Duel));
        fixture.session.testingEngine().players().at(1)->addCard(card("duel-normal", CardType::Slash));
        fixture.session.testingEngine().players().at(1)->addCard(card("duel-fire", CardType::FireSlash));
        fixture.session.testingEngine().players().at(1)->addCard(card("duel-thunder", CardType::ThunderSlash));
        expect(fixture.session.submitAction(fixture.a, PlayCardAction {1, "duel", {2}}).accepted, "Duel play must succeed");
        passNullificationChain(fixture);
        const auto responder = fixture.session.viewFor(fixture.b);
        expect(responder.response && responder.response->isResponder, "Duel target must receive a response");
        expect(responder.response->prompt.find("决斗") != std::string::npos, "Duel responder prompt must name Duel");
        const auto hasCard = [&] (const CardId& id) { return std::any_of(responder.response->selectableCards.begin(), responder.response->selectableCards.end(), [&] (const auto& card) { return card.id == id; }); };
        expect(hasCard("duel-normal") && hasCard("duel-fire") && hasCard("duel-thunder"), "Duel UI receives every Slash-family response card");
        expect(fixture.session.submitAction(fixture.b, RespondAction {2, responder.response->requestId, CardId("duel-fire")}).accepted,
               "Duel UI may submit Fire Slash as a response");
    }
}

void passResponse(SessionFixture& fixture, SessionClientId client, PlayerId player)
{
    const auto response = fixture.session.viewFor(client).response;
    expect(response && response->isResponder, "expected the requested client to own the response");
    expect(fixture.session.submitAction(client, RespondAction {player, response->requestId, std::nullopt}).accepted,
           "response pass must be accepted");
}

void testNullificationAndFireAttackInteractionState()
{
    {
        SessionFixture fixture;
        fixture.session.testingEngine().players().at(0)->addCard(card("ex", CardType::ExNihilo));
        fixture.session.testingEngine().players().at(1)->addCard(card("p2-null", CardType::Nullification));
        expect(fixture.session.submitAction(fixture.a, PlayCardAction {1, "ex", {}}).accepted, "Ex Nihilo play must succeed");
        const auto view = fixture.session.viewFor(fixture.b);
        expect(view.response && view.response->prompt.find("无懈可击") != std::string::npos,
               "Nullification request must provide a clear prompt");
        expect(view.nullification && view.nullification->sourceId == 1 && view.nullification->trickType == CardType::ExNihilo
                   && !view.nullification->targetId && view.nullification->chainRound == 1,
               "Nullification UI state must expose the authoritative trick context");
    }
    {
        SessionFixture fixture;
        fixture.session.testingEngine().players().at(0)->addCard(card("ex-no-null", CardType::ExNihilo));
        expect(fixture.session.submitAction(fixture.a, PlayCardAction {1, "ex-no-null", {}}).accepted, "Ex Nihilo without Nullification must succeed");
        const auto view = fixture.session.viewFor(fixture.b);
        expect(fixture.session.testingEngine().pendingResponse() && view.response && view.response->type == ResponseType::Nullification
                   && view.response->isResponder && view.response->selectableCards.empty() && view.nullification,
               "no legal Nullification card receives a private empty-card response window");
    }
    {
        SessionFixture fixture;
        fixture.session.testingEngine().players().at(0)->addCard(card("fire", CardType::FireAttack));
        fixture.session.testingEngine().players().at(0)->addCard(card("same-suit", CardType::Dodge));
        fixture.session.testingEngine().players().at(1)->addCard(card("revealed", CardType::Slash));
        expect(fixture.session.submitAction(fixture.a, PlayCardAction {1, "fire", {2}}).accepted, "Fire Attack play must succeed");
        passNullificationChain(fixture);
        const auto reveal = fixture.session.viewFor(fixture.b);
        expect(reveal.cardSelection && reveal.cardSelection->purpose == CardSelectionPurpose::FireAttackReveal,
               "Fire Attack target must receive the reveal selection");
        expect(fixture.session.submitCardSelection(fixture.b, reveal.cardSelection->requestId, 1).accepted,
               "Fire Attack reveal must be accepted");
        const auto discard = fixture.session.viewFor(fixture.a);
        expect(discard.cardSelection && discard.cardSelection->purpose == CardSelectionPurpose::FireAttackDiscard,
               "Fire Attack source must receive the matching-suit selection");
        expect(discard.fireAttack && discard.fireAttack->revealedCard.id == "revealed",
               "only the selected Fire Attack card must become public");
    }
}
void createKirinRequest(SessionFixture& fixture)
{
    fixture.session.testingEngine().testingEquip(1, equipment("bow", "麒麟弓", EquipmentSlot::Weapon));
    fixture.session.testingEngine().testingEquip(2, equipment("mount", "赤兔", EquipmentSlot::OffensiveHorse));
    fixture.session.testingEngine().testingStartSlash(1, {2});
    const auto response = fixture.session.testingEngine().pendingResponse();
    expect(response && fixture.session.submitAction(fixture.b, RespondAction {2, response->requestId, std::nullopt}).accepted, "Kirin Slash must deal damage");
}
void kirinBowUiShowsSelectionForActor(){ SessionFixture f; createKirinRequest(f); const auto v=f.session.viewFor(f.a); expect(v.cardSelection && v.cardSelection->purpose==CardSelectionPurpose::KirinBowMount && v.cardSelection->minCount==0 && v.cardSelection->maxCount==1 && v.cardSelection->options.size()==1 && v.cardSelection->options[0].displayName=="赤兔", "actor sees authoritative Kirin selection"); }
void kirinBowUiIsNotInteractiveForNonActor(){ SessionFixture f; createKirinRequest(f); const auto v=f.session.viewFor(f.b); expect(!v.cardSelection, "non actor has no Kirin selection"); }
void kirinBowUiClearsAfterSelectionEnds(){ SessionFixture f; createKirinRequest(f); const auto request=*f.session.viewFor(f.a).cardSelection; expect(f.session.submitCardSelection(f.a,request.requestId,std::vector<SelectionOptionId>{}).accepted,"Kirin pass accepted"); expect(!f.session.viewFor(f.a).cardSelection&&!f.session.viewFor(f.b).cardSelection,"Kirin selection clears after pass"); }
void createIceSwordPrompt(SessionFixture& f){ auto& e=f.session.testingEngine(); e.testingEquip(1,equipment("ice","寒冰剑",EquipmentSlot::Weapon)); e.players().at(1)->addCard(card("ice-target",CardType::Peach)); e.testingStartSlash(1,{2}); const auto response=e.pendingResponse(); expect(response&&f.session.submitAction(f.b,RespondAction{2,response->requestId,std::nullopt}).accepted,"Ice Sword Slash must reach prompt"); }
void iceSwordUiShowsPromptForActor(){ SessionFixture f; createIceSwordPrompt(f); const auto v=f.session.viewFor(f.a); expect(v.cardSelection&&v.cardSelection->purpose==CardSelectionPurpose::IceSwordPrompt&&v.cardSelection->targetId==2&&v.cardSelection->minCount==0&&v.cardSelection->maxCount==1&&v.cardSelection->prompt.find("寒冰剑")!=std::string::npos&&v.cardSelection->options.size()==1&&v.cardSelection->options[0].displayName=="Activate Ice Sword"&&!v.cardSelection->options[0].hidden,"actor sees the authoritative Ice Sword prompt and only Activate option"); }
void iceSwordUiPromptIsNotInteractiveForNonActor(){ SessionFixture f; createIceSwordPrompt(f); const auto b=f.session.viewFor(f.b); const auto c=f.session.viewFor(f.c); expect(!b.cardSelection&&!c.cardSelection,"non-actors receive no interactive Ice Sword prompt or Activate option"); }
void iceSwordUiSwitchesFromPromptToDiscard(){ SessionFixture f; createIceSwordPrompt(f); const auto prompt=*f.session.viewFor(f.a).cardSelection; expect(f.session.submitCardSelection(f.a,prompt.requestId,1).accepted,"Ice Sword Activate is accepted"); const auto discard=*f.session.viewFor(f.a).cardSelection; expect(discard.purpose==CardSelectionPurpose::IceSwordDiscard&&discard.requestId!=prompt.requestId&&discard.targetId==2&&discard.minCount==1&&discard.maxCount==1&&discard.prompt.find("寒冰剑")!=std::string::npos&&discard.options.size()==1&&discard.options[0].displayName!="Activate Ice Sword","Ice Sword UI switches from prompt to discard selection"); }
void iceSwordUiShowsPublicEquipmentCandidate(){ SessionFixture f; createIceSwordPrompt(f); f.session.testingEngine().testingEquip(2,equipment("ice-mount","赤兔",EquipmentSlot::OffensiveHorse)); const auto prompt=*f.session.viewFor(f.a).cardSelection; expect(f.session.submitCardSelection(f.a,prompt.requestId,1).accepted,"Ice Sword Activate accepted before equipment selection"); const auto discard=*f.session.viewFor(f.a).cardSelection; const auto mount=std::find_if(discard.options.begin(),discard.options.end(),[](const auto& option){return option.equipmentSlot==EquipmentSlot::OffensiveHorse&&option.displayName=="赤兔"&&!option.hidden;}); expect(mount!=discard.options.end()&&f.session.submitCardSelection(f.a,discard.requestId,mount->optionId).accepted,"public target equipment is shown and accepted as an Ice Sword candidate"); }
void iceSwordUiKeepsTargetHandHidden(){ SessionFixture f; createIceSwordPrompt(f); f.session.testingEngine().players().at(1)->addCard(std::make_shared<Card>("ice-secret","秘密牌",CardType::Dodge,Suit::Spade,9)); const auto prompt=*f.session.viewFor(f.a).cardSelection; expect(f.session.submitCardSelection(f.a,prompt.requestId,1).accepted,"Ice Sword Activate accepted before hidden candidates"); const auto discard=*f.session.viewFor(f.a).cardSelection; int hidden=0; for(const auto& option:discard.options){if(option.hidden){++hidden;expect(option.displayName.empty(),"hidden Ice Sword hand option reveals no card name");}} expect(hidden==2,"Ice Sword exposes target hand only as hidden selectable candidates"); }
void iceSwordUiRefreshesForSecondDiscard(){ SessionFixture f; createIceSwordPrompt(f); f.session.testingEngine().players().at(1)->addCard(card("ice-other",CardType::Dodge)); const auto prompt=*f.session.viewFor(f.a).cardSelection; f.session.submitCardSelection(f.a,prompt.requestId,1); const auto first=*f.session.viewFor(f.a).cardSelection; expect(f.session.submitCardSelection(f.a,first.requestId,first.options[0].optionId).accepted,"first Ice Sword discard accepted"); const auto second=*f.session.viewFor(f.a).cardSelection; expect(second.purpose==CardSelectionPurpose::IceSwordDiscard&&second.requestId!=first.requestId&&second.targetId==2&&second.minCount==1&&second.maxCount==1&&second.options.size()==1,"second Ice Sword discard refreshes request and candidates"); }
void iceSwordUiClearsAfterDiscardEnds(){ SessionFixture f; createIceSwordPrompt(f); const auto prompt=*f.session.viewFor(f.a).cardSelection; f.session.submitCardSelection(f.a,prompt.requestId,1); const auto discard=*f.session.viewFor(f.a).cardSelection; expect(f.session.submitCardSelection(f.a,discard.requestId,discard.options[0].optionId).accepted,"final Ice Sword discard accepted"); expect(!f.session.viewFor(f.a).cardSelection&&!f.session.viewFor(f.b).cardSelection,"Ice Sword selection UI clears after discard ends"); }
void iceSwordUiClearsAfterPromptTimeout(){ SessionFixture f; createIceSwordPrompt(f); const auto old=f.session.viewFor(f.a).cardSelection->requestId; expect(f.session.handleTimeout().accepted,"Ice Sword prompt timeout accepted as Pass"); const auto v=f.session.viewFor(f.a); expect(!v.cardSelection&&f.session.testingEngine().player(2)->hp()==3&&f.session.timeoutContextKey()!="selection:"+std::to_string(old),"Ice Sword prompt timeout clears UI and old request state"); }
void createDoubleSwordPrompt(SessionFixture& f){ auto& e=f.session.testingEngine(); e.testingSetGender(1,Gender::Male); e.testingSetGender(2,Gender::Female); e.testingSetGender(3,Gender::Male); e.testingEquip(1,equipment("double","雌雄双股剑",EquipmentSlot::Weapon)); e.players().at(1)->addCard(std::make_shared<Card>("double-a","DOUBLE SECRET A",CardType::Peach,Suit::Spade,9)); e.players().at(1)->addCard(std::make_shared<Card>("double-b","DOUBLE SECRET B",CardType::Dodge,Suit::Heart,12)); e.testingStartSlash(1,{2}); }
void doubleSwordUiShowsChoiceForTarget(){ SessionFixture f; createDoubleSwordPrompt(f); const auto v=f.session.viewFor(f.b); expect(v.cardSelection&&v.cardSelection->purpose==CardSelectionPurpose::DoubleSwordDiscard&&v.cardSelection->targetId==2&&v.cardSelection->minCount==0&&v.cardSelection->maxCount==1&&v.cardSelection->prompt.find("雌雄双股剑")!=std::string::npos&&v.cardSelection->options.size()==2&&v.cardSelection->options[0].displayName=="DOUBLE SECRET A"&&!v.cardSelection->options[0].hidden,"target receives named own-hand Double Sword choices and a zero-card draw choice"); }
void doubleSwordUiIsNotInteractiveForAttacker(){ SessionFixture f; createDoubleSwordPrompt(f); const auto v=f.session.viewFor(f.a); expect(!v.cardSelection,"attacker cannot operate the target Double Sword selection"); }
void doubleSwordUiIsNotInteractiveForObserver(){ SessionFixture f; createDoubleSwordPrompt(f); const auto v=f.session.viewFor(f.c); expect(!v.cardSelection,"observer cannot operate the target Double Sword selection"); }
void doubleSwordUiOnlyShowsHandToTarget(){ SessionFixture f; createDoubleSwordPrompt(f); const auto target=f.session.viewFor(f.b); const auto attacker=f.session.viewFor(f.a); const auto observer=f.session.viewFor(f.c); expect(target.cardSelection&&target.cardSelection->options[0].displayName=="DOUBLE SECRET A"&&std::any_of(target.ownHand.begin(),target.ownHand.end(),[](const auto& c){return c.id=="double-a";})&&!attacker.cardSelection&&!observer.cardSelection,"only target receives private hand candidates and its own hand identity"); }
void doubleSwordUiClearsAfterChoice(){ SessionFixture f; createDoubleSwordPrompt(f); const auto request=*f.session.viewFor(f.b).cardSelection; expect(f.session.submitCardSelection(f.b,request.requestId,request.options.front().optionId).accepted,"target discard choice is accepted"); const auto target=f.session.viewFor(f.b); expect(!target.cardSelection&&!f.session.viewFor(f.a).cardSelection&&f.session.testingEngine().pendingResponse()&&f.session.testingEngine().pendingResponse()->responder==2,"Double Sword UI clears and Slash proceeds to Dodge"); }

void equipmentEffectsMapToEveryViewInStableOrder()
{
    SessionFixture f;
    auto& engine = f.session.testingEngine();
    engine.testingEquip(1, equipment("effect-zhuque", "朱雀羽扇", EquipmentSlot::Weapon));
    engine.testingEquip(2, std::make_shared<Card>("effect-vine", "藤甲", CardType::Armor, Suit::Club, 2, EquipmentData {EquipmentSlot::Armor, 2}));
    engine.players().at(0)->addCard(card("effect-slash", CardType::Slash));
    expect(f.session.submitAction(f.a, PlayCardAction {1, "effect-slash", {2}}).accepted, "Zhuque Slash starts for ViewState mapping");
    const auto response = engine.pendingResponse();
    expect(response && f.session.submitAction(f.b, RespondAction {2, response->requestId, std::nullopt}).accepted,
           "Vine target declines the mapped Slash");

    const auto host = f.session.viewFor(f.a);
    const auto remote = f.session.viewFor(f.b);
    expect(host.equipmentEffects.size() == 2 && remote.equipmentEffects.size() == 2,
           "all authoritative ViewStates preserve the complete public equipment-effect window");
    const auto& converted = host.equipmentEffects[0];
    const auto& increased = host.equipmentEffects[1];
    expect(converted.eventId < increased.eventId && converted.equipment == "朱雀羽扇"
               && converted.effect == EquipmentEffectTypeView::SlashConvertedToFire && converted.ownerId == 1
               && converted.targetId == 0 && converted.value == 0 && converted.relatedCard == CardType::Slash,
           "ViewState maps the full Zhuque event without generating a new ID");
    expect(increased.equipment == "藤甲" && increased.effect == EquipmentEffectTypeView::FireDamageIncreased
               && increased.ownerId == 2 && increased.targetId == 2 && increased.value == 1
               && increased.relatedCard == CardType::FireSlash && remote.equipmentEffects[0].eventId == converted.eventId
               && remote.equipmentEffects[1].eventId == increased.eventId && remote.equipmentEffects[1].equipment == "藤甲",
           "ViewState preserves the ordered Vine event identically for another player");
}

} // namespace

int main()
{
    testTargetConstraintsAreAuthoritative();
    testResponsePromptsAndPrivacy();
    testNullificationAndFireAttackInteractionState();
    kirinBowUiShowsSelectionForActor(); kirinBowUiIsNotInteractiveForNonActor(); kirinBowUiClearsAfterSelectionEnds();
    iceSwordUiShowsPromptForActor(); iceSwordUiPromptIsNotInteractiveForNonActor(); iceSwordUiSwitchesFromPromptToDiscard(); iceSwordUiShowsPublicEquipmentCandidate(); iceSwordUiKeepsTargetHandHidden(); iceSwordUiRefreshesForSecondDiscard(); iceSwordUiClearsAfterDiscardEnds(); iceSwordUiClearsAfterPromptTimeout();
    doubleSwordUiShowsChoiceForTarget(); doubleSwordUiIsNotInteractiveForAttacker(); doubleSwordUiIsNotInteractiveForObserver(); doubleSwordUiOnlyShowsHandToTarget(); doubleSwordUiClearsAfterChoice();
    equipmentEffectsMapToEveryViewInStableOrder();
    std::cout << "BasicSanguoshaComplexUiTests PASS\n";
    return 0;
}
