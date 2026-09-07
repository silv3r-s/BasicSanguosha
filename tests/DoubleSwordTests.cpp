#include <cstdlib>
#include <iostream>
#include <memory>

#include "core/GameEngine.h"

using namespace sanguosha;

namespace {

[[noreturn]] void fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void expect(bool value, const char* message)
{
    if (!value) fail(message);
}

std::shared_ptr<Card> doubleSword()
{
    return std::make_shared<Card>("double-sword", "雌雄双股剑", CardType::Weapon, Suit::Spade, 2,
                                  EquipmentData {EquipmentSlot::Weapon, 2});
}

std::shared_ptr<Card> handCard(const CardId& id)
{
    return std::make_shared<Card>(id, id, CardType::Peach, Suit::Heart, 1);
}

void clearHands(GameEngine& engine)
{
    for (const auto& player : engine.players()) {
        std::vector<CardId> ids;
        for (const auto& card : player->handCards()) ids.push_back(card->id());
        for (const auto& id : ids) player->removeCard(id);
    }
}

GameEngine setup(Gender sourceGender = Gender::Male, Gender targetGender = Gender::Female, bool targetHasHand = true)
{
    GameEngine engine;
    engine.createGame({"P1", "P2"});
    engine.startGame();
    clearHands(engine);
    engine.testingSetGender(1, sourceGender);
    engine.testingSetGender(2, targetGender);
    engine.testingEquip(1, doubleSword());
    if (targetHasHand) engine.players()[1]->addCard(handCard("target-hand"));
    return engine;
}

void doubleSwordTriggersAgainstOppositeGender()
{
    auto engine = setup();
    engine.testingStartSlash(1, {2});
    const auto& request = engine.pendingCardSelection();
    const auto& context = engine.doubleSwordContext();
    expect(request && context && request->purpose == CardSelectionPurpose::DoubleSwordDiscard
               && request->requester == 2 && request->target == 2 && request->minCount == 0 && request->maxCount == 1
               && context->source == 1 && context->target == 2 && context->requestId == request->requestId
               && !engine.pendingResponse(),
           "opposite known genders create the Double Sword choice before Dodge");
}

void doubleSwordDoesNotTriggerAgainstSameGender()
{
    auto engine = setup(Gender::Male, Gender::Male);
    engine.testingStartSlash(1, {2});
    expect(!engine.pendingCardSelection() && engine.pendingResponse()
               && engine.pendingResponse()->type == ResponseType::Dodge,
           "same genders do not trigger Double Sword");
}

void doubleSwordDoesNotTriggerWithUnknownGender()
{
    auto targetUnknown = setup(Gender::Male, Gender::Unknown);
    targetUnknown.testingStartSlash(1, {2});
    expect(!targetUnknown.pendingCardSelection() && targetUnknown.pendingResponse(),
           "unknown target gender does not trigger Double Sword");

    auto sourceUnknown = setup(Gender::Unknown, Gender::Female);
    sourceUnknown.testingStartSlash(1, {2});
    expect(!sourceUnknown.pendingCardSelection() && sourceUnknown.pendingResponse(),
           "unknown source gender does not trigger Double Sword");
}

void doubleSwordTargetCanDiscardHandCard()
{
    auto engine = setup();
    engine.testingStartSlash(1, {2});
    const auto requestId = engine.pendingCardSelection()->requestId;
    const auto sourceHandBefore = engine.player(1)->handCards().size();
    expect(engine.submitAction(SelectCardsAction {2, requestId, {"target-hand"}}).accepted,
           "target may discard its own hand card for Double Sword");
    expect(engine.player(2)->handCards().empty() && engine.player(1)->handCards().size() == sourceHandBefore
               && !engine.doubleSwordContext() && !engine.pendingCardSelection()
               && engine.pendingResponse() && engine.pendingResponse()->responder == 2,
           "discard completes Double Sword and continues into Dodge without drawing");
}

void doubleSwordTargetCanLetAttackerDraw()
{
    auto engine = setup();
    engine.testingStartSlash(1, {2});
    const auto requestId = engine.pendingCardSelection()->requestId;
    const auto sourceHandBefore = engine.player(1)->handCards().size();
    expect(engine.submitAction(SelectCardsAction {2, requestId, {}}).accepted,
           "target may decline the discard for Double Sword");
    expect(engine.player(2)->handCards().size() == 1 && engine.player(1)->handCards().size() == sourceHandBefore + 1
               && !engine.doubleSwordContext() && !engine.pendingCardSelection()
               && engine.pendingResponse() && engine.pendingResponse()->responder == 2,
           "declining the discard draws one card for the attacker then continues into Dodge");
}

void doubleSwordNoHandForcesAttackerDraw()
{
    auto engine = setup(Gender::Male, Gender::Female, false);
    const auto sourceHandBefore = engine.player(1)->handCards().size();
    engine.testingStartSlash(1, {2});
    expect(!engine.pendingCardSelection() && !engine.doubleSwordContext()
               && engine.player(1)->handCards().size() == sourceHandBefore + 1
               && engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Dodge
               && engine.pendingResponse()->responder == 2,
           "a handless target immediately gives the attacker one draw and the Slash proceeds to Dodge");
}

GameEngine setupFour(const std::vector<Gender>& genders)
{
    GameEngine engine;
    engine.createGame({"P1", "P2", "P3", "P4"});
    engine.startGame();
    clearHands(engine);
    for (std::size_t index = 0; index < genders.size(); ++index) engine.testingSetGender(static_cast<PlayerId>(index + 1), genders[index]);
    engine.testingEquip(1, doubleSword());
    for (PlayerId id = 2; id <= 4; ++id) engine.players()[static_cast<std::size_t>(id - 1)]->addCard(handCard("hand-" + std::to_string(id)));
    return engine;
}

void passDodge(GameEngine& engine, PlayerId target)
{
    const auto request = engine.pendingResponse();
    expect(request && request->type == ResponseType::Dodge && request->responder == target,
           "Double Sword continuation creates the expected Dodge request");
    expect(engine.submitAction(RespondAction {target, request->requestId, std::nullopt}).accepted,
           "target may pass the continued Dodge request");
}

void doubleSwordMultiTargetResolvesEachTargetIndependently()
{
    auto engine = setupFour({Gender::Male, Gender::Female, Gender::Male, Gender::Female});
    engine.testingStartSlash(1, {2, 3, 4});
    const auto first = engine.pendingCardSelection();
    expect(first && first->requester == 2, "first opposite-gender target gets a Double Sword request");
    expect(engine.submitAction(SelectCardsAction {2, first->requestId, {}}).accepted, "first target can decline discard");
    passDodge(engine, 2);
    expect(!engine.pendingCardSelection() && engine.pendingResponse() && engine.pendingResponse()->responder == 3,
           "same-gender middle target continues directly to Dodge");
    passDodge(engine, 3);
    const auto third = engine.pendingCardSelection();
    expect(third && third->requester == 4 && third->requestId != first->requestId && engine.doubleSwordContext()
               && engine.doubleSwordContext()->target == 4,
           "later opposite-gender target receives its own independent Double Sword request");
    expect(engine.submitAction(SelectCardsAction {4, third->requestId, {}}).accepted, "third target can finish its own request");
    passDodge(engine, 4);
}

void doubleSwordMultiTargetSupportsDifferentChoices()
{
    auto engine = setupFour({Gender::Male, Gender::Female, Gender::Female, Gender::Male});
    engine.testingStartSlash(1, {2, 3});
    const auto first = engine.pendingCardSelection();
    const auto sourceBefore = engine.player(1)->handCards().size();
    expect(first && engine.submitAction(SelectCardsAction {2, first->requestId, {"hand-2"}}).accepted,
           "first target discards its own hand card");
    expect(engine.player(2)->handCards().empty() && engine.player(1)->handCards().size() == sourceBefore,
           "first choice does not draw for the attacker");
    passDodge(engine, 2);
    const auto second = engine.pendingCardSelection();
    expect(second && second->requester == 3 && second->requestId != first->requestId,
           "second target has a separate request ID");
    expect(engine.submitAction(SelectCardsAction {3, second->requestId, {}}).accepted,
           "second target lets the attacker draw");
    expect(engine.player(3)->handCards().size() == 1 && engine.player(1)->handCards().size() == sourceBefore + 1,
           "different target choice does not leak discard state");
    passDodge(engine, 3);
}

void doubleSwordTimeoutLetsAttackerDraw()
{
    auto engine = setup();
    engine.testingStartSlash(1, {2});
    const auto sourceBefore = engine.player(1)->handCards().size();
    expect(engine.handleTimeout().accepted && engine.player(1)->handCards().size() == sourceBefore + 1
               && engine.player(2)->handCards().size() == 1 && !engine.doubleSwordContext(),
           "Double Sword timeout declines the discard and draws exactly one card");
    const auto afterFirstTimeout = engine.player(1)->handCards().size();
    engine.handleTimeout();
    expect(engine.player(1)->handCards().size() == afterFirstTimeout,
           "a completed Double Sword timeout cannot draw a second card");
}

void doubleSwordRejectsStaleRequest()
{
    auto engine = setup();
    engine.testingStartSlash(1, {2});
    const auto oldRequestId = engine.pendingCardSelection()->requestId;
    expect(engine.submitAction(SelectCardsAction {2, oldRequestId, {}}).accepted, "original Double Sword request completes");
    const auto sourceAfter = engine.player(1)->handCards().size();
    expect(!engine.submitAction(SelectCardsAction {2, oldRequestId, {"target-hand"}}).accepted
               && engine.player(1)->handCards().size() == sourceAfter && !engine.doubleSwordContext()
               && engine.pendingResponse(),
           "stale Double Sword request cannot change state or restore context");
}

void doubleSwordRejectsIllegalDiscardCandidate()
{
    auto engine = setup();
    engine.players()[0]->addCard(handCard("attacker-hand"));
    engine.testingEquip(2, std::make_shared<Card>("target-armor", "armor", CardType::Armor, Suit::Club, 2,
                                                   EquipmentData {EquipmentSlot::Armor, 0}));
    engine.testingStartSlash(1, {2});
    const auto requestId = engine.pendingCardSelection()->requestId;
    engine.players()[1]->removeCard("target-hand");
    for (const auto& candidate : {CardId {"attacker-hand"}, CardId {"target-armor"}, CardId {"unknown"}, CardId {"target-hand"}}) {
        expect(!engine.submitAction(SelectCardsAction {2, requestId, {candidate}}).accepted,
               "only the target's current hand card is a legal Double Sword discard candidate");
    }
    expect(engine.pendingCardSelection() && engine.pendingCardSelection()->requestId == requestId && engine.doubleSwordContext()
               && engine.player(1)->handCards().size() == 1 && engine.player(2)->equipment(EquipmentSlot::Armor),
           "illegal candidates preserve the request and all state");
}

void doubleSwordDoesNotTriggerAfterUnequip()
{
    auto engine = setup();
    engine.testingRemoveEquipment(1, EquipmentSlot::Weapon);
    engine.testingStartSlash(1, {2});
    expect(!engine.pendingCardSelection() && engine.pendingResponse(), "unequipped Double Sword does not trigger");
}

void doubleSwordDoesNotTriggerAfterReplacement()
{
    auto engine = setup();
    engine.testingEquip(1, std::make_shared<Card>("replacement", "replacement", CardType::Weapon, Suit::Club, 2,
                                                   EquipmentData {EquipmentSlot::Weapon, 2}));
    engine.testingStartSlash(1, {2});
    expect(engine.player(1)->equipment(EquipmentSlot::Weapon)->id() == "replacement"
               && !engine.pendingCardSelection() && engine.pendingResponse(),
           "replacing Double Sword disables the trigger immediately");
}

void doubleSwordPendingRequestInvalidatesWhenWeaponIsLost()
{
    auto engine = setup();
    engine.testingStartSlash(1, {2});
    const auto oldRequestId = engine.pendingCardSelection()->requestId;
    engine.testingRemoveEquipment(1, EquipmentSlot::Weapon);
    expect(!engine.submitAction(SelectCardsAction {2, oldRequestId, {"target-hand"}}).accepted
               && !engine.doubleSwordContext() && !engine.pendingCardSelection()
               && engine.player(2)->handCards().size() == 1 && engine.player(1)->handCards().empty()
               && engine.pendingResponse() && engine.pendingResponse()->responder == 2,
           "weapon loss invalidates the pending request without drawing or discarding and safely continues Slash");
}

void doubleSwordClearsWhenAttackerDies()
{
    auto engine = setupFour({Gender::Male, Gender::Female, Gender::Male, Gender::Unknown});
    engine.testingStartSlash(1, {2});
    const auto oldRequestId = engine.pendingCardSelection()->requestId;
    engine.testingKillPlayer(1);
    expect(!engine.submitAction(SelectCardsAction {2, oldRequestId, {}}).accepted && !engine.doubleSwordContext()
               && !engine.pendingCardSelection(),
           "attacker death clears Double Sword and rejects its stale action");
}

void doubleSwordClearsWhenTargetDies()
{
    auto engine = setupFour({Gender::Male, Gender::Female, Gender::Male, Gender::Unknown});
    engine.testingStartSlash(1, {2});
    const auto oldRequestId = engine.pendingCardSelection()->requestId;
    const auto sourceHand = engine.player(1)->handCards().size();
    engine.testingKillPlayer(2);
    expect(!engine.submitAction(SelectCardsAction {2, oldRequestId, {}}).accepted && !engine.doubleSwordContext()
               && !engine.pendingCardSelection() && engine.player(1)->handCards().size() == sourceHand,
           "target death clears Double Sword and cannot draw or discard after death");
}

void doubleSwordClearsOnGameOver()
{
    auto engine = setup();
    engine.testingStartSlash(1, {2});
    const auto oldRequestId = engine.pendingCardSelection()->requestId;
    engine.testingKillPlayer(2);
    expect(engine.gameOver() && !engine.doubleSwordContext() && !engine.pendingCardSelection()
               && !engine.submitAction(SelectCardsAction {2, oldRequestId, {}}).accepted,
           "GameOver clears Double Sword and rejects the stale request");
}

void doubleSwordContextDoesNotLeakToNextSlash()
{
    auto engine = setupFour({Gender::Male, Gender::Female, Gender::Female, Gender::Unknown});
    engine.testingStartSlash(1, {2});
    const auto firstRequestId = engine.pendingCardSelection()->requestId;
    engine.submitAction(SelectCardsAction {2, firstRequestId, {}});
    passDodge(engine, 2);
    expect(!engine.doubleSwordContext() && !engine.pendingCardSelection(), "first Slash fully cleans its Double Sword context");
    engine.testingStartSlash(1, {3});
    const auto second = engine.pendingCardSelection();
    expect(second && second->requester == 3 && second->requestId != firstRequestId && engine.doubleSwordContext()
               && engine.doubleSwordContext()->target == 3,
           "next Slash creates a fresh context for its new target");
}

} // namespace

int main()
{
    doubleSwordTriggersAgainstOppositeGender();
    doubleSwordDoesNotTriggerAgainstSameGender();
    doubleSwordDoesNotTriggerWithUnknownGender();
    doubleSwordTargetCanDiscardHandCard();
    doubleSwordTargetCanLetAttackerDraw();
    doubleSwordNoHandForcesAttackerDraw();
    doubleSwordMultiTargetResolvesEachTargetIndependently();
    doubleSwordMultiTargetSupportsDifferentChoices();
    doubleSwordTimeoutLetsAttackerDraw();
    doubleSwordRejectsStaleRequest();
    doubleSwordRejectsIllegalDiscardCandidate();
    doubleSwordDoesNotTriggerAfterUnequip();
    doubleSwordDoesNotTriggerAfterReplacement();
    doubleSwordPendingRequestInvalidatesWhenWeaponIsLost();
    doubleSwordClearsWhenAttackerDies();
    doubleSwordClearsWhenTargetDies();
    doubleSwordClearsOnGameOver();
    doubleSwordContextDoesNotLeakToNextSlash();
    std::cout << "BasicSanguoshaDoubleSwordTests PASS\n";
}
