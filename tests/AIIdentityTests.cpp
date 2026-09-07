#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include "ai/AIActionDecider.h"
#include "ai/AIHeuristics.h"
#include "ai/AIIdentityObjective.h"
#include "ai/AIIdentityInference.h"
#include "ai/AIIdentityStrategy.h"
#include "ai/AITargetEvaluator.h"
#include "ai/AIThreatEvaluator.h"
#include "client/GameClientController.h"
#include "network/GameServer.h"

using namespace sanguosha;
using namespace sanguosha::network;
namespace {
void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool waitUntil(const std::function<bool()>& condition, int ms = 3000)
{
    if (condition()) return true;
    QEventLoop loop; QTimer poll, deadline; deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(1); deadline.start(ms); loop.exec(); return condition();
}
void emptyHand(Player& p)
{
    auto cards = p.handCards();
    for (const auto& c : cards) p.removeCard(c->id());
}
std::shared_ptr<Card> makeCard(std::string id, CardType type)
{
    return std::make_shared<Card>(id, id, type, Suit::Heart, 7);
}
CardView cv(const char* id, CardType type, int min = 1, int max = 1)
{
    return {id, type, Suit::Heart, 7, id, {}, min, max};
}
const std::vector<PlayerIdentity> roles {PlayerIdentity::Lord, PlayerIdentity::Loyalist,
    PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade};
PlayerViewState policyView(PlayerIdentity role)
{
    GameSession s({"Lord", "Loyalist", "Rebel", "Rebel", "Renegade"}, true, roles, {}, GameMode::Identity);
    auto v = s.viewFor(s.addClient(2));
    v.selfIdentity = role; v.players[1].identity = role;
    v.currentTurnPlayer = 2; v.currentPhase = Phase::Play;
    for (auto& p : v.players) { p.hp = 3; p.handCardCount = 2; }
    v.ownHand = {cv("attack", CardType::Slash)};
    return v;
}
void privacyAndGoals()
{
    ai::BasicAIActionDecider decider;
    for (PlayerId self : {1, 2, 3, 5}) {
        GameSession s({"L", "Y", "R", "R", "N"}, true, roles, {}, GameMode::Identity);
        auto& e = s.testingEngine();
        const auto client = s.addClient(self);
        auto before = s.viewFor(client);
        check(before.selfIdentity == roles[self - 1], "all four roles see their own actual identity");
        ai::AIIdentityObjective goal(before);
        check(goal.active() && goal.selfIdentity() == roles[self - 1] && goal.lordPlayerId() == 1,
              "all four objectives receive public Lord and own identity");
        check(std::string(goal.victoryCondition()).find(self == 5 ? "sole survivor" : self == 3 ? "Eliminate the Lord" : "every Rebel and Renegade") != std::string::npos,
              "each role states its complete rule victory condition");
        std::vector<PlayerId> hidden;
        for (const auto& p : before.players) if (p.id != self && p.id != 1) {
            check(!p.identity, "hidden Loyalist, Rebel and Renegade are absent"); hidden.push_back(p.id);
        }
        const auto first = e.players()[hidden.front() - 1]->identity();
        for (std::size_t i = 0; i + 1 < hidden.size(); ++i)
            e.testingSetIdentity(hidden[i], e.players()[hidden[i + 1] - 1]->identity());
        e.testingSetIdentity(hidden.back(), first);
        const auto after = s.viewFor(client);
        check(encodeViewState(before) == encodeViewState(after), "changing hidden actual identities leaves the complete serialized AI view identical");
        before.currentTurnPlayer = self; before.currentPhase = Phase::Play;
        before.ownHand = {cv("attack", CardType::Slash)};
        auto comparison = after; comparison.currentTurnPlayer = self; comparison.currentPhase = Phase::Play; comparison.ownHand = before.ownHand;
        const auto legal = [self](const CardId&, PlayerId id) { return id != self; };
        const auto a = decider.decideAction(before, 0, legal), b = decider.decideAction(comparison, 0, legal);
        check(a && b && encodeAction(*a) == encodeAction(*b), "hidden identity permutations cannot change the complete AI action");
    }
    auto ffa = policyView(PlayerIdentity::Loyalist); ffa.gameMode = GameMode::FreeForAll;
    auto neutral = ffa; neutral.selfIdentity = PlayerIdentity::None;
    for (auto& p : neutral.players) p.identity.reset();
    const auto legal = [](const CardId&, PlayerId id) { return id != 2; };
    check(!ai::AIIdentityObjective(ffa).active()
          && encodeAction(*decider.decideAction(ffa, 0, legal)) == encodeAction(*decider.decideAction(neutral, 0, legal)),
          "FFA bypasses identity policy even with stray role data");
}
void targetAndResponseBoundaries()
{
    ai::BasicAIActionDecider d;
    const auto onlyLord = [](const CardId&, PlayerId id) { return id == 1; };
    auto loyal = policyView(PlayerIdentity::Loyalist);
    for (auto type : {CardType::Slash, CardType::FireSlash, CardType::ThunderSlash, CardType::Duel,
                     CardType::Dismantlement, CardType::Snatch, CardType::Indulgence, CardType::SupplyShortage,
                     CardType::FireAttack, CardType::Lightning, CardType::IronChain}) {
        loyal.ownHand = {cv("attack", type)};
        check(std::holds_alternative<EndPlayPhaseAction>(*d.decideAction(loyal, 0, onlyLord)),
              "Loyalist ends Play when only Lord is a hostile target");
    }
    loyal.ownHand = {cv("aoe", CardType::ArrowBarrage, 0, 0)};
    check(std::holds_alternative<EndPlayPhaseAction>(*d.decideAction(loyal, 0, onlyLord)), "Loyalist declines AOE that hits Lord");
    loyal.ownHand = {cv("borrow", CardType::BorrowedSword, 2, 2)};
    check(std::holds_alternative<EndPlayPhaseAction>(*d.decideAction(loyal, 0, onlyLord,
        [](const CardId&) { return std::vector<std::pair<PlayerId, PlayerId>>{{3, 1}, {1, 3}}; })), "Loyalist protects both Borrowed Sword endpoints");
    loyal.ownHand = {cv("a", CardType::Dodge), cv("b", CardType::Dodge)};
    loyal.serpentSpearSlashTargets = {1};
    check(std::holds_alternative<EndPlayPhaseAction>(*d.decideAction(loyal, 0, onlyLord)), "virtual Slash cannot bypass Lord protection");
    loyal.serpentSpearSlashTargets.clear();
    loyal.ownHand = {cv("heal", CardType::PeachGarden, 0, 0), cv("attack", CardType::Slash)};
    const auto legal = [](const CardId&, PlayerId id) { return id != 2; };
    check(std::get<PlayCardAction>(*d.decideAction(loyal, 0, legal)).cardId == "heal", "beneficial group healing prioritizes injured Lord");
    for (auto type : {CardType::Slash, CardType::Duel, CardType::Dismantlement, CardType::Snatch, CardType::Indulgence}) {
        auto rebel = policyView(PlayerIdentity::Rebel);
        rebel.ownHand = {cv("a", type), cv("b", CardType::Slash), cv("c", CardType::Slash)};
        check(std::get<PlayCardAction>(*d.decideAction(rebel, 0, legal)).targetIds == std::vector<PlayerId>{1}, "Rebel prioritizes legal Lord across hostile cards");
        check(std::get<PlayCardAction>(*d.decideAction(rebel, 0, [](const CardId&, PlayerId id) { return id == 3; })).targetIds == std::vector<PlayerId>{3}, "identity bias never bypasses range or legality");
    }
    for (auto role : {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade}) {
        auto v = policyView(role);
        v.players[1].hp = 1;
        v.ownHand = {cv("attack", CardType::Slash), cv("heal", CardType::Peach, 0, 0)};
        check(std::get<PlayCardAction>(*d.decideAction(v, 0, legal)).cardId == "heal", "every identity prioritizes urgent self survival over target bias");
        v.response = ResponseView {ResponseType::PeachRescue, 10, 1, 2, true, true, {cv("peach", CardType::Peach)}, "", 1};
        check(bool(std::get<RespondAction>(*d.decideAction(v, 0, legal)).cardId) == (role == PlayerIdentity::Loyalist), "only known Loyalist protects another Lord with Peach");
        v.response->targetId = 2; v.response->requester = 2;
        v.response->selectableCards = {cv("wine", CardType::Wine), cv("peach", CardType::Peach)};
        check(std::get<RespondAction>(*d.decideAction(v, 0, legal)).cardId == "wine", "every role self-rescues with legal Wine and preserves Peach");
        v.response->type = ResponseType::Nullification; v.response->selectableCards = {cv("null", CardType::Nullification)};
        v.nullification = NullificationContextView {3, CardType::Duel, 1, 1};
        check(bool(std::get<RespondAction>(*d.decideAction(v, 0, legal)).cardId) == (role == PlayerIdentity::Loyalist), "only Loyalist nullifies harm to Lord");
        v.nullification->chainRound = 2;
        check(!std::get<RespondAction>(*d.decideAction(v, 0, legal)).cardId, "do not reverse protection with a second Nullification");
        v.nullification->trickType = CardType::PeachGarden;
        check(bool(std::get<RespondAction>(*d.decideAction(v, 0, legal)).cardId) == (role == PlayerIdentity::Loyalist), "restore cancelled beneficial effect on Lord");
        v.nullification->targetId = 2; v.nullification->trickType = CardType::Duel; v.nullification->chainRound = 1;
        check(std::get<RespondAction>(*d.decideAction(v, 0, legal)).cardId.has_value(), "all identities protect themselves");
        v.nullification.reset(); v.response->type = ResponseType::Dodge;
        v.response->selectableCards = {cv("dodge", CardType::Dodge)};
        check(std::get<RespondAction>(*d.decideAction(v, 0, legal)).cardId == "dodge", "all identities retain legal AOE defense");
        CardSelectionView selection {11, CardSelectionPurpose::Harvest, 2, 1, 1,
            {{1, CardZone::Hand, {}, false, "Peach", CardType::Peach}, {2, CardZone::Hand, {}, false, "Slash", CardType::Slash}}, ""};
        check(d.decideSelection(v, selection) == std::vector<SelectionOptionId>{1}, "identity selection preserves survival-oriented keep values");
    }
    auto renegade = policyView(PlayerIdentity::Renegade);
    check(std::get<PlayCardAction>(*d.decideAction(renegade, 0, legal)).targetIds != std::vector<PlayerId>{1}, "Renegade avoids ending game in another side's victory");
    check(std::holds_alternative<EndPlayPhaseAction>(*d.decideAction(renegade, 0, onlyLord)), "Renegade safely declines early Lord-only attack");
    for (auto& p : renegade.players) if (p.id != 1 && p.id != 2) p.alive = false;
    check(std::holds_alternative<PlayCardAction>(*d.decideAction(renegade, 0, onlyLord)), "Renegade can attack Lord in final duel without stalemate");
    renegade.players[2].alive = true;
    const auto tactical = ai::targetValue(renegade.players[2], renegade) + ai::AIThreatEvaluator::threatScore(renegade.players[2]) / 4;
    check(ai::AIIdentityObjective(renegade).hostileTargetBias(3) == 0 && tactical > 0, "identity adjustment preserves existing unknown-target tactical score");
}
void realRescueViews()
{
    for (auto role : {PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade}) {
        auto assigned = roles; assigned[1] = role;
        GameSession s({"L", "AI", "R", "R", "N"}, true, assigned, {}, GameMode::Identity);
        auto& e = s.testingEngine();
        for (const auto& p : e.players()) emptyHand(*p);
        e.players()[1]->addCard(makeCard("rescue", CardType::Peach));
        e.testingSetHp(1, 1); e.testingApplyDamage(Damage {3, 1, 1});
        const auto lordClient = s.addClient(1), aiClient = s.addClient(2);
        check(e.pendingResponse() && e.pendingResponse()->responder == 1, "dying Lord gets first self rescue");
        check(s.submitAction(lordClient, RespondAction {1, e.pendingResponse()->requestId, {}}).accepted, "Lord declines absent self rescue");
        const auto view = s.viewFor(aiClient);
        check(view.response && view.response->isResponder && view.response->targetId == 1, "real filtered rescue view carries dying Lord ID");
        ai::BasicAIActionDecider d;
        auto action = d.decideAction(view, 0, [](const CardId&, PlayerId) { return false; });
        check(action && s.submitAction(aiClient, *action).accepted, "identity rescue is a legal engine response");
        check((e.players()[0]->hp() == 1) == (role == PlayerIdentity::Loyalist), "only Loyalist actually heals Lord");
    }
}
void mixedRounds(int count)
{
    GameServer server; QString error;
    check(server.listen(error, 0) && server.setTargetPlayerCount(count), "mixed Identity server starts");
    auto assigned = roles;
    while (int(assigned.size()) < count) assigned.insert(assigned.end() - 1, PlayerIdentity::Rebel);
    if (count == 8) assigned[5] = PlayerIdentity::Loyalist;
    server.testingSetIdentityAssignments(assigned);
    check(server.startLobbyGame(), "1 Human plus 4/5/7 AI starts Identity");
    auto& s = server.session(); auto& e = s.testingEngine();
    server.setInteractionTimeoutForTesting(60000);
    const auto host = s.addClient(1);
    std::vector<SessionClientId> viewers;
    for (PlayerId id = 1; id <= count; ++id) {
        viewers.push_back(s.addClient(id)); emptyHand(*e.players()[id - 1]);
    }
    // A deterministic defensive draw stream keeps every seat alive for three
    // table rounds while exercising normal draw/discard and the opening plays.
    // Production AI receives only the resulting private hand, never this deck.
    for (int i = 0; i < count * 8; ++i) e.testingAddToDrawPile(makeCard("smoke-dodge-" + std::to_string(i), CardType::Dodge));
    e.players()[1]->addCard(makeCard("multi-ex", CardType::ExNihilo));
    e.players()[1]->addCard(makeCard("multi-wine-a", CardType::Wine));
    e.players()[1]->addCard(makeCard("multi-wine-b", CardType::Wine));
    e.players()[1]->addCard(makeCard("multi-slash", CardType::Slash));
    ai::BasicAIActionDecider d;
    int turns = 0; std::set<int> completedSeats, endedTurns;
    s.subscribeToGameEvents([&](const GameEvent& event) {
        if (event.type == GameEventType::TurnEnded) {
            check(endedTurns.insert(e.turnNumber()).second, "no turn settles twice");
            ++turns; if (event.source) completedSeats.insert(*event.source);
        }
    });
    const bool progressed = waitUntil([&] {
        for (const auto client : viewers) {
            const auto v = s.viewFor(client);
            for (const auto& p : v.players)
                check(p.id == v.selfPlayerId || p.id == 1 || !p.alive || !p.identity, "ongoing mixed game has no hidden identity leak");
        }
        const auto v = s.viewFor(host);
        if (v.cardSelection) check(s.submitCardSelection(host, v.cardSelection->requestId, d.decideSelection(v, *v.cardSelection)).accepted, "human legal selection");
        else if (v.response && v.response->isResponder) check(s.submitAction(host, RespondAction {1, v.response->requestId, {}}).accepted, "human legal response");
        else if (s.canTakeTurnAction(host)) {
            if (v.currentPhase == Phase::Play) check(s.submitAction(host, EndPlayPhaseAction {1}).accepted, "human ends Play");
            else check(s.submitAction(host, *d.decideAction(v, s.requiredDiscardCount(host), [](const CardId&, PlayerId) { return false; })).accepted, "human legal discard");
        }
        return turns >= count * 3;
    }, 10000);
    if (!progressed) {
        const auto stalled = s.viewFor(host);
        std::cerr << "Stalled turn=" << stalled.turnNumber << " player=" << stalled.currentTurnPlayer
                  << " phase=" << int(stalled.currentPhase) << " turns=" << turns
                  << " rejected=" << s.testingRejectedActionCount() << '\n';
        for (const auto& line : stalled.visibleLogs) std::cerr << line << '\n';
    }
    check(progressed, "mixed Identity completes three full table rounds without timeout");
    check(int(completedSeats.size()) == count && s.testingRejectedActionCount() == 0, "every seat completed turns with zero rejected or stale actions");
    const auto v = s.viewFor(host);
    check(std::none_of(v.visibleLogs.begin(), v.visibleLogs.end(), [](const std::string& line) { return line.find("timed out") != std::string::npos; }), "no timeout masks stalled AI");
    check(server.returnToLobby(), "mixed game returns to Lobby");
    check(!server.gameStarted() && !s.testingEngine().pendingResponse() && !s.testingEngine().pendingCardSelection(), "Lobby clears interactions and controllers");
    assigned[1] = PlayerIdentity::Rebel; assigned[2] = PlayerIdentity::Loyalist;
    server.testingSetIdentityAssignments(assigned); check(server.startLobbyGame(), "second Identity game starts");
    check(ai::AIIdentityObjective(s.viewFor(viewers[1])).selfIdentity() == PlayerIdentity::Rebel, "second game rebuilds changed own identity");
    std::cout << "Identity mixed 1+" << count - 1 << " AI: three rounds PASS\n";
}
void identityReconnect()
{
    GameServer server; QString error;
    check(server.listen(error, 0) && server.setTargetPlayerCount(5), "Identity reconnect server starts");
    server.testingSetIdentityAssignments(roles);
    NetworkGameClient remote("127.0.0.1", server.serverPort(), "Reconnect");
    remote.connectToHost(); check(waitUntil([&] { return remote.selfPlayerId() == 2; }), "remote claims second seat");
    check(server.startLobbyGame(), "mixed network Identity starts");
    check(waitUntil([&] { return remote.view().selfIdentity == PlayerIdentity::Loyalist; }), "remote receives filtered own identity");
    const auto aiClient = server.session().addClient(3);
    const auto before = server.session().viewFor(aiClient);
    remote.disconnectForReconnect(); check(waitUntil([&] { return server.testingReconnectReservationCount() == 1; }), "Identity reconnect reservation exists");
    remote.reconnectToHost();
    check(waitUntil([&] { return remote.connected() && server.testingReconnectReservationCount() == 0; }), "Identity reconnect succeeds");
    check(remote.view().selfIdentity == PlayerIdentity::Loyalist && !remote.view().players[2].identity, "reconnect restores only authorized identities");
    auto after = server.session().viewFor(aiClient);
    check(before.selfIdentity == after.selfIdentity && !after.players[1].identity, "AI objective unchanged and remote role still hidden after reconnect");
    GameClientController host(server.session(), 1); emptyHand(*server.session().testingEngine().players()[0]);
    check(host.submit(EndPlayPhaseAction {1}).accepted, "reconnected game progresses to human seat");
    check(waitUntil([&] { return remote.view().currentTurnPlayer == 2 && remote.view().currentPhase == Phase::Play; }), "reconnected human gets turn");
    emptyHand(*server.session().testingEngine().players()[1]);
    remote.submit(EndPlayPhaseAction {2});
    check(waitUntil([&] { return server.session().testingEngine().turnNumber() >= 3; }), "AI controllers remain active after reconnect");
}
void identityGameOver()
{
    for (const auto side : {WinningSide::LordSide, WinningSide::RebelSide, WinningSide::Renegade}) {
        GameSession s({"L", "Y", "R", "R", "N"}, true, roles, {}, GameMode::Identity);
        auto& e = s.testingEngine();
        if (side == WinningSide::LordSide) for (PlayerId id : {3, 4, 5}) e.testingKillPlayer(id);
        else if (side == WinningSide::RebelSide) e.testingKillPlayer(1);
        else { for (PlayerId id : {2, 3, 4}) e.testingKillPlayer(id); e.testingKillPlayer(1); }
        ai::BasicAIActionDecider d;
        for (PlayerId id = 1; id <= 5; ++id) {
            const auto v = s.viewFor(s.addClient(id));
            check(v.gameOver && v.winningSide == side, "Identity GameOver keeps formal winning side");
            check(!d.decideAction(v, 0, [](const CardId&, PlayerId) { return true; }), "all identities stop submitting at GameOver");
        }
    }
}

void identityInferenceCoverage()
{
    const auto event = [](GameEventType type, PlayerId source, std::optional<PlayerId> target, const char* detail) {
        return GameEvent {type, source, target, detail};
    };
    auto view = policyView(PlayerIdentity::Lord);
    view.selfPlayerId = 1; view.selfIdentity = PlayerIdentity::Lord;
    view.players[0].identity = PlayerIdentity::Lord;
    for (std::size_t index = 1; index < view.players.size(); ++index) view.players[index].identity.reset();
    view.publicEvents.clear();
    ai::AIIdentityInference inference;
    inference.rebuild(view);
    check(inference.enabled() && inference.beliefFor(1)->confirmedIdentity == PlayerIdentity::Lord,
          "Lord belief is confirmed from public view");
    check(inference.beliefFor(2)->inferred == ai::AIInferredIdentity::Unknown
              && inference.beliefFor(2)->unknownScore == 100,
          "initial hidden players remain Unknown");

    auto attacks = view;
    attacks.publicEvents = {
        event(GameEventType::CardUsed, 2, 1, "Slash"), event(GameEventType::DamageReceived, 2, 1, "1"),
        event(GameEventType::CardUsed, 2, 1, "Duel"), event(GameEventType::CardUsed, 2, 1, "Fire Attack")};
    inference.rebuild(attacks);
    const auto repeatedAttack = *inference.beliefFor(2);
    check(repeatedAttack.rebelScore > 0 && repeatedAttack.loyalistScore < 0
              && repeatedAttack.inferred == ai::AIInferredIdentity::Rebel
              && repeatedAttack.confidence == ai::AIInferenceConfidence::High
              && repeatedAttack.rebelScore <= 100,
          "repeated attacks and damage to Lord raise Rebel tendency and confidence without hard confirmation");
    auto decayed = attacks;
    decayed.publicEvents.push_back(event(GameEventType::TurnEnded, 2, {}, ""));
    inference.rebuild(decayed);
    check(inference.beliefFor(2)->rebelScore < repeatedAttack.rebelScore,
          "turn-end decay moves old bounded evidence slightly toward neutral");

    auto heals = view;
    heals.publicEvents = {event(GameEventType::CardResponded, 2, 1, "Peach"),
                          event(GameEventType::CardResponded, 2, 1, "Peach")};
    inference.rebuild(heals);
    check(inference.beliefFor(2)->loyalistScore > inference.beliefFor(2)->rebelScore
              && inference.beliefFor(2)->inferred == ai::AIInferredIdentity::Loyalist,
          "rescuing Lord raises Loyalist tendency");

    auto nullification = view;
    nullification.publicEvents = {event(GameEventType::NullificationContext, 3, 1, "Harmful:0"),
                                  event(GameEventType::CardResponded, 2, 3, "Nullification"),
                                  event(GameEventType::NullificationContext, 3, 1, "Harmful:1"),
                                  event(GameEventType::CardResponded, 4, 3, "Nullification")};
    inference.rebuild(nullification);
    check(inference.beliefFor(2)->loyalistScore > 0 && inference.beliefFor(4)->rebelScore > 0,
          "Nullification parity distinguishes protecting Lord from countering that protection");
    auto beneficialNullification = view;
    beneficialNullification.publicEvents = {event(GameEventType::NullificationContext, 1, 1, "Beneficial:0"),
                                             event(GameEventType::CardResponded, 3, 1, "Nullification"),
                                             event(GameEventType::NullificationContext, 1, 1, "Beneficial:1"),
                                             event(GameEventType::CardResponded, 2, 1, "Nullification")};
    inference.rebuild(beneficialNullification);
    check(inference.beliefFor(3)->rebelScore > 0 && inference.beliefFor(2)->loyalistScore > 0,
          "beneficial-effect Nullification parity marks cancellation hostile and restoration protective");

    auto dismantleArmor = view;
    dismantleArmor.publicEvents = {event(GameEventType::PublicCardMoved, 2, 1, "Dismantlement:Equipment:八卦阵")};
    inference.rebuild(dismantleArmor);
    check(inference.beliefFor(2)->rebelScore > 0, "removing Lord equipment is hostile evidence");
    auto dismantleDelay = view;
    dismantleDelay.publicEvents = {event(GameEventType::PublicCardMoved, 2, 1, "Dismantlement:Judgment:Indulgence")};
    inference.rebuild(dismantleDelay);
    check(inference.beliefFor(2)->loyalistScore > 0, "removing Lord Indulgence is protective evidence");

    auto delayed = view;
    delayed.publicEvents = {event(GameEventType::CardUsed, 2, 1, "Indulgence"),
                            event(GameEventType::CardUsed, 2, 1, "Supply Shortage")};
    inference.rebuild(delayed);
    check(inference.beliefFor(2)->rebelScore >= 80 && inference.beliefFor(2)->loyalistScore < 0,
          "applying bad delayed tricks to Lord is strong Rebel evidence");

    auto mixed = view;
    mixed.publicEvents = {event(GameEventType::CardUsed, 2, 1, "Slash"),
                          event(GameEventType::CardResponded, 2, 1, "Peach")};
    inference.rebuild(mixed);
    const auto mixedBelief = *inference.beliefFor(2);
    check(mixedBelief.inferred == ai::AIInferredIdentity::Renegade
              || mixedBelief.inferred == ai::AIInferredIdentity::Unknown,
          "contradictory Lord attack and rescue does not permanently classify the actor as Rebel");
    check(mixedBelief.renegadeScore > 0 && mixedBelief.confidence != ai::AIInferenceConfidence::High,
          "mixed behavior creates Renegade tendency while reducing confidence");

    auto hiddenSwapA = view;
    auto hiddenSwapB = view;
    hiddenSwapA.publicEvents = hiddenSwapB.publicEvents = attacks.publicEvents;
    hiddenSwapA.players[1].identity.reset(); hiddenSwapA.players[2].identity.reset();
    hiddenSwapB.players[1].identity.reset(); hiddenSwapB.players[2].identity.reset();
    ai::AIIdentityInference inferenceA, inferenceB;
    inferenceA.rebuild(hiddenSwapA); inferenceB.rebuild(hiddenSwapB);
    check(inferenceA.beliefFor(2)->rebelScore == inferenceB.beliefFor(2)->rebelScore
              && inferenceA.beliefFor(2)->inferred == inferenceB.beliefFor(2)->inferred,
          "hidden identity permutation cannot affect public-event inference");
    hiddenSwapA.players[1].handCardCount = hiddenSwapB.players[1].handCardCount = 3;
    inferenceA.rebuild(hiddenSwapA); inferenceB.rebuild(hiddenSwapB);
    check(inferenceA.beliefFor(2)->rebelScore == inferenceB.beliefFor(2)->rebelScore,
          "unchanged public hand count hides all opponent hand-face differences from inference");

    auto swappedRoles = roles;
    std::swap(swappedRoles[1], swappedRoles[2]);
    GameSession hiddenIdentityGameA({"L", "P2", "P3", "P4", "P5"}, true, roles, {}, GameMode::Identity);
    GameSession hiddenIdentityGameB({"L", "P2", "P3", "P4", "P5"}, true, swappedRoles, {}, GameMode::Identity);
    auto realSwapA = hiddenIdentityGameA.viewFor(hiddenIdentityGameA.addClient(1));
    auto realSwapB = hiddenIdentityGameB.viewFor(hiddenIdentityGameB.addClient(1));
    realSwapA.publicEvents = realSwapB.publicEvents = attacks.publicEvents;
    inferenceA.rebuild(realSwapA); inferenceB.rebuild(realSwapB);
    check(!realSwapA.players[1].identity && !realSwapB.players[1].identity
              && inferenceA.beliefFor(2)->rebelScore == inferenceB.beliefFor(2)->rebelScore,
          "swapping actual hidden identities between real sessions leaves beliefs identical");

    GameSession hiddenHandGameA({"L", "P2", "P3", "P4", "P5"}, true, roles, {}, GameMode::Identity);
    GameSession hiddenHandGameB({"L", "P2", "P3", "P4", "P5"}, true, roles, {}, GameMode::Identity);
    emptyHand(*hiddenHandGameA.testingEngine().players()[1]);
    emptyHand(*hiddenHandGameB.testingEngine().players()[1]);
    hiddenHandGameA.testingEngine().players()[1]->addCard(makeCard("hidden-slash", CardType::Slash));
    hiddenHandGameB.testingEngine().players()[1]->addCard(makeCard("hidden-peach", CardType::Peach));
    auto realHandA = hiddenHandGameA.viewFor(hiddenHandGameA.addClient(1));
    auto realHandB = hiddenHandGameB.viewFor(hiddenHandGameB.addClient(1));
    realHandA.publicEvents = realHandB.publicEvents = attacks.publicEvents;
    inferenceA.rebuild(realHandA); inferenceB.rebuild(realHandB);
    check(realHandA.players[1].handCardCount == realHandB.players[1].handCardCount
              && inferenceA.beliefFor(2)->rebelScore == inferenceB.beliefFor(2)->rebelScore,
          "different real hidden hand faces with identical public count leave beliefs identical");

    auto revealed = attacks;
    revealed.players[1].alive = false;
    revealed.players[1].identity = PlayerIdentity::Rebel;
    inference.rebuild(revealed);
    check(inference.beliefFor(2)->confirmedIdentity == PlayerIdentity::Rebel
              && inference.beliefFor(2)->inferred == ai::AIInferredIdentity::Rebel,
          "death-time public reveal overrides inference as confirmed identity");

    auto ffa = view; ffa.gameMode = GameMode::FreeForAll; ffa.selfIdentity = PlayerIdentity::None;
    for (auto& player : ffa.players) player.identity.reset();
    ffa.publicEvents = attacks.publicEvents;
    inference.rebuild(ffa);
    check(!inference.enabled() && inference.beliefs().empty(), "FFA disables identity inference completely");

    inferenceA.rebuild(attacks);
    inferenceB.rebuild(attacks);
    check(inferenceA.beliefFor(2)->rebelScore == inferenceB.beliefFor(2)->rebelScore
              && inferenceA.evidence().size() == inferenceB.evidence().size(),
          "reconnect-style reconstruction and repeated inference are deterministic");
    inference.reset();
    check(!inference.enabled() && inference.beliefs().empty() && inference.evidence().empty(),
          "return-to-Lobby reset clears beliefs and evidence");
    inference.rebuild(view);
    check(inference.beliefFor(2)->inferred == ai::AIInferredIdentity::Unknown,
          "second game starts hidden players from a neutral belief");

    inference.rebuild(attacks);
    const auto modifier = inference.targetModifier(PlayerIdentity::Lord, 2);
    check(modifier.hostile > 0 && modifier.hostile <= 18 && modifier.friendly == 0,
          "inferred modifier is confidence-scaled and lower than known identity policy");
    auto targetView = attacks;
    targetView.inferenceModifiers = {modifier};
    const auto inferredTarget = ai::AITargetEvaluator::evaluate(targetView, cv("inference-slash", CardType::Slash),
                                                                 targetView.players[1], true, 1);
    check(inferredTarget.knownIdentityModifier == 0 && inferredTarget.inferenceModifier == modifier.hostile,
          "target evaluator keeps known identity and inferred behavior modifiers separate");

    GameSession lifecycle({"L", "P2", "P3", "P4", "P5"}, true, roles, {}, GameMode::Identity);
    const auto observer = lifecycle.addClient(1);
    lifecycle.testingEngine().testingApplyDamage(Damage {2, 1, 1});
    const auto beforeReconnect = lifecycle.viewFor(observer);
    inferenceA.rebuild(beforeReconnect);
    const int beforeReconnectScore = inferenceA.beliefFor(2)->rebelScore;
    lifecycle.disconnectClient(observer);
    const auto reconnectedObserver = lifecycle.addClient(1);
    inferenceB.rebuild(lifecycle.viewFor(reconnectedObserver));
    check(beforeReconnectScore > 0 && inferenceB.beliefFor(2)->rebelScore == beforeReconnectScore,
          "reconnect rebuilds identical belief from retained public event history");
    lifecycle.configureGame({"L", "P2", "P3", "P4", "P5"}, swappedRoles, {}, GameMode::Identity);
    lifecycle.startGame();
    inferenceB.rebuild(lifecycle.viewFor(reconnectedObserver));
    check(inferenceB.beliefFor(2)->inferred == ai::AIInferredIdentity::Unknown
              && inferenceB.evidence().empty(),
          "formal new-game configuration clears public evidence and second-game beliefs");
}

AIInferenceModifierView inferred(PlayerId id, AIInferredIdentityView role, int confidence = 3)
{
    AIInferenceModifierView value; value.playerId = id; value.confidence = confidence; value.inferred = role;
    if (role == AIInferredIdentityView::Loyalist) value.loyalistScore = confidence * 25;
    else if (role == AIInferredIdentityView::Rebel) value.rebelScore = confidence * 25;
    else if (role == AIInferredIdentityView::Renegade) value.renegadeScore = confidence * 25;
    return value;
}

void identityAdvancedStrategyCoverage()
{
    auto lord = policyView(PlayerIdentity::Lord);
    lord.players[1].hp = 1;
    lord.inferenceModifiers = {inferred(3, AIInferredIdentityView::Rebel),
                               inferred(4, AIInferredIdentityView::Loyalist),
                               inferred(5, AIInferredIdentityView::Renegade, 2)};
    const auto lordStrategy = ai::AIIdentityStrategy::evaluate(lord);
    check(lordStrategy.enabled && lordStrategy.intent == ai::AIStrategicIntent::PreserveSelf
              && ai::AIIdentityStrategy::cardKeepModifier(lord, lordStrategy, CardType::Peach) > 0
              && ai::AIIdentityStrategy::cardKeepModifier(lord, lordStrategy, CardType::Dodge) > 0,
          "low-HP Lord preserves survival resources");
    check(ai::AIIdentityStrategy::targetModifier(lordStrategy, 3, CardType::Slash)
              > ai::AIIdentityStrategy::targetModifier(lordStrategy, 4, CardType::Slash),
          "Lord prioritizes high-confidence inferred Rebel and avoids inferred Loyalist");
    auto lordEndgame = lord;
    lordEndgame.inferenceModifiers = {inferred(3, AIInferredIdentityView::Loyalist),
                                      inferred(5, AIInferredIdentityView::Renegade, 3)};
    const auto lordEndgameStrategy = ai::AIIdentityStrategy::evaluate(lordEndgame);
    check(ai::AIIdentityStrategy::targetModifier(lordEndgameStrategy, 5, CardType::Slash) > 0,
          "Lord can transition toward a possible Renegade after inferred Rebels disappear");
    lord.players[2].handCardCount = 10;
    const auto threatenedLord = ai::AIIdentityStrategy::evaluate(lord);
    check(ai::AIIdentityStrategy::targetModifier(threatenedLord, 3, CardType::Dismantlement) > 0,
          "Lord still reacts to a high public immediate threat while preserving itself");

    auto loyalist = policyView(PlayerIdentity::Loyalist);
    loyalist.players[0].hp = 1;
    loyalist.inferenceModifiers = {inferred(3, AIInferredIdentityView::Rebel),
                                   inferred(4, AIInferredIdentityView::Loyalist)};
    auto loyalStrategy = ai::AIIdentityStrategy::evaluate(loyalist);
    check(loyalStrategy.intent == ai::AIStrategicIntent::ProtectLord
              && ai::AIIdentityStrategy::shouldRescue(loyalist, loyalStrategy, 1),
          "Loyalist highly prioritizes a legal Lord rescue");
    loyalist.nullification = NullificationContextView {3, CardType::Duel, 1, 1};
    loyalStrategy = ai::AIIdentityStrategy::evaluate(loyalist);
    check(ai::AIIdentityStrategy::shouldNullify(loyalist, loyalStrategy),
          "Loyalist protects Lord from a harmful trick with Nullification");
    check(ai::AIIdentityStrategy::targetModifier(loyalStrategy, 3, CardType::Duel) > 0
              && ai::AIIdentityStrategy::targetModifier(loyalStrategy, 4, CardType::Duel) < 0,
          "Loyalist pressures inferred Rebels without treating inferred allies as certain enemies");
    check(ai::AIIdentityStrategy::cardKeepModifier(loyalist, loyalStrategy, CardType::Peach) > 0,
          "Loyalist retains its own survival and Lord-rescue resources");

    auto rebel = policyView(PlayerIdentity::Rebel);
    rebel.players[0].hp = 1;
    rebel.inferenceModifiers = {inferred(3, AIInferredIdentityView::Rebel),
                                inferred(4, AIInferredIdentityView::Loyalist)};
    const auto rebelStrategy = ai::AIIdentityStrategy::evaluate(rebel);
    check(rebelStrategy.intent == ai::AIStrategicIntent::FinishLord
              && ai::AIIdentityStrategy::targetModifier(rebelStrategy, 1, CardType::Slash) > 150,
          "Rebel switches to lethal Lord focus");
    check(ai::AIIdentityStrategy::targetModifier(rebelStrategy, 3, CardType::Slash) < 0
              && ai::AIIdentityStrategy::targetModifier(rebelStrategy, 4, CardType::Dismantlement) > 0,
          "Rebel reduces pressure on inferred teammates and may disable inferred Loyalists");
    check(ai::AIIdentityStrategy::targetModifier(rebelStrategy, 1, CardType::Dismantlement)
              > ai::AIIdentityStrategy::targetModifier(rebelStrategy, 1, CardType::Slash),
          "Rebel values removing Lord defense before damage when useful");

    auto renegade = policyView(PlayerIdentity::Renegade);
    renegade.players[0].hp = 1;
    renegade.players[2].hp = renegade.players[3].hp = 4;
    renegade.players[2].handCardCount = renegade.players[3].handCardCount = 8;
    renegade.inferenceModifiers = {inferred(3, AIInferredIdentityView::Rebel),
                                   inferred(4, AIInferredIdentityView::Rebel)};
    auto renegadeStrategy = ai::AIIdentityStrategy::evaluate(renegade);
    check(renegadeStrategy.intent == ai::AIStrategicIntent::ProtectLord
              && renegadeStrategy.tableBalanceScore > 0
              && ai::AIIdentityStrategy::shouldRescue(renegade, renegadeStrategy, 1),
          "Renegade protects Lord when the inferred Rebel side dominates");

    auto dominantLord = policyView(PlayerIdentity::Renegade);
    dominantLord.players[0].hp = 4; dominantLord.players[0].handCardCount = 8;
    dominantLord.inferenceModifiers = {inferred(3, AIInferredIdentityView::Loyalist),
                                       inferred(4, AIInferredIdentityView::Loyalist)};
    const auto dominantStrategy = ai::AIIdentityStrategy::evaluate(dominantLord);
    check(dominantStrategy.tableBalanceScore < 0
              && ai::AIIdentityStrategy::targetModifier(dominantStrategy, 1, CardType::Slash) > 0,
          "Renegade pressures the dominant Lord side using public balance estimates");

    auto duel = policyView(PlayerIdentity::Renegade);
    for (auto& player : duel.players) player.alive = player.id == 1 || player.id == 2;
    duel.players[0].hp = 1;
    const auto duelStrategy = ai::AIIdentityStrategy::evaluate(duel);
    check(duelStrategy.phase == ai::AIStrategyPhase::Endgame
              && duelStrategy.intent == ai::AIStrategicIntent::FinishLord
              && ai::AIIdentityStrategy::targetModifier(duelStrategy, 1, CardType::Slash) > 0
              && !ai::AIIdentityStrategy::shouldRescue(duel, duelStrategy, 1),
          "Renegade final duel stops balancing, attacks Lord, and never rescues Lord");

    auto threePlayer = renegade;
    for (auto& player : threePlayer.players) player.alive = player.id == 1 || player.id == 2 || player.id == 3;
    const auto threeStrategy = ai::AIIdentityStrategy::evaluate(threePlayer);
    check(ai::AIIdentityStrategy::targetModifier(threeStrategy, 3, CardType::Slash) > 0,
          "Renegade three-player endgame prevents an immediate inferred-Rebel win");

    check(ai::AIIdentityStrategy::cardUseModifier(loyalist, loyalStrategy, CardType::ArrowBarrage)
              < ai::AIIdentityStrategy::cardUseModifier(rebel, rebelStrategy, CardType::ArrowBarrage),
          "AOE value differs by role and Lord risk");
    check(ai::AIIdentityStrategy::targetModifier(rebelStrategy, 1, CardType::Indulgence) > 0,
          "role strategy affects delayed-trick targeting");

    auto ffa = lord; ffa.gameMode = GameMode::FreeForAll; ffa.selfIdentity = PlayerIdentity::None;
    const auto ffaStrategy = ai::AIIdentityStrategy::evaluate(ffa);
    check(!ffaStrategy.enabled && ai::AIIdentityStrategy::cardUseModifier(ffa, ffaStrategy, CardType::Slash) == 0,
          "FFA remains completely unaffected by Identity strategy");

    const auto repeat = ai::AIIdentityStrategy::evaluate(renegade);
    check(repeat.intent == renegadeStrategy.intent && repeat.tableBalanceScore == renegadeStrategy.tableBalanceScore
              && repeat.targets.size() == renegadeStrategy.targets.size(),
          "same public view deterministically rebuilds the same strategy after reconnect");
    auto secondGame = policyView(PlayerIdentity::Renegade);
    const auto fresh = ai::AIIdentityStrategy::evaluate(secondGame);
    check(fresh.enabled && fresh.phase == ai::AIStrategyPhase::EarlyGame
              && fresh.tableBalanceScore != renegadeStrategy.tableBalanceScore,
          "second game has no cached phase or table-balance state");

    auto privacyA = rebel, privacyB = rebel;
    const auto privateA = ai::AIIdentityStrategy::evaluate(privacyA);
    const auto privateB = ai::AIIdentityStrategy::evaluate(privacyB);
    check(privateA.intent == privateB.intent && privateA.tableBalanceScore == privateB.tableBalanceScore,
          "hidden identity and hidden hand faces cannot affect strategy when public views match");

    auto targetView = rebel;
    const auto evaluation = ai::AITargetEvaluator::evaluate(targetView, cv("strategy-slash", CardType::Slash),
                                                             targetView.players[0], true, 1);
    check(evaluation.identityStrategyModifier > 0 && evaluation.knownIdentityModifier > 0,
          "TargetEvaluator keeps known, inferred, and role-strategy layers additive");
}
}
void runAIIdentityTests()
{
    std::cerr << "Identity privacy/goals\n"; privacyAndGoals();
    std::cerr << "Identity policy boundaries\n"; targetAndResponseBoundaries();
    std::cerr << "Identity real rescue\n"; realRescueViews();
    std::cerr << "Identity mixed rounds\n";
    for (int count : {5, 6, 8}) mixedRounds(count);
    identityReconnect(); identityGameOver(); identityInferenceCoverage(); identityAdvancedStrategyCoverage();
    std::cout << "Stage 12B Identity AI coverage PASS\n";
    std::cout << "Stage 13.5E Identity strategy coverage PASS\n";
}
