#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "ai/AIActionDecider.h"
#include "ai/AIActionPacing.h"
#include "ai/AIController.h"
#include "ai/AIIdentityObjective.h"
#include "ai/AIResourcePlanner.h"
#include "ai/AITargetEvaluator.h"
#include "ai/AIThreatEvaluator.h"
#include "client/GameClientController.h"
#include "network/GameServer.h"

using namespace sanguosha;
using namespace sanguosha::network;

void runAIIdentityTests();

namespace {
[[noreturn]] void fail(const char* message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
void expect(bool value, const char* message) { if (!value) fail(message); }
bool waitFor(const std::function<bool()>& condition, int milliseconds = 3000)
{
    if (condition()) return true;
    QEventLoop loop; QTimer timeout, poll; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    timeout.start(milliseconds); poll.start(5); loop.exec(); return condition();
}
void clearHand(Player& player);

void aiActionPacingCoverage()
{
    PlayerViewState normal {1, 1, 1, Phase::Discard}; normal.ownHand.push_back(CardView {"slash", CardType::Slash, Suit::Spade, 7, "Slash"});
    auto critical = normal; critical.currentPhase = Phase::Play;
    auto pass = critical; pass.ownHand.clear();
    auto response = critical; response.response = ResponseView {ResponseType::Dodge, 7, 2, 1, true, true,
        {CardView {"dodge", CardType::Dodge, Suit::Heart, 2, "Dodge"}}, "", 2};
    expect(ai::AIActionPacing::delayFor(normal) == ai::AIActionPacing::NormalActionDelayMs,
           "normal visible AI action uses the configured delay");
    expect(ai::AIActionPacing::delayFor(pass) == ai::AIActionPacing::PassDelayMs
               && ai::AIActionPacing::delayFor(critical) == ai::AIActionPacing::CriticalActionDelayMs
               && ai::AIActionPacing::delayFor(response) == ai::AIActionPacing::CriticalActionDelayMs,
           "AI Pass is shorter while visible responses use the critical delay");
    expect(ai::AIActionPacing::delayFor(normal, true)
               == ai::AIActionPacing::NormalActionDelayMs + ai::AIActionPacing::TurnTransitionDelayMs,
           "AI turn transition adds a separate readable pause");
    expect(ai::AIActionPacing::NormalActionDelayMs == 1200 && ai::AIActionPacing::CriticalActionDelayMs == 1600
               && ai::AIActionPacing::PassDelayMs == 320 && ai::AIActionPacing::TurnTransitionDelayMs == 600,
           "formal pacing constants match Stage 14A-R2 readability targets");

    GameSession bypassSession({"AI", "Other"});
    clearHand(*bypassSession.testingEngine().players().at(0));
    int bypassApplied = 0; ai::AIController bypass(bypassSession, 1, [&] { ++bypassApplied; });
    QElapsedTimer bypassTimer; bypassTimer.start(); bypass.schedule();
    expect(waitFor([&] { return bypassApplied == 1; }, 300) && bypassTimer.elapsed() < 300,
           "testing builds bypass formal pacing without slowing simulations");

    GameSession session({"AI", "Other"});
    clearHand(*session.testingEngine().players().at(0));
    int applied = 0; ai::AIController controller(session, 1, [&] { ++applied; });
    controller.setPacingEnabledForTesting(true, 45);
    controller.schedule();
    expect(applied == 0 && session.testingEngine().currentPhase() == Phase::Play,
           "AI pacing schedules without blocking or submitting immediately");
    expect(waitFor([&] { return applied == 1; }, 500), "visible AI action is submitted after pacing delay");

    GameSession staleSession({"AI", "Other"});
    clearHand(*staleSession.testingEngine().players().at(0));
    const auto humanRoute = staleSession.addClient(1); int staleApplied = 0;
    ai::AIController stale(staleSession, 1, [&] { ++staleApplied; }); stale.setPacingEnabledForTesting(true, 60); stale.schedule();
    expect(staleSession.submitAction(humanRoute, EndPlayPhaseAction {1}).accepted,
           "fixture changes authoritative state while AI action is queued");
    QEventLoop staleWait; QTimer::singleShot(100, &staleWait, &QEventLoop::quit); staleWait.exec();
    expect(staleApplied == 0, "stale delayed AI callback revalidates and does not submit the old action");

    GameSession ended({"AI", "Other"}); clearHand(*ended.testingEngine().players().at(0));
    int afterGameOver = 0; ai::AIController queued(ended, 1, [&] { ++afterGameOver; }); queued.setPacingEnabledForTesting(true, 60); queued.schedule();
    ended.testingEngine().testingKillPlayer(2);
    QEventLoop gameOverWait; QTimer::singleShot(100, &gameOverWait, &QEventLoop::quit); gameOverWait.exec();
    expect(afterGameOver == 0 && ended.testingEngine().gameOver(), "GameOver cancels a queued AI action");
    queued.reset(); queued.stop();
}
std::shared_ptr<Card> card(const char* id, CardType type)
{
    return std::make_shared<Card>(id, id, type, Suit::Heart, 7);
}
void clearHand(Player& player)
{
    std::vector<CardId> ids;
    for (const auto& item : player.handCards()) ids.push_back(item->id());
    for (const auto& id : ids) player.removeCard(id);
}

CardView aiCard(const char* id, CardType type, int minTargets = 0, int maxTargets = 0)
{
    return {id, type, Suit::Heart, 7, id, {}, minTargets, maxTargets};
}

PublicPlayerView visiblePlayer(PlayerId id, Seat seat)
{
    PublicPlayerView player {id, "P" + std::to_string(id), 3, 4, true, false, true, false, 0, {}};
    player.seat = seat;
    return player;
}

PlayerViewState identityDecisionView(PlayerIdentity selfIdentity)
{
    PlayerViewState view {2, 1, 2, Phase::Play};
    view.gameMode = GameMode::Identity;
    view.selfIdentity = selfIdentity;
    auto lord = visiblePlayer(1, 0); lord.identity = PlayerIdentity::Lord;
    auto self = visiblePlayer(2, 1); self.identity = selfIdentity;
    view.players = {lord, self, visiblePlayer(3, 2)};
    view.ownHand = {aiCard("identity-slash", CardType::Slash, 1, 1)};
    return view;
}

void identityObjectiveUsesOnlyVisibleIdentityData()
{
    const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade};
    GameSession session({"L", "AI", "R1", "R2", "N"}, true, identities,
                        {PlayerControlType::Human, PlayerControlType::AI, PlayerControlType::Human, PlayerControlType::Human, PlayerControlType::Human}, GameMode::Identity);
    const auto view = session.viewFor(session.addClient(2));
    expect(view.selfIdentity == PlayerIdentity::Loyalist && view.players[0].identity == PlayerIdentity::Lord,
           "Identity AI receives its own identity and the public Lord");
    expect(!view.players[2].identity && !view.players[3].identity && !view.players[4].identity,
           "Identity AI cannot inspect hidden living identities");
    const ai::AIIdentityObjective objective(view);
    expect(objective.active() && objective.selfIdentity() == PlayerIdentity::Loyalist && objective.lordPlayerId() == std::optional<PlayerId>(1),
           "Identity objective is built from the filtered AI view");
    expect(objective.goal() == ai::AIIdentityGoal::PreserveLord
               && ai::AIIdentityObjective(identityDecisionView(PlayerIdentity::Lord)).goal() == ai::AIIdentityGoal::PreserveLord
               && ai::AIIdentityObjective(identityDecisionView(PlayerIdentity::Rebel)).goal() == ai::AIIdentityGoal::EliminateLord
               && ai::AIIdentityObjective(identityDecisionView(PlayerIdentity::Renegade)).goal() == ai::AIIdentityGoal::Independent,
           "each Identity AI receives its own rule-consistent minimum objective");
}

void identityObjectivesModifyOnlyLegalGenericChoices()
{
    ai::BasicAIActionDecider decider;
    const auto legal = [] (const CardId&, PlayerId target) { return target != 2; };
    const auto loyalAction = decider.decideAction(identityDecisionView(PlayerIdentity::Loyalist), 0, legal);
    expect(loyalAction && std::get<PlayCardAction>(*loyalAction).targetIds == std::vector<PlayerId>{3},
           "Loyalist does not choose the public Lord as a hostile target");
    const auto rebelAction = decider.decideAction(identityDecisionView(PlayerIdentity::Rebel), 0, legal);
    expect(rebelAction && std::get<PlayCardAction>(*rebelAction).targetIds == std::vector<PlayerId>{1},
           "Rebel prioritizes the public Lord among equal legal targets");

    auto rescue = identityDecisionView(PlayerIdentity::Loyalist);
    rescue.response = ResponseView {ResponseType::PeachRescue, 9, 1, 2, true, true, {aiCard("rescue-peach", CardType::Peach)}, "", 1};
    const auto loyalRescue = decider.decideAction(rescue, 0, legal);
    expect(loyalRescue && std::get<RespondAction>(*loyalRescue).cardId == std::optional<CardId>("rescue-peach"),
           "Loyalist responds with Peach when the public Lord is dying");
    rescue.selfIdentity = PlayerIdentity::Rebel;
    rescue.players[1].identity = PlayerIdentity::Rebel;
    const auto rebelRescue = decider.decideAction(rescue, 0, legal);
    expect(rebelRescue && !std::get<RespondAction>(*rebelRescue).cardId,
           "Rebel does not automatically rescue the public Lord");

    auto ffa = identityDecisionView(PlayerIdentity::None);
    ffa.gameMode = GameMode::FreeForAll;
    ffa.players[0].identity.reset(); ffa.players[1].identity.reset();
    const ai::AIIdentityObjective ffaObjective(ffa);
    const auto ffaAction = decider.decideAction(ffa, 0, legal);
    expect(!ffaObjective.active() && ffaObjective.lordPlayerId() == std::nullopt && ffaAction
               && std::get<PlayCardAction>(*ffaAction).targetIds == std::vector<PlayerId>{1},
           "FreeForAll ignores all identity objectives");
}

void threatEvaluatorUsesOnlyPublicDeterministicState()
{
    auto lowHand = visiblePlayer(3, 2); lowHand.handCardCount = 1;
    auto highHand = lowHand; highHand.id = 4; highHand.seat = 3; highHand.handCardCount = 4;
    expect(ai::AIThreatEvaluator::threatScore(highHand) > ai::AIThreatEvaluator::threatScore(lowHand),
           "more public hand cards produce higher threat");

    auto weapon = lowHand;
    weapon.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = aiCard("halberd", CardType::Weapon);
    weapon.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]->displayName = "方天画戟";
    auto basicWeapon = lowHand;
    basicWeapon.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = aiCard("basic", CardType::Weapon);
    expect(ai::AIThreatEvaluator::threatScore(weapon) > ai::AIThreatEvaluator::threatScore(lowHand)
               && ai::AIThreatEvaluator::threatScore(weapon) > ai::AIThreatEvaluator::threatScore(basicWeapon),
           "public offensive weapons add deterministic differentiated threat");

    auto armor = lowHand;
    armor.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)] = aiCard("armor", CardType::Armor);
    armor.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)]->displayName = "八卦阵";
    expect(ai::AIThreatEvaluator::threatScore(armor) > ai::AIThreatEvaluator::threatScore(lowHand),
           "public armor adds persistence threat");
    auto threatView = identityDecisionView(PlayerIdentity::None); threatView.gameMode = GameMode::FreeForAll;
    threatView.players = {visiblePlayer(2, 0), lowHand, armor};
    threatView.selfPlayerId = 2;
    expect(ai::AIThreatEvaluator::defenseScore(armor) > ai::AIThreatEvaluator::defenseScore(lowHand)
               && ai::AIThreatEvaluator::killOpportunityScore(threatView, armor, true, 1)
                    < ai::AIThreatEvaluator::killOpportunityScore(threatView, lowHand, true, 1),
           "strong public armor raises Defense and lowers KillOpportunity");
    auto lowHp = lowHand; lowHp.hp = 1; lowHp.handCardCount = 0;
    auto highHp = lowHand; highHp.hp = 4; highHp.handCardCount = 6;
    expect(ai::AIThreatEvaluator::killOpportunityScore(threatView, lowHp, true, 2)
               > ai::AIThreatEvaluator::killOpportunityScore(threatView, highHp, true, 2),
           "low HP and an empty hand raise KillOpportunity while high HP does not become the kill target");
    auto cardsInHand = lowHp; cardsInHand.handCardCount = 3;
    expect(ai::AIThreatEvaluator::killOpportunityScore(threatView, lowHp, true, 1)
               > ai::AIThreatEvaluator::killOpportunityScore(threatView, cardsInHand, true, 1),
           "an empty public hand raises KillOpportunity without inspecting card faces");
    auto restricted = armor;
    restricted.judgmentCards.push_back(aiCard("indulgence", CardType::Indulgence));
    expect(ai::AIThreatEvaluator::threatScore(restricted) < ai::AIThreatEvaluator::threatScore(armor),
           "public delayed-trick restriction reduces threat without predicting judgment");

    auto hiddenIdentityA = lowHand; hiddenIdentityA.identity = PlayerIdentity::Rebel;
    auto hiddenIdentityB = lowHand; hiddenIdentityB.identity = PlayerIdentity::Loyalist;
    expect(ai::AIThreatEvaluator::threatScore(hiddenIdentityA) == ai::AIThreatEvaluator::threatScore(hiddenIdentityB),
           "identity is not a Threat Evaluator input");
    GameSession hiddenSlash({"Self", "Target"}, true);
    GameSession hiddenPeach({"Self", "Target"}, true);
    clearHand(*hiddenSlash.testingEngine().players().at(1));
    clearHand(*hiddenPeach.testingEngine().players().at(1));
    hiddenSlash.testingEngine().players().at(1)->addCard(card("hidden-slash", CardType::Slash));
    hiddenPeach.testingEngine().players().at(1)->addCard(card("hidden-peach", CardType::Peach));
    const auto slashView = hiddenSlash.viewFor(hiddenSlash.addClient(1));
    const auto peachView = hiddenPeach.viewFor(hiddenPeach.addClient(1));
    expect(ai::AIThreatEvaluator::threatScore(slashView.players[1]) == ai::AIThreatEvaluator::threatScore(peachView.players[1]),
           "different hidden hand contents with the same public count do not affect threat");
    auto slashResourceView = slashView;
    auto peachResourceView = peachView;
    for (auto* candidate : {&slashResourceView, &peachResourceView}) {
        candidate->currentTurnPlayer = 1;
        candidate->currentPhase = Phase::Play;
        candidate->ownHand = {aiCard("privacy-draw", CardType::ExNihilo), aiCard("privacy-slash", CardType::Slash, 1, 1)};
    }
    const ai::BasicAIActionDecider privacyDecider;
    const auto hiddenSlashAction = privacyDecider.decideAction(slashResourceView, 0,
        [] (const CardId&, PlayerId target) { return target == 2; });
    const auto hiddenPeachAction = privacyDecider.decideAction(peachResourceView, 0,
        [] (const CardId&, PlayerId target) { return target == 2; });
    expect(hiddenSlashAction && hiddenPeachAction && hiddenSlashAction->index() == hiddenPeachAction->index()
               && std::get<PlayCardAction>(*hiddenSlashAction).cardId == std::get<PlayCardAction>(*hiddenPeachAction).cardId,
           "different hidden opponent card faces cannot change resource-planned action choice");

    auto sameSeat = lowHand; sameSeat.id = 8; sameSeat.seat = 4;
    auto laterSeat = lowHand; laterSeat.id = 7; laterSeat.seat = 5;
    expect(ai::AIThreatEvaluator::isHigherThreat(sameSeat, laterSeat)
               && ai::AIThreatEvaluator::isHigherThreat(sameSeat, laterSeat),
           "equal threat resolves with a stable deterministic seat tie-break");
    auto ffa = identityDecisionView(PlayerIdentity::None); ffa.gameMode = GameMode::FreeForAll;
    auto identity = ffa; identity.gameMode = GameMode::Identity;
    expect(ai::AIThreatEvaluator::threatScore(ffa.players[0]) == ai::AIThreatEvaluator::threatScore(identity.players[0]),
           "Threat Evaluator works identically in FreeForAll and Identity modes");
}

void threatSystemIntegratesByActionIntent()
{
    ai::BasicAIActionDecider decider;
    auto view = identityDecisionView(PlayerIdentity::None);
    view.gameMode = GameMode::FreeForAll; view.players[0].identity.reset(); view.players[1].identity.reset();
    view.players[0].hp = 4; view.players[0].handCardCount = 6;
    view.players[2].hp = 1; view.players[2].handCardCount = 0;
    view.ownHand = {aiCard("threat-slash", CardType::Slash, 1, 1)};
    const auto legalHostile = [] (const CardId&, PlayerId target) { return target != 2; };
    auto action = decider.decideAction(view, 0, legalHostile);
    expect(action && std::get<PlayCardAction>(*action).targetIds == std::vector<PlayerId> {3},
           "Slash prioritizes a legal low-defense kill opportunity over raw sustained threat");

    view.players[0].hp = view.players[2].hp = 4; view.players[0].handCardCount = 1; view.players[2].handCardCount = 4;
    view.players[2].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = aiCard("crossbow", CardType::Weapon);
    view.players[2].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]->displayName = "诸葛连弩";
    view.ownHand = {aiCard("dismantle", CardType::Dismantlement, 1, 1)};
    action = decider.decideAction(view, 0, legalHostile);
    expect(std::get<PlayCardAction>(*action).targetIds == std::vector<PlayerId> {3},
           "Dismantlement prioritizes dangerous public equipment and hand pressure");

    view.players[2].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)].reset();
    view.players[2].handCardCount = 7;
    view.ownHand = {aiCard("snatch", CardType::Snatch, 1, 1)};
    action = decider.decideAction(view, 0, legalHostile);
    expect(std::get<PlayCardAction>(*action).targetIds == std::vector<PlayerId> {3},
           "Snatch prioritizes the richer legal public target");

    view.players[0].hp = 4; view.players[0].handCardCount = 5;
    view.players[2].hp = 1; view.players[2].handCardCount = 0;
    view.ownHand = {aiCard("duel", CardType::Duel, 1, 1), aiCard("duel-slash-a", CardType::Slash, 1, 1), aiCard("duel-slash-b", CardType::Slash, 1, 1)};
    action = decider.decideAction(view, 0, [] (const CardId& id, PlayerId target) { return id == "duel" && target != 2; });
    expect(std::get<PlayCardAction>(*action).cardId == "duel"
               && std::get<PlayCardAction>(*action).targetIds == std::vector<PlayerId> {3},
           "Duel prioritizes a low-resource killable target using only public hand count");

    view.players[0].hp = 2; view.players[0].handCardCount = 0;
    view.players[2].hp = 4; view.players[2].handCardCount = 6;
    view.players[2].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = aiCard("halberd", CardType::Weapon);
    view.players[2].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]->displayName = "方天画戟";
    view.ownHand = {aiCard("indulgence", CardType::Indulgence, 1, 1)};
    action = decider.decideAction(view, 0, legalHostile);
    expect(std::get<PlayCardAction>(*action).targetIds == std::vector<PlayerId> {3},
           "delayed tricks prioritize sustained high-threat high-resource targets");

    view.players[0] = visiblePlayer(1, 0); view.players[2] = visiblePlayer(3, 2);
    view.players[0].hp = view.players[2].hp = 1; view.players[0].handCardCount = view.players[2].handCardCount = 0;
    view.ownHand = {aiCard("aoe", CardType::BarbarianInvasion, 0, 0), aiCard("horse", CardType::DefensiveHorse, 0, 0)};
    action = decider.decideAction(view, 0, legalHostile);
    expect(std::get<PlayCardAction>(*action).cardId == "aoe", "AOE aggregates public low-HP and low-hand pressure");

    view.players[0] = visiblePlayer(1, 0); view.players[2] = visiblePlayer(3, 2);
    view.ownHand = {aiCard("tie-slash", CardType::Slash, 1, 1)};
    action = decider.decideAction(view, 0, legalHostile);
    expect(std::get<PlayCardAction>(*action).targetIds == std::vector<PlayerId> {1},
           "equal pressure resolves deterministically by seat then player ID");
}

void resourcePlannerValuesCardsAndActions()
{
    ai::BasicAIActionDecider decider;
    auto view = identityDecisionView(PlayerIdentity::None);
    view.gameMode = GameMode::FreeForAll;
    view.players[0].identity.reset(); view.players[1].identity.reset();
    view.players[1].hp = 1;
    const auto peach = aiCard("reserve-peach", CardType::Peach);
    const auto dodge = aiCard("reserve-dodge", CardType::Dodge);
    const auto slash = aiCard("reserve-slash", CardType::Slash, 1, 1);
    view.ownHand = {peach, dodge, slash};
    expect(ai::AIResourcePlanner::keepValue(view, peach) > ai::AIResourcePlanner::keepValue(view, slash)
               && ai::AIResourcePlanner::keepValue(view, dodge) > ai::AIResourcePlanner::keepValue(view, slash),
           "low-HP resource values preserve Peach and Dodge before ordinary cards");
    auto safeWound = view; safeWound.players[1].hp = 3;
    safeWound.players[0].hp = 1; safeWound.players[0].maxHp = 1; safeWound.players[0].handCardCount = 0;
    safeWound.ownHand = {peach};
    auto action = decider.decideAction(safeWound, 0, [] (const CardId&, PlayerId) { return false; });
    expect(action && std::holds_alternative<EndPlayPhaseAction>(*action),
           "a safe one-HP wound does not automatically consume the only Peach");
    safeWound.players[0].hp = 4; safeWound.players[0].maxHp = 4; safeWound.players[0].handCardCount = 7;
    auto threateningWeapon = aiCard("threat-weapon", CardType::Weapon); threateningWeapon.displayName = "诸葛连弩";
    safeWound.players[0].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = threateningWeapon;
    action = decider.decideAction(safeWound, 0, [] (const CardId&, PlayerId) { return false; });
    expect(action && std::get<PlayCardAction>(*action).cardId == "reserve-peach",
           "public high threat raises survival pressure enough to heal a wound");

    auto oneSlash = view; oneSlash.players[1].hp = 4; oneSlash.ownHand = {slash};
    auto manySlash = oneSlash;
    manySlash.ownHand = {slash, aiCard("slash-b", CardType::Slash), aiCard("slash-c", CardType::Slash)};
    expect(ai::AIResourcePlanner::keepValue(manySlash, slash) < ai::AIResourcePlanner::keepValue(oneSlash, slash),
           "redundant Slash copies lose KeepValue");
    auto oneDodge = oneSlash; oneDodge.ownHand = {dodge};
    auto manyDodge = oneSlash;
    manyDodge.ownHand = {dodge, aiCard("dodge-b", CardType::Dodge), aiCard("dodge-c", CardType::Dodge), aiCard("dodge-d", CardType::Dodge)};
    expect(ai::AIResourcePlanner::keepValue(manyDodge, dodge) < ai::AIResourcePlanner::keepValue(oneDodge, dodge),
           "redundant Dodge copies lose KeepValue while the last copy is reserved");

    auto nullify = oneSlash;
    nullify.ownHand = {aiCard("null-a", CardType::Nullification)};
    nullify.response = ResponseView {ResponseType::Nullification, 72, 1, 2, true, true, nullify.ownHand, "", 2};
    nullify.nullification = NullificationContextView {1, CardType::Dismantlement, 2, 1};
    action = decider.decideAction(nullify, 0, [] (const CardId&, PlayerId) { return true; });
    expect(action && !std::get<RespondAction>(*action).cardId, "the last Nullification is reserved against a low-impact trick");
    nullify.ownHand.push_back(aiCard("null-b", CardType::Nullification));
    action = decider.decideAction(nullify, 0, [] (const CardId&, PlayerId) { return true; });
    expect(action && std::get<RespondAction>(*action).cardId == std::optional<CardId>("null-a"),
           "redundant Nullification may be spent without consuming the final reserve");

    auto wine = oneSlash;
    wine.ownHand = {aiCard("wine", CardType::Wine), aiCard("wine-slash", CardType::Slash, 1, 1)};
    action = decider.decideAction(wine, 0, [] (const CardId&, PlayerId) { return false; });
    expect(action && std::holds_alternative<EndPlayPhaseAction>(*action), "Wine is not consumed without a legal attack opportunity");
    wine.players[0].hp = 2; wine.players[0].handCardCount = 0;
    action = decider.decideAction(wine, 0, [] (const CardId&, PlayerId target) { return target == 1; });
    expect(action && std::get<PlayCardAction>(*action).cardId == "wine", "Wine precedes Slash for a public high-value kill opportunity");

    auto elemental = oneSlash;
    elemental.players[0].hp = 3; elemental.players[0].handCardCount = 2;
    elemental.ownHand = {aiCard("normal", CardType::Slash, 1, 1), aiCard("fire", CardType::FireSlash, 1, 1)};
    action = decider.decideAction(elemental, 0, [] (const CardId&, PlayerId target) { return target == 1; });
    expect(action && std::get<PlayCardAction>(*action).cardId == "normal", "elemental Slash is preserved when it has no public synergy");
    elemental.players[0].chained = true;
    action = decider.decideAction(elemental, 0, [] (const CardId&, PlayerId target) { return target == 1; });
    expect(action && std::get<PlayCardAction>(*action).cardId == "fire", "Fire Slash is preferred against a chained public target");

    auto spear = oneSlash;
    spear.ownHand = {aiCard("cheap-a", CardType::Slash), aiCard("cheap-b", CardType::Slash), peach, aiCard("reserve-null", CardType::Nullification)};
    expect(ai::AIResourcePlanner::serpentSpearMaterials(spear) == std::vector<CardId>({"cheap-a", "cheap-b"}),
           "Serpent Spear consumes the deterministic lowest-value pair");
    spear.ownHand = {peach, aiCard("reserve-null", CardType::Nullification)};
    expect(ai::AIResourcePlanner::serpentSpearMaterials(spear).empty(), "Serpent Spear does not trade away Peach plus Nullification outside emergency response");

    auto equipment = oneSlash;
    auto silverMoon = aiCard("silver-moon", CardType::Weapon); silverMoon.displayName = "银月枪";
    auto crossbow = aiCard("crossbow-upgrade", CardType::Weapon); crossbow.displayName = "诸葛连弩";
    equipment.players[1].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = crossbow;
    expect(ai::AIResourcePlanner::equipmentReplacementValue(equipment, silverMoon) == 0,
           "AI skips a clearly worse equipment replacement");
    equipment.players[1].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = silverMoon;
    expect(ai::AIResourcePlanner::equipmentReplacementValue(equipment, crossbow) > 0,
           "AI recognizes a meaningful equipment upgrade");
    auto lion = aiCard("lion", CardType::Armor); lion.displayName = "白银狮子";
    auto bagua = aiCard("bagua", CardType::Armor); bagua.displayName = "八卦阵";
    equipment.players[1].hp = 2;
    equipment.players[1].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)] = lion;
    expect(ai::AIResourcePlanner::equipmentReplacementValue(equipment, bagua) > 0,
           "wounded AI includes Silver Lion leave-play healing in replacement value");
    auto mountView = oneSlash;
    mountView.players.push_back(visiblePlayer(4, 3));
    const auto offensiveHorse = aiCard("offensive-horse", CardType::OffensiveHorse);
    const auto defensiveHorse = aiCard("defensive-horse", CardType::DefensiveHorse);
    expect(ai::AIResourcePlanner::equipmentReplacementValue(mountView, offensiveHorse)
               > ai::AIResourcePlanner::equipmentReplacementValue(oneSlash, offensiveHorse),
           "offensive mount value includes public distance-two reach structure");
    mountView.players[0].handCardCount = 7;
    auto mountWeapon = aiCard("mount-threat", CardType::Weapon); mountWeapon.displayName = "诸葛连弩";
    mountView.players[0].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = mountWeapon;
    expect(ai::AIResourcePlanner::equipmentReplacementValue(mountView, defensiveHorse)
               > ai::AIResourcePlanner::equipmentReplacementValue(oneSlash, defensiveHorse),
           "defensive mount value rises under adjacent public threat");

    auto ordering = oneSlash;
    ordering.ownHand = {aiCard("attack", CardType::Slash, 1, 1), aiCard("draw", CardType::ExNihilo)};
    action = decider.decideAction(ordering, 0, [] (const CardId&, PlayerId target) { return target == 1; });
    expect(action && std::get<PlayCardAction>(*action).cardId == "draw", "high immediate-use Ex Nihilo is ordered before an ordinary attack");

    auto discard = view;
    discard.currentPhase = Phase::Discard;
    discard.ownHand = {peach, dodge, aiCard("discard-slash-a", CardType::Slash), aiCard("discard-slash-b", CardType::Slash)};
    action = decider.decideAction(discard, 1, [] (const CardId&, PlayerId) { return true; });
    expect(action && std::get<DiscardAction>(*action).cardIds == std::vector<CardId> {"discard-slash-a"},
           "discard planning preserves low-HP emergency cards and removes redundant Slash first");
}

void resourcePlannerChoosesSelectionCosts()
{
    ai::BasicAIActionDecider decider;
    auto view = identityDecisionView(PlayerIdentity::None);
    view.gameMode = GameMode::FreeForAll;
    view.players[0].identity.reset(); view.players[1].identity.reset();
    view.players[0].hp = 1; view.players[0].handCardCount = 0;
    view.ownHand = {aiCard("slash-a", CardType::Slash), aiCard("slash-b", CardType::Slash), aiCard("peach", CardType::Peach), aiCard("null", CardType::Nullification)};
    CardSelectionView axe {81, CardSelectionPurpose::AxeDiscard, 1, 0, 2,
        {{11, CardZone::Hand, {}, false, "slash-a", CardType::Slash}, {12, CardZone::Hand, {}, false, "slash-b", CardType::Slash},
         {13, CardZone::Hand, {}, false, "peach", CardType::Peach}}, ""};
    expect(decider.decideSelection(view, axe) == std::vector<SelectionOptionId>({11, 12}),
           "Axe selects the two lowest-cost resources for a kill opportunity");
    axe.options = {{13, CardZone::Hand, {}, false, "peach", CardType::Peach}, {14, CardZone::Hand, {}, false, "null", CardType::Nullification}};
    expect(decider.decideSelection(view, axe).empty(), "Axe declines when forced damage is worth less than protected resources");

    view.players[1].hp = 1;
    CardSelectionView harvest {82, CardSelectionPurpose::Harvest, 2, 1, 1,
        {{21, CardZone::Hand, {}, false, "slash", CardType::Slash}, {22, CardZone::Hand, {}, false, "peach", CardType::Peach}}, ""};
    expect(decider.decideSelection(view, harvest) == std::vector<SelectionOptionId> {22}, "low-HP Harvest chooses Peach by highest resource value");

    CardSelectionView fire {83, CardSelectionPurpose::FireAttackDiscard, 1, 0, 1,
        {{31, CardZone::Hand, {}, false, "peach", CardType::Peach}, {30, CardZone::Hand, {}, false, "slash", CardType::Slash}}, ""};
    expect(decider.decideSelection(view, fire) == std::vector<SelectionOptionId> {30}, "Fire Attack pays the lowest matching-card cost");
    fire.options = {{31, CardZone::Hand, {}, false, "peach", CardType::Peach}};
    expect(decider.decideSelection(view, fire).empty(), "Fire Attack declines rather than spend a low-HP Peach for weak damage");

    auto hiddenA = view; auto hiddenB = view;
    hiddenA.players[0].identity = PlayerIdentity::Rebel;
    hiddenB.players[0].identity = PlayerIdentity::Loyalist;
    expect(decider.decideSelection(hiddenA, harvest) == decider.decideSelection(hiddenB, harvest),
           "resource selection is deterministic and independent of hidden identity content");
}

void advancedTargetEvaluatorScoresActionContext()
{
    auto view = identityDecisionView(PlayerIdentity::None);
    view.gameMode = GameMode::FreeForAll;
    view.players[0].identity.reset(); view.players[1].identity.reset();
    view.players[0].hp = 4; view.players[0].handCardCount = 5;
    view.players[2].hp = 1; view.players[2].handCardCount = 0;
    const auto slash = aiCard("target-slash", CardType::Slash, 1, 1);
    view.ownHand = {slash};
    auto best = ai::AITargetEvaluator::bestTargetSet(view, slash,
        [] (const CardId&, PlayerId id) { return id != 2; });
    expect(best.targets == std::vector<PlayerId> {3}, "Slash prefers a low-defense killable target");

    view.players[0].hp = view.players[2].hp = 4;
    view.players[0].handCardCount = 7; view.players[2].handCardCount = 5;
    auto crossbow = aiCard("target-crossbow", CardType::Weapon); crossbow.displayName = "诸葛连弩";
    view.players[0].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = crossbow;
    best = ai::AITargetEvaluator::bestTargetSet(view, slash, [] (const CardId&, PlayerId id) { return id != 2; });
    expect(best.targets == std::vector<PlayerId> {1}, "Slash suppresses the higher public threat when neither target is lethal");

    view.players[0] = visiblePlayer(1, 0); view.players[2] = visiblePlayer(3, 2);
    auto bagua = aiCard("target-bagua", CardType::Armor); bagua.displayName = "八卦阵";
    view.players[0].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)] = bagua;
    best = ai::AITargetEvaluator::bestTargetSet(view, slash, [] (const CardId&, PlayerId id) { return id != 2; });
    expect(best.targets == std::vector<PlayerId> {3}, "Slash avoids a higher-defense target when an equivalent open target exists");

    auto vineTarget = visiblePlayer(1, 0);
    auto vine = aiCard("target-vine", CardType::Armor); vine.displayName = "藤甲";
    vineTarget.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)] = vine;
    auto plainTarget = visiblePlayer(3, 2);
    const auto fire = aiCard("target-fire", CardType::FireSlash, 1, 1);
    expect(ai::AITargetEvaluator::evaluate(view, fire, vineTarget, true, 2).finalScore()
               > ai::AITargetEvaluator::evaluate(view, fire, plainTarget, true, 2).finalScore(),
           "Fire Slash gains context-sensitive value against Vine");
    expect(ai::AITargetEvaluator::evaluate(view, slash, vineTarget, true, 2).finalScore()
               < ai::AITargetEvaluator::evaluate(view, slash, plainTarget, true, 2).finalScore(),
           "normal Slash accounts for Vine prevention instead of sharing Fire Slash scoring");

    auto chainView = view;
    chainView.players = {visiblePlayer(1, 0), visiblePlayer(2, 1), visiblePlayer(3, 2), visiblePlayer(4, 3)};
    chainView.players[0].chained = true;
    chainView.players[2].chained = true; chainView.players[2].hp = 1; chainView.players[2].handCardCount = 0;
    const auto thunder = aiCard("target-thunder", CardType::ThunderSlash, 1, 1);
    expect(ai::AITargetEvaluator::evaluate(chainView, fire, chainView.players[0], true, 2).chainValue > 0
               && ai::AITargetEvaluator::evaluate(chainView, thunder, chainView.players[0], true, 2).chainValue > 0,
           "Fire and Thunder Slash include profitable chained propagation");
    expect(ai::AITargetEvaluator::evaluate(chainView, slash, chainView.players[0], true, 2).chainValue == 0,
           "normal Slash receives no fake elemental chain value");
    const int safeChainAttack = ai::AITargetEvaluator::evaluate(chainView, fire, chainView.players[0], true, 2).finalScore();
    chainView.players[1].chained = true; chainView.players[1].hp = 1;
    expect(ai::AITargetEvaluator::evaluate(chainView, fire, chainView.players[0], true, 2).finalScore() < safeChainAttack,
           "elemental targeting subtracts severe self-chain propagation risk");

    auto duelView = view;
    duelView.ownHand = {aiCard("duel", CardType::Duel, 1, 1), aiCard("duel-a", CardType::Slash), aiCard("duel-b", CardType::Slash)};
    duelView.players[0].handCardCount = 6; duelView.players[2].handCardCount = 0;
    const auto duel = duelView.ownHand.front();
    best = ai::AITargetEvaluator::bestTargetSet(duelView, duel, [] (const CardId&, PlayerId id) { return id != 2; });
    expect(best.targets == std::vector<PlayerId> {3}, "Duel prefers a low-public-resource target without reading its Slash faces");

    auto denialView = view;
    denialView.players[0] = visiblePlayer(1, 0); denialView.players[2] = visiblePlayer(3, 2);
    denialView.players[0].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = crossbow;
    const auto dismantle = aiCard("target-dismantle", CardType::Dismantlement, 1, 1);
    best = ai::AITargetEvaluator::bestTargetSet(denialView, dismantle, [] (const CardId&, PlayerId id) { return id != 2; });
    expect(best.targets == std::vector<PlayerId> {1}, "Dismantlement prioritizes dangerous public equipment");
    denialView.players[0].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)].reset();
    denialView.players[0].handCardCount = 1; denialView.players[2].handCardCount = 7;
    const auto snatch = aiCard("target-snatch", CardType::Snatch, 1, 1);
    best = ai::AITargetEvaluator::bestTargetSet(denialView, snatch, [] (const CardId&, PlayerId id) { return id != 2; });
    expect(best.targets == std::vector<PlayerId> {3}, "Snatch interprets high public resources as acquisition value");
    const auto indulgence = aiCard("target-indulgence", CardType::Indulgence, 1, 1);
    best = ai::AITargetEvaluator::bestTargetSet(denialView, indulgence, [] (const CardId&, PlayerId id) { return id != 2; });
    expect(best.targets == std::vector<PlayerId> {3}, "delayed trick prioritizes high-threat high-action-value targets");

    auto cheapFire = chainView;
    cheapFire.players[1].chained = false;
    cheapFire.ownHand = {aiCard("fire-attack", CardType::FireAttack, 1, 1), aiCard("cheap-cost", CardType::Slash)};
    auto costlyFire = cheapFire;
    costlyFire.players[1].hp = 1;
    costlyFire.ownHand = {aiCard("fire-attack", CardType::FireAttack, 1, 1), aiCard("costly-peach", CardType::Peach)};
    const auto fireAttack = cheapFire.ownHand.front();
    expect(ai::AITargetEvaluator::evaluate(cheapFire, fireAttack, cheapFire.players[0], true, 0).finalScore()
               > ai::AITargetEvaluator::evaluate(costlyFire, fireAttack, costlyFire.players[0], true, 0).finalScore(),
           "Fire Attack target value includes the visible discard resource cost");
    expect(ai::AITargetEvaluator::evaluate(cheapFire, fireAttack, cheapFire.players[0], true, 0).chainValue > 0,
           "Fire Attack includes profitable public chain propagation");
}

void advancedTargetEvaluatorScoresSetsAndGlobalEffects()
{
    auto view = identityDecisionView(PlayerIdentity::None);
    view.gameMode = GameMode::FreeForAll;
    view.players = {visiblePlayer(1, 0), visiblePlayer(2, 1), visiblePlayer(3, 2), visiblePlayer(4, 3)};
    view.ownHand = {aiCard("chain", CardType::IronChain, 0, 2), aiCard("future-fire", CardType::FireSlash, 1, 1)};
    view.players[0].hp = 2; view.players[2].hp = 1;
    const auto chain = view.ownHand.front();
    auto set = ai::AITargetEvaluator::bestTargetSet(view, chain, [] (const CardId&, PlayerId) { return true; });
    expect(set.targets.size() == 2 && std::find(set.targets.begin(), set.targets.end(), 2) == set.targets.end(),
           "Iron Chain creates a profitable two-competitor chain without chaining self");
    view.players[1].chained = true; view.players[1].hp = 1;
    const auto unlinkSelf = ai::AITargetEvaluator::evaluate(view, chain, view.players[1], true, 1);
    expect(unlinkSelf.chainValue > 0 && unlinkSelf.finalScore() > 0, "Iron Chain values removing a dangerous self-chain");

    auto loyal = identityDecisionView(PlayerIdentity::Loyalist);
    loyal.players[0].hp = 1; loyal.players[0].handCardCount = 0;
    loyal.players[2].hp = 4; loyal.players[2].handCardCount = 5;
    expect(ai::AITargetEvaluator::aoeValue(loyal, CardType::ArrowBarrage, 1) <= 0,
           "AOE skips a negative total dominated by known Lord risk");
    auto ffa = loyal; ffa.gameMode = GameMode::FreeForAll; ffa.selfIdentity = PlayerIdentity::None;
    for (auto& player : ffa.players) player.identity.reset();
    ffa.players[0].hp = ffa.players[2].hp = 1;
    ffa.players[0].handCardCount = ffa.players[2].handCardCount = 0;
    expect(ai::AITargetEvaluator::aoeValue(ffa, CardType::BarbarianInvasion, 1) > 0,
           "FFA uses positive aggregate AOE value without identity bias");
    auto moreHand = ffa; moreHand.players[0].handCardCount = 6;
    expect(ai::AITargetEvaluator::aoeValue(ffa, CardType::BarbarianInvasion, 1)
               > ai::AITargetEvaluator::aoeValue(moreHand, CardType::BarbarianInvasion, 1),
           "Savage Assault uses only public hand count as its response estimate");
    auto baguaView = ffa;
    auto bagua = aiCard("aoe-bagua", CardType::Armor); bagua.displayName = "八卦阵";
    baguaView.players[0].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)] = bagua;
    expect(ai::AITargetEvaluator::aoeValue(baguaView, CardType::ArrowBarrage, 1)
               < ai::AITargetEvaluator::aoeValue(ffa, CardType::ArrowBarrage, 1),
           "Arrow Barrage lowers expected value against public Bagua armor");

    auto attackerA = visiblePlayer(1, 0); attackerA.handCardCount = 5;
    attackerA.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = aiCard("borrow-crossbow", CardType::Weapon);
    attackerA.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]->displayName = "诸葛连弩";
    auto attackerB = visiblePlayer(3, 2); attackerB.handCardCount = 0;
    attackerB.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = aiCard("borrow-basic", CardType::Weapon);
    auto victim = visiblePlayer(4, 3); victim.hp = 1; victim.handCardCount = 0;
    expect(ai::AITargetEvaluator::borrowedSwordPairScore(ffa, attackerA, victim, 1)
               > ai::AITargetEvaluator::borrowedSwordPairScore(ffa, attackerB, victim, 1),
           "Borrowed Sword scores attacker and victim as one public pair");

    auto halberdView = ffa;
    halberdView.players.push_back(visiblePlayer(4, 3));
    auto halberdSlash = aiCard("halberd-slash", CardType::Slash, 1, 3);
    halberdView.ownHand = {halberdSlash};
    set = ai::AITargetEvaluator::bestTargetSet(halberdView, halberdSlash,
        [] (const CardId&, PlayerId id) { return id != 2; });
    expect(set.targets.size() == 3, "multi-Slash target-set scoring evaluates the complete legal Halberd combination");

    ai::BasicAIActionDecider decider;
    CardSelectionView selection {91, CardSelectionPurpose::Dismantlement, 1, 1, 1,
        {{1, CardZone::Hand, {}, false, "乐不思蜀", CardType::Indulgence},
         {2, CardZone::Equipment, EquipmentSlot::Weapon, false, "诸葛连弩", CardType::Weapon}}, ""};
    expect(decider.decideSelection(ffa, selection) == std::vector<SelectionOptionId> {2},
           "Dismantlement removes dangerous equipment instead of helping an enemy discard Indulgence");

    auto tieA = ai::AITargetEvaluator::bestTargetSet(ffa, aiCard("tie", CardType::Slash, 1, 1),
        [] (const CardId&, PlayerId id) { return id != 2; });
    auto tieB = ai::AITargetEvaluator::bestTargetSet(ffa, aiCard("tie", CardType::Slash, 1, 1),
        [] (const CardId&, PlayerId id) { return id != 2; });
    expect(tieA.targets == tieB.targets, "advanced target and target-set tie-breaking is deterministic");

    auto wineTargetView = ffa;
    wineTargetView.players[0].hp = 3; wineTargetView.players[0].handCardCount = 5;
    wineTargetView.players[2].hp = 2; wineTargetView.players[2].handCardCount = 0;
    const auto wineSlash = aiCard("wine-target-slash", CardType::Slash, 1, 1);
    wineTargetView.ownHand = {wineSlash};
    expect(ai::AITargetEvaluator::bestTargetSet(wineTargetView, wineSlash,
               [] (const CardId&, PlayerId id) { return id != 2; }).targets == std::vector<PlayerId> {3},
           "Wine-enhanced Slash uses the same evaluator to select the best lethal target");

    auto loyalTargeting = identityDecisionView(PlayerIdentity::Loyalist);
    loyalTargeting.ownHand = {aiCard("loyal-target-slash", CardType::Slash, 1, 1)};
    loyalTargeting.players[0].hp = 1; loyalTargeting.players[0].handCardCount = 0;
    loyalTargeting.players[2].hp = 3;
    expect(ai::AITargetEvaluator::bestTargetSet(loyalTargeting, loyalTargeting.ownHand.front(),
               [] (const CardId&, PlayerId id) { return id != 2; }).targets == std::vector<PlayerId> {3},
           "advanced targeting preserves the known Lord protection identity modifier");

    auto hiddenIdentityA = ffa; auto hiddenIdentityB = ffa;
    hiddenIdentityA.players[0].identity = PlayerIdentity::Rebel;
    hiddenIdentityB.players[0].identity = PlayerIdentity::Loyalist;
    const auto evalA = ai::AITargetEvaluator::evaluate(hiddenIdentityA, halberdSlash, hiddenIdentityA.players[0], true, 1);
    const auto evalB = ai::AITargetEvaluator::evaluate(hiddenIdentityB, halberdSlash, hiddenIdentityB.players[0], true, 1);
    expect(evalA.finalScore() == evalB.finalScore(), "FFA target score ignores stray hidden-identity values");
}

void identityAIContextRebuildsAcrossLobbyLifecycle()
{
    GameServer server;
    QString error;
    expect(server.listen(error, 0) && server.setTargetPlayerCount(5), "identity AI lifecycle server starts");
    server.testingSetIdentityAssignments({PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade});
    expect(server.startLobbyGame(), "one human plus four AI starts an Identity game");
    const auto identityView = server.session().viewFor(server.session().addClient(2));
    expect(identityView.gameMode == GameMode::Identity && identityView.selfIdentity == PlayerIdentity::Loyalist
               && identityView.players[0].identity == PlayerIdentity::Lord && !identityView.players[2].identity,
           "AI identity context exposes only self and public identities in a live mixed game");
    expect(server.returnToLobby() && server.setTargetPlayerCount(4) && server.startLobbyGame(),
           "returning to Lobby then changing seats starts the next mode cleanly");
    const auto ffaView = server.session().viewFor(server.session().addClient(2));
    expect(ffaView.gameMode == GameMode::FreeForAll && ffaView.selfIdentity == PlayerIdentity::None
               && std::none_of(ffaView.players.begin(), ffaView.players.end(), [](const auto& player) { return player.identity.has_value(); }),
           "second game rebuilds AI context without stale Identity state");
}

void deciderCoversEveryResponseAndSelection()
{
    ai::BasicAIActionDecider decider;
    const auto legal = [] (const CardId&, PlayerId) { return true; };
    const std::vector<std::pair<ResponseType, CardType>> responses {
        {ResponseType::Dodge, CardType::Dodge}, {ResponseType::Slash, CardType::Slash},
        {ResponseType::PeachRescue, CardType::Peach}, {ResponseType::BorrowedSwordSlash, CardType::Slash},
        {ResponseType::Nullification, CardType::Nullification}, {ResponseType::QinglongSlash, CardType::Slash}
    };
    for (const auto& [type, cardType] : responses) {
        auto view = identityDecisionView(PlayerIdentity::None);
        view.gameMode = GameMode::FreeForAll; view.players[0].identity.reset(); view.players[1].identity.reset();
        view.response = ResponseView {type, 41, 1, 2, true, true, {aiCard("response", cardType)}, "", 2};
        if (type == ResponseType::Nullification) view.nullification = NullificationContextView {1, CardType::Duel, 2, 1};
        const auto action = decider.decideAction(view, 0, legal);
        expect(action && std::holds_alternative<RespondAction>(*action)
                   && std::get<RespondAction>(*action).cardId == std::optional<CardId>("response"),
               "every ResponseType has a legal AI response path");
    }
    for (const auto purpose : {CardSelectionPurpose::Dismantlement, CardSelectionPurpose::Snatch,
                               CardSelectionPurpose::Harvest, CardSelectionPurpose::FireAttackReveal,
                               CardSelectionPurpose::FireAttackDiscard, CardSelectionPurpose::AxeDiscard,
                               CardSelectionPurpose::KirinBowMount, CardSelectionPurpose::DoubleSwordDiscard,
                               CardSelectionPurpose::IceSwordPrompt, CardSelectionPurpose::IceSwordDiscard}) {
        CardSelectionView selection {7, purpose, 1, 1, 1, {{9, CardZone::Hand, {}, false, "option", CardType::Dodge}}, ""};
        const auto chosen = decider.decideSelection(identityDecisionView(PlayerIdentity::None), selection);
        expect(chosen == std::vector<SelectionOptionId> {9}, "every required Selection purpose chooses an offered legal option");
    }
    CardSelectionView optional {8, CardSelectionPurpose::FireAttackDiscard, 1, 0, 1, {}, ""};
    expect(decider.decideSelection(identityDecisionView(PlayerIdentity::None), optional).empty(),
           "optional Fire Attack discard safely declines when the authoritative options are empty");
}

void deciderCoversSerpentSpearAndBorrowedSword()
{
    ai::BasicAIActionDecider decider;
    const auto legal = [] (const CardId&, PlayerId) { return true; };
    auto serpent = identityDecisionView(PlayerIdentity::None);
    serpent.gameMode = GameMode::FreeForAll; serpent.players[0].identity.reset(); serpent.players[1].identity.reset();
    serpent.ownHand = {aiCard("spear-a", CardType::Dodge), aiCard("spear-b", CardType::Wine)};
    serpent.serpentSpearSlashTargets = {1};
    serpent.players[1].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)] = aiCard("spear", CardType::Weapon);
    serpent.players[1].equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]->displayName = "丈八蛇矛";
    const auto play = decider.decideAction(serpent, 0, legal);
    expect(play && std::holds_alternative<PlayVirtualSlashAction>(*play)
               && std::get<PlayVirtualSlashAction>(*play).targetIds == std::vector<PlayerId> {1},
           "Serpent Spear play uses only engine-provided legal targets");

    serpent.response = ResponseView {ResponseType::BorrowedSwordSlash, 17, 1, 2, true, true, {}, "", 1};
    const auto response = decider.decideAction(serpent, 0, legal);
    expect(response && std::holds_alternative<RespondVirtualSlashAction>(*response),
           "Serpent Spear answers a required Slash with two actual hand cards");

    auto borrowed = identityDecisionView(PlayerIdentity::None);
    borrowed.gameMode = GameMode::FreeForAll; borrowed.players[0].identity.reset(); borrowed.players[1].identity.reset();
    borrowed.ownHand = {aiCard("borrowed", CardType::BorrowedSword, 2, 2)};
    const auto action = decider.decideAction(borrowed, 0, legal, [] (const CardId&) {
        return std::vector<std::pair<PlayerId, PlayerId>> {{3, 1}};
    });
    expect(action && std::holds_alternative<PlayCardAction>(*action)
               && std::get<PlayCardAction>(*action).targetIds == std::vector<PlayerId> {3, 1},
           "Borrowed Sword uses an authoritative legal holder-target pair");
}

struct SimulationResult {
    int turns {};
    int actions {};
};

SimulationResult runCompleteSimulation(int playerCount, GameMode mode, bool humanFirstSeat, int seed)
{
    std::vector<std::string> names;
    std::vector<PlayerControlType> controls(static_cast<std::size_t>(playerCount), PlayerControlType::AI);
    for (int id = 1; id <= playerCount; ++id) names.push_back((humanFirstSeat && id == 1 ? "Human-" : "AI-") + std::to_string(id));
    if (humanFirstSeat) controls.front() = PlayerControlType::Human;
    std::vector<PlayerIdentity> identities;
    if (mode == GameMode::Identity) {
        identities = playerCount == 5
            ? std::vector<PlayerIdentity> {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel,
                                           PlayerIdentity::Rebel, PlayerIdentity::Renegade}
            : std::vector<PlayerIdentity> {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Loyalist,
                                           PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Rebel,
                                           PlayerIdentity::Rebel, PlayerIdentity::Renegade};
    }
    GameSession session(names, false, identities, controls, mode);
    auto& engine = session.testingEngine();
    engine.testingSetRandomSeed(static_cast<std::uint32_t>(seed));
    session.startGame();

    // The deterministic continuation deck is ordinary Slash cards. They are
    // drawn and used through normal rules; no HP, hand, phase, response, or
    // selection state is modified by the harness.
    for (int index = 0; index < 512; ++index) {
        const auto id = "simulation-slash-" + std::to_string(seed) + "-" + std::to_string(index);
        engine.testingAddToDrawPile(std::make_shared<Card>(id, "Slash", CardType::Slash, Suit::Spade, 7));
    }

    std::vector<std::unique_ptr<ai::AIController>> controllers;
    std::function<void()> scheduleAll;
    int actionCount = 0;
    session.subscribeToGameEvents([&](const GameEvent&) { if (scheduleAll) scheduleAll(); });
    session.subscribeToActions([&] { if (scheduleAll) scheduleAll(); });
    for (PlayerId id = humanFirstSeat ? 2 : 1; id <= playerCount; ++id) {
        controllers.push_back(std::make_unique<ai::AIController>(session, id, [&] { ++actionCount; }));
    }
    scheduleAll = [&] { for (auto& controller : controllers) controller->schedule(); };

    std::optional<SessionClientId> humanClient;
    ai::BasicAIActionDecider humanDriver;
    if (humanFirstSeat) humanClient = session.addClient(1);
    const auto driveHuman = [&] {
        if (!humanClient || engine.gameOver()) return;
        const auto view = session.viewFor(*humanClient);
        if (view.cardSelection) {
            const auto selected = humanDriver.decideSelection(view, *view.cardSelection);
            if (session.submitCardSelection(*humanClient, view.cardSelection->requestId, selected).accepted) ++actionCount;
            return;
        }
        if (!(view.response && view.response->isResponder) && !session.canTakeTurnAction(*humanClient)) return;
        const auto action = humanDriver.decideAction(view, session.requiredDiscardCount(*humanClient),
            [&](const CardId& cardId, PlayerId target) { return session.canPlayCardOnTarget(*humanClient, cardId, target); },
            [&](const CardId& cardId) { return session.borrowedSwordTargetPairs(*humanClient, cardId); },
            [&](const CardId& cardId) { return session.canPlayCard(*humanClient, cardId); });
        if (action && session.submitAction(*humanClient, *action).accepted) ++actionCount;
    };

    scheduleAll();
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 lastProgressMs = 0;
    std::string lastProgress;
    while (!engine.gameOver() && elapsed.elapsed() < 15000 && actionCount < 20000 && engine.turnNumber() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        driveHuman();
        const auto responseId = engine.pendingResponse() ? engine.pendingResponse()->requestId : 0;
        const auto selectionId = engine.pendingCardSelection() ? engine.pendingCardSelection()->requestId : 0;
        const auto current = engine.currentPlayer() ? engine.currentPlayer()->id() : 0;
        const auto progress = std::to_string(actionCount) + ":" + std::to_string(engine.turnNumber()) + ":"
            + std::to_string(current) + ":" + std::to_string(static_cast<int>(engine.currentPhase())) + ":"
            + std::to_string(responseId) + ":" + std::to_string(selectionId);
        if (progress != lastProgress) { lastProgress = progress; lastProgressMs = elapsed.elapsed(); }
        if (elapsed.elapsed() - lastProgressMs > 1000) {
            std::cerr << "AI simulation deadlock: mode=" << static_cast<int>(mode) << " players=" << playerCount
                      << " progress=" << progress << " judgment=" << bool(engine.judgmentContext())
                      << " chain=" << bool(engine.chainDamageContext()) << " dying=" << bool(engine.dyingContext()) << '\n';
            const auto& logs = engine.logEntries();
            for (auto it = logs.end() - std::min<std::size_t>(logs.size(), 20); it != logs.end(); ++it)
                std::cerr << "  " << *it << '\n';
            fail("AI simulation progress watchdog fired");
        }
    }
    expect(engine.gameOver(), "AI simulation reaches GameOver within action/turn/time bounds");
    expect(session.testingRejectedActionCount() == 0, "AI simulation submits no illegal or stale actions");
    const int actionsAtGameOver = actionCount;
    scheduleAll();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    expect(actionCount == actionsAtGameOver, "AI controllers submit no action after GameOver");
    for (auto& controller : controllers) controller->stop();
    return {engine.turnNumber(), actionCount};
}

void completeGameSimulations()
{
    const auto ffa = runCompleteSimulation(4, GameMode::FreeForAll, false, 1304);
    const auto identity5 = runCompleteSimulation(5, GameMode::Identity, false, 1305);
    const auto identity8 = runCompleteSimulation(8, GameMode::Identity, false, 1308);
    std::cout << "Stage 13 complete simulations: 4AI FFA=" << ffa.turns << " turns, 5AI Identity="
              << identity5.turns << " turns, 8AI Identity=" << identity8.turns << " turns\n";
    for (int run = 0; run < 5; ++run) {
        const auto mixed = runCompleteSimulation(8, GameMode::Identity, true, 1370 + run);
        std::cout << "Stage 13 1H+7AI stability " << run + 1 << "/5 PASS (" << mixed.turns << " turns)\n";
    }
}

void humanDuelAgainstAIRespondsWithoutTimeout()
{
    GameServer server;
    QString error;
    expect(server.listen(error, 0) && server.setTargetPlayerCount(2) && server.startLobbyGame(),
           "runtime Duel server starts with one human and one AI");
    auto& engine = server.session().testingEngine();
    clearHand(*engine.players().at(0)); clearHand(*engine.players().at(1));
    engine.players().at(0)->addCard(card("runtime-duel", CardType::Duel));
    engine.players().at(1)->addCard(card("runtime-ai-slash", CardType::Slash));
    GameClientController host(server.session(), 1);
    server.setInteractionTimeoutForTesting(10000);
    expect(host.submit(PlayCardAction {1, "runtime-duel", {2}}).accepted, "human starts a Duel through the real session route");
    expect(waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->responder == 1
                               && engine.pendingResponse()->type == ResponseType::Slash; }, 500),
           "AI answers the Duel before any timeout path and passes the response back to the human");
    expect(std::none_of(engine.players().at(1)->handCards().begin(), engine.players().at(1)->handCards().end(),
                        [] (const auto& item) { return item->id() == "runtime-ai-slash"; }),
           "AI submitted its real Slash with the current authoritative request ID");
}

void humanDuelAgainstAIWithoutSlashPassesImmediately()
{
    GameServer server;
    QString error;
    expect(server.listen(error, 0) && server.startLobbyGame(), "runtime Duel pass server starts");
    auto& engine = server.session().testingEngine();
    clearHand(*engine.players().at(0)); clearHand(*engine.players().at(1));
    engine.players().at(0)->addCard(card("runtime-pass-duel", CardType::Duel));
    GameClientController host(server.session(), 1);
    server.setInteractionTimeoutForTesting(10000);
    expect(host.submit(PlayCardAction {1, "runtime-pass-duel", {2}}).accepted, "human starts Duel against an AI with no Slash");
    expect(waitFor([&] { return !engine.pendingResponse() && engine.players().at(1)->hp() == 3; }, 500),
           "AI immediately submits a legal Duel pass rather than waiting for timeout");
}

void humanDuelAgainstAISerpentSpearRespondsWithoutTimeout()
{
    GameServer server;
    QString error;
    expect(server.listen(error, 0) && server.startLobbyGame(), "runtime Serpent Spear Duel server starts");
    auto& engine = server.session().testingEngine();
    clearHand(*engine.players().at(0)); clearHand(*engine.players().at(1));
    engine.players().at(0)->addCard(card("runtime-spear-duel", CardType::Duel));
    engine.players().at(1)->addCard(card("runtime-spear-a", CardType::Dodge));
    engine.players().at(1)->addCard(card("runtime-spear-b", CardType::Wine));
    engine.testingEquip(2, std::make_shared<Card>("runtime-spear", "丈八蛇矛", CardType::Weapon, Suit::Spade, 12,
                                                   EquipmentData {EquipmentSlot::Weapon, 3}));
    GameClientController host(server.session(), 1);
    server.setInteractionTimeoutForTesting(10000);
    expect(host.submit(PlayCardAction {1, "runtime-spear-duel", {2}}).accepted, "human starts Duel against Serpent Spear AI");
    expect(waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->responder == 1
                               && engine.players().at(1)->handCards().empty(); }, 500),
           "AI uses the existing virtual Slash response through the runtime dispatch path");
}

void consecutiveDuelAndAOEResponsesAreRedriven()
{
    GameServer server;
    QString error;
    expect(server.listen(error, 0) && server.startLobbyGame(), "continuous runtime response server starts");
    auto& engine = server.session().testingEngine();
    clearHand(*engine.players().at(0)); clearHand(*engine.players().at(1));
    engine.players().at(0)->addCard(card("runtime-chain-duel", CardType::Duel));
    engine.players().at(0)->addCard(card("runtime-human-slash", CardType::Slash));
    engine.players().at(0)->addCard(card("runtime-barbarian", CardType::BarbarianInvasion));
    engine.players().at(1)->addCard(card("runtime-ai-first", CardType::Slash));
    engine.players().at(1)->addCard(card("runtime-ai-second", CardType::Slash));
    engine.players().at(1)->addCard(card("runtime-ai-aoe", CardType::Slash));
    GameClientController host(server.session(), 1);
    server.setInteractionTimeoutForTesting(10000);
    expect(host.submit(PlayCardAction {1, "runtime-chain-duel", {2}}).accepted, "human starts a multi-response Duel");
    expect(waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->responder == 1; }, 500),
           "first AI Duel response is dispatched");
    const auto humanRequest = engine.pendingResponse()->requestId;
    expect(host.submit(RespondAction {1, humanRequest, "runtime-human-slash"}).accepted, "human continues the Duel with Slash");
    expect(waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->responder == 1
                               && engine.players().at(1)->handCards().size() == 1; }, 500),
           "second AI Duel response receives a new runtime dispatch");
    expect(host.submit(RespondAction {1, engine.pendingResponse()->requestId, std::nullopt}).accepted, "human ends the completed Duel");
    expect(host.submit(PlayCardAction {1, "runtime-barbarian", {}}).accepted, "human starts a non-Duel response interaction");
    expect(waitFor([&] { return !engine.pendingResponse() && engine.players().at(1)->hp() == 4; }, 500),
           "AOE Slash response uses the same generic runtime dispatch");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    aiActionPacingCoverage();
    runAIIdentityTests();
    identityObjectiveUsesOnlyVisibleIdentityData();
    identityObjectivesModifyOnlyLegalGenericChoices();
    threatEvaluatorUsesOnlyPublicDeterministicState();
    threatSystemIntegratesByActionIntent();
    resourcePlannerValuesCardsAndActions();
    resourcePlannerChoosesSelectionCosts();
    advancedTargetEvaluatorScoresActionContext();
    advancedTargetEvaluatorScoresSetsAndGlobalEffects();
    identityAIContextRebuildsAcrossLobbyLifecycle();
    deciderCoversEveryResponseAndSelection();
    deciderCoversSerpentSpearAndBorrowedSword();
    completeGameSimulations();
    humanDuelAgainstAIRespondsWithoutTimeout();
    humanDuelAgainstAIWithoutSlashPassesImmediately();
    humanDuelAgainstAISerpentSpearRespondsWithoutTimeout();
    consecutiveDuelAndAOEResponsesAreRedriven();
    GameServer server;
    QString error;
    expect(server.listen(error, 0), "AI test server starts");
    expect(server.setTargetPlayerCount(2), "two-seat lobby is configured");

    const auto initialLobby = server.lobbyState();
    const auto aiEntry = std::find_if(initialLobby.players.begin(), initialLobby.players.end(), [](const auto& player) { return player.ai; });
    expect(initialLobby.aiPlayerCount == 1 && aiEntry != initialLobby.players.end() && aiEntry->playerId == 2 && aiEntry->connected,
           "AI occupies an online lobby seat with an AI flag");
    expect(server.addAI() && server.aiPlayerCount() == 2 && server.removeAI() && server.aiPlayerCount() == 1,
           "host can add and remove only AI lobby capacity");

    expect(server.startLobbyGame(), "human plus AI starts a game");
    GameClientController host(server.session(), 1);
    auto& engine = server.session().testingEngine();
    expect(engine.players().at(1)->controlType() == PlayerControlType::AI && engine.gameMode() == GameMode::FreeForAll, "AI control type and FreeForAll mode are authoritative in game state");
    const auto aiView = server.session().viewFor(server.session().addClient(2));
    expect(aiView.gameMode == GameMode::FreeForAll && aiView.selfIdentity == PlayerIdentity::None
               && std::none_of(aiView.players.begin(), aiView.players.end(), [](const auto& player) { return player.identity.has_value(); }),
           "AI reads authoritative FreeForAll mode without hidden identity data");

    // Isolate scheduling from random card choice: the empty AI must take its
    // short Pass path and return control without blocking the event loop.
    clearHand(*engine.players().at(0)); clearHand(*engine.players().at(1));
    server.setInteractionTimeoutForTesting(100);
    expect(host.submit(EndPlayPhaseAction {1}).accepted, "human ends opening Play phase");
    const bool aiTurnCompleted = waitFor([&] { return engine.currentPlayer() && engine.currentPlayer()->id() == 1
        && engine.currentPhase() == Phase::Play && engine.turnNumber() >= 2; });
    if (!aiTurnCompleted) {
        std::cerr << "AI turn diagnostic: turn=" << engine.turnNumber() << " phase=" << static_cast<int>(engine.currentPhase())
                  << " player=" << (engine.currentPlayer() ? engine.currentPlayer()->id() : 0)
                  << " gameOver=" << engine.gameOver() << " pendingResponse=" << bool(engine.pendingResponse()) << '\n';
    }
    expect(aiTurnCompleted, "AI receives and completes its turn without blocking the event loop");

    clearHand(*engine.players().at(0)); clearHand(*engine.players().at(1));
    // This subcase tests an ordinary Slash/Dodge exchange. The preceding live
    // AI turn may now finish equipping a defensive horse or armor, so restore
    // its stated range/armor fixture instead of depending on the random draw.
    for (const auto slot : {EquipmentSlot::Weapon, EquipmentSlot::Armor, EquipmentSlot::OffensiveHorse, EquipmentSlot::DefensiveHorse})
        engine.testingRemoveEquipment(2, slot);
    engine.players().at(0)->addCard(card("ai-test-slash", CardType::Slash));
    engine.players().at(1)->addCard(card("ai-test-dodge", CardType::Dodge));
    const auto openingSlash = host.submit(PlayCardAction {1, "ai-test-slash", {2}});
    if (!openingSlash.accepted) std::cerr << "Opening Slash rejected: " << openingSlash.message
        << " distance=" << engine.distanceBetween(1, 2) << " range=" << engine.attackRange(1) << '\n';
    expect(openingSlash.accepted, "human opens a legal Slash response");
    expect(waitFor([&] { return !engine.pendingResponse() && engine.players().at(1)->hp() == 4
                               && std::none_of(engine.players().at(1)->handCards().begin(), engine.players().at(1)->handCards().end(), [](const auto& item) { return item->id() == "ai-test-dodge"; })
                               && engine.currentPlayer() && engine.currentPlayer()->id() == 1 && engine.currentPhase() == Phase::Play; }),
           "AI submits its own current requestId to play Dodge");

    engine.players().at(0)->addCard(card("ai-test-ex", CardType::ExNihilo));
    engine.players().at(1)->addCard(card("ai-test-null", CardType::Nullification));
    expect(host.submit(PlayCardAction {1, "ai-test-ex", {}}).accepted, "human opens a Nullification request");
    expect(waitFor([&] { return !engine.pendingResponse() && std::any_of(engine.players().at(1)->handCards().begin(), engine.players().at(1)->handCards().end(), [](const auto& item) { return item->id() == "ai-test-null"; }); }),
           "AI preserves Nullification against another player's beneficial effect");

    engine.testingKillPlayer(2);
    expect(waitFor([&] { return engine.gameOver(); }), "test reaches game over");
    expect(server.returnToLobby(), "return to lobby clears AI controllers and game state");
    expect(!server.gameStarted() && server.lobbyState().aiPlayerCount == 1, "AI lobby state survives cleanup without reconnect state");
    expect(server.startLobbyGame(), "second game recreates AI lifecycle");
    GameClientController secondHost(server.session(), 1);
    clearHand(*server.session().testingEngine().players().at(0));
    server.setInteractionTimeoutForTesting(1000);
    expect(secondHost.submit(EndPlayPhaseAction {1}).accepted, "second-game human ends Play");
    expect(waitFor([&] { return server.session().testingEngine().turnNumber() >= 2; }), "AI runs again in the second game");

    std::cout << "BasicSanguoshaAITests PASS\n";
}
