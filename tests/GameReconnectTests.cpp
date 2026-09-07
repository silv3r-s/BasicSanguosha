#include <QCoreApplication>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

#include "client/GameClientController.h"
#include "network/GameServer.h"
#include "network/NetworkGameClient.h"

using namespace sanguosha;
using namespace sanguosha::network;

namespace {
[[noreturn]] void fail(const char* message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
void expect(bool value, const char* message) { if (!value) fail(message); }
bool waitFor(const std::function<bool()>& condition, int milliseconds = 3000)
{
    if (condition()) return true;
    QEventLoop loop; QTimer timeout, poll; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    timeout.start(milliseconds); poll.start(10); loop.exec(); return condition();
}

struct Fixture {
    GameServer server;
    GameClientController host;
    std::unique_ptr<NetworkGameClient> reconnecting;
    std::unique_ptr<NetworkGameClient> observer;

    Fixture() : host(server.session(), 1)
    {
        QString error;
        expect(server.listen(error, 0) && server.setTargetPlayerCount(3), "game reconnect server starts");
        server.setReconnectGracePeriodForTesting(1000);
        server.setInteractionTimeoutForTesting(60000);
        reconnecting = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort(), "Reconnect Player");
        observer = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort(), "Observer");
        reconnecting->connectToHost();
        expect(waitFor([&] { return reconnecting->selfPlayerId() == 2; }), "reconnecting player receives P2");
        observer->connectToHost();
        expect(waitFor([&] { return observer->selfPlayerId() == 3; }), "observer receives P3");
        expect(server.startLobbyGame(), "game starts");
        expect(waitFor([&] { return reconnecting->connected() && observer->connected(); }), "clients receive started game");

        auto& engine = server.session().testingEngine();
        for (const auto& player : engine.players()) {
            std::vector<CardId> ids;
            for (const auto& card : player->handCards()) ids.push_back(card->id());
            for (const auto& id : ids) player->removeCard(id);
        }
        engine.testingSetHp(2, 2);
        engine.testingEquip(2, std::make_shared<Card>("reconnect-weapon", "Reconnect Weapon", CardType::Weapon, Suit::Spade, 5,
                                                       EquipmentData {EquipmentSlot::Weapon, 2}));
        engine.players().at(1)->addCard(std::make_shared<Card>("RECONNECT_PRIVATE_CARD", "Reconnect Private Card", CardType::Peach, Suit::Heart, 9));
        expect(host.submit(EndPlayPhaseAction {1}).accepted, "P1 ends Play so P2 becomes current");
        expect(waitFor([&] { return engine.currentPlayer()->id() == 2 && engine.currentPhase() == Phase::Play; }), "engine advances to P2 Play phase");
        server.broadcastViews();
        expect(waitFor([&] { return reconnecting->view().currentTurnPlayer == 2 && reconnecting->view().currentPhase == Phase::Play; }), "P2 has the stable Play phase");
    }

    void reconnect()
    {
        reconnecting->disconnectForReconnect();
        expect(waitFor([&] { return server.testingReconnectReservationCount() == 1; }), "disconnect creates an in-game reconnect reservation");
        reconnecting->reconnectToHost();
        expect(waitFor([&] { return reconnecting->connected() && reconnecting->selfPlayerId() == 2 && server.testingReconnectReservationCount() == 0; }),
               "reconnect restores P2 and consumes the reservation");
    }
};

void reconnectDuringPlayPhaseRestoresViewState()
{
    Fixture f;
    const auto turn = f.reconnecting->view().turnNumber;
    f.reconnect();
    const auto& view = f.reconnecting->view();
    expect(f.reconnecting->lobbyState().gameStarted && view.turnNumber == turn && view.currentTurnPlayer == 2 && view.currentPhase == Phase::Play,
           "reconnect restores the active game and unchanged P2 Play phase");
    const auto& self = view.players.at(1);
    expect(self.hp == 2 && self.alive && self.equipment.cardsBySlot.at(static_cast<std::size_t>(EquipmentSlot::Weapon))
               && self.equipment.cardsBySlot.at(static_cast<std::size_t>(EquipmentSlot::Weapon))->id == "reconnect-weapon",
           "reconnect restores P2 HP and equipment from the authoritative game state");
}

void reconnectRestoresPrivateHand()
{
    Fixture f;
    f.reconnect();
    const auto hasPrivate = [](const auto& hand) { return std::any_of(hand.begin(), hand.end(), [](const auto& card) { return card.id == "RECONNECT_PRIVATE_CARD"; }); };
    expect(hasPrivate(f.reconnecting->view().ownHand), "reconnected player receives its complete private hand");
    expect(!hasPrivate(f.observer->view().ownHand) && f.observer->view().players.at(1).handCardCount == f.reconnecting->view().ownHand.size(),
           "observer receives only P2 hand count and not P2 private card data");
}

void oldSocketCannotActAfterReconnect()
{
    Fixture f;
    f.reconnecting->disconnectForReconnect();
    expect(waitFor([&] { return f.server.testingReconnectReservationCount() == 1; }), "P2 disconnect is observed before stale action check");
    const auto stale = f.reconnecting->submit(EndPlayPhaseAction {2});
    expect(!stale.accepted && f.server.session().testingEngine().currentPlayer()->id() == 2
               && f.server.session().testingEngine().currentPhase() == Phase::Play,
           "disconnected socket cannot mutate the authoritative game");
    f.reconnecting->reconnectToHost();
    expect(waitFor([&] { return f.reconnecting->connected() && f.reconnecting->selfPlayerId() == 2; }), "new socket becomes P2 connection");
    expect(f.reconnecting->submit(EndPlayPhaseAction {2}).accepted, "new socket is the sole connection authorized for P2");
    expect(waitFor([&] { return !f.reconnecting->actionPending(); }), "new socket action receives a server result");
    expect(f.server.session().testingEngine().currentPlayer()->id() == 2 && f.server.session().testingEngine().currentPhase() == Phase::Discard,
           "only the new socket advances P2 out of Play exactly once");
}

void reconnectDoesNotDuplicateGameState()
{
    Fixture f;
    const auto& before = f.server.session().testingEngine();
    const auto turn = before.turnNumber();
    const auto hand = before.player(2)->handCards().size();
    const auto logs = before.logEntries().size();
    f.reconnect();
    const auto& after = f.server.session().testingEngine();
    expect(after.turnNumber() == turn && after.currentPlayer()->id() == 2 && after.currentPhase() == Phase::Play
               && after.player(2)->handCards().size() == hand && after.logEntries().size() == logs + 2
               && after.logEntries()[logs] == "Reconnect Player disconnected; waiting to reconnect."
               && after.logEntries().back() == "Reconnect Player reconnected.",
           "reconnect does not deal, restart a phase, or duplicate game logs/actions beyond its connection notices");
}

void reconnectViewStateContainsRecentEquipmentEffects()
{
    Fixture f;
    auto& engine = f.server.session().testingEngine();
    engine.testingEquip(2, std::make_shared<Card>("reconnect-qinggang", "青釭剑", CardType::Weapon, Suit::Spade, 6,
                                                   EquipmentData {EquipmentSlot::Weapon, 2}));
    engine.testingStartSlash(2, {3}, 1, DamageNature::Normal, true);
    const auto response = engine.pendingResponse();
    expect(response && engine.submitAction(RespondAction {3, response->requestId, std::nullopt}).accepted,
           "reconnect equipment-effect setup Slash resolves");
    f.server.broadcastViews();
    expect(waitFor([&] { return f.reconnecting->view().equipmentEffects.size() == 1; }),
           "pre-disconnect ViewState contains the recent public equipment event");
    const auto eventId = f.reconnecting->view().equipmentEffects.front().eventId;
    f.reconnect();
    const auto& restored = f.reconnecting->view().equipmentEffects;
    expect(restored.size() == 1 && restored.front().eventId == eventId && restored.front().equipment == "青釭剑"
               && restored.front().effect == EquipmentEffectTypeView::IgnoreArmor && restored.front().ownerId == 2
               && restored.front().targetId == 3 && restored.front().relatedCard == CardType::Slash,
           "reconnect snapshot retains the existing public equipment-effect window without replaying engine logic");
}

void reconnectDuringResponseRestoresRequest()
{
    Fixture f;
    auto& engine = f.server.session().testingEngine();
    engine.players().at(1)->addCard(std::make_shared<Card>("reconnect-response-slash", "Slash", CardType::Slash, Suit::Club, 7));
    engine.players().at(2)->addCard(std::make_shared<Card>("reconnect-response-dodge", "Dodge", CardType::Dodge, Suit::Heart, 2));
    expect(f.reconnecting->submit(PlayCardAction {2, "reconnect-response-slash", {3}}).accepted, "P2 creates a real Dodge response");
    expect(waitFor([&] { return engine.pendingResponse() && f.observer->view().response && f.observer->view().response->isResponder; }), "P3 receives the response");
    const auto requestId = f.observer->view().response->requestId;
    const auto remainingBefore = f.observer->view().timeoutRemainingMs;
    expect(waitFor([&] { return f.observer->view().timeoutRemainingMs <= remainingBefore - 200; }, 1000), "server broadcasts elapsed response timeout before reconnect");
    f.observer->disconnectForReconnect();
    expect(waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->requestId == requestId && f.server.testingReconnectReservationCount() == 1; }),
           "response and request ID survive responder disconnect during grace");
    expect(!f.observer->submit(RespondAction {3, requestId, "reconnect-response-dodge"}).accepted,
           "old socket cannot answer the retained response");
    f.observer->reconnectToHost();
    expect(waitFor([&] { return f.observer->connected() && f.observer->view().response && f.observer->view().response->requestId == requestId
                              && f.observer->view().response->responder == 3 && f.observer->view().response->isResponder; }),
           "reconnected responder receives the same authoritative response");
    const auto& response = *f.observer->view().response;
    expect(f.observer->view().timeoutRemainingMs < remainingBefore && std::any_of(response.selectableCards.begin(), response.selectableCards.end(), [](const auto& card) { return card.id == "reconnect-response-dodge"; }),
           "response legal cards and non-reset timeout are restored");
    expect(f.observer->submit(RespondAction {3, requestId, "reconnect-response-dodge"}).accepted, "new socket answers the restored request");
    expect(waitFor([&] { return !engine.pendingResponse() && engine.player(3)->handCards().empty(); }), "restored response settles exactly once");
}

void responseTimeoutWhileDisconnectedInvalidatesOldRequest()
{
    Fixture f;
    f.server.setInteractionTimeoutForTesting(120);
    auto& engine = f.server.session().testingEngine();
    engine.players().at(1)->addCard(std::make_shared<Card>("timeout-response-slash", "Slash", CardType::Slash, Suit::Club, 7));
    engine.players().at(2)->addCard(std::make_shared<Card>("timeout-response-dodge", "Dodge", CardType::Dodge, Suit::Heart, 2));
    expect(f.reconnecting->submit(PlayCardAction {2, "timeout-response-slash", {3}}).accepted, "P2 creates timeout response");
    expect(waitFor([&] { return f.observer->view().response.has_value(); }), "P3 receives timeout response");
    const auto requestId = f.observer->view().response->requestId;
    f.observer->disconnectForReconnect();
    expect(waitFor([&] { return !engine.pendingResponse() && engine.player(3)->hp() == 3; }, 2000), "interaction timeout resolves disconnected response once");
    f.observer->reconnectToHost();
    expect(waitFor([&] { return f.observer->connected() && !f.observer->view().response; }), "reconnect receives advanced state after timeout");
    expect(f.observer->submit(RespondAction {3, requestId, "timeout-response-dodge"}).accepted, "old request reaches rejection path");
    expect(waitFor([&] { return !f.observer->actionPending(); }) && engine.player(3)->hp() == 3 && !engine.pendingResponse(),
           "timed-out request ID cannot replay an old response");
}

void reconnectDuringSelectionRestoresOptions()
{
    Fixture f;
    auto& engine = f.server.session().testingEngine();
    engine.testingSetGender(2, Gender::Male); engine.testingSetGender(3, Gender::Female);
    engine.testingEquip(2, std::make_shared<Card>("reconnect-double-sword", "雌雄双股剑", CardType::Weapon, Suit::Spade, 2, EquipmentData {EquipmentSlot::Weapon, 2}));
    engine.players().at(1)->addCard(std::make_shared<Card>("reconnect-selection-slash", "Slash", CardType::Slash, Suit::Club, 7));
    engine.players().at(2)->addCard(std::make_shared<Card>("reconnect-selection-card", "Selection Card", CardType::Peach, Suit::Heart, 3));
    expect(f.reconnecting->submit(PlayCardAction {2, "reconnect-selection-slash", {3}}).accepted, "P2 creates Double Sword selection");
    expect(waitFor([&] { return engine.pendingCardSelection() && f.observer->view().cardSelection.has_value(); }), "P3 receives Double Sword selection");
    const auto selection = *f.observer->view().cardSelection;
    f.observer->disconnectForReconnect();
    expect(waitFor([&] { return engine.pendingCardSelection() && engine.pendingCardSelection()->requestId == selection.requestId && f.server.testingReconnectReservationCount() == 1; }),
           "selection and request ID survive selector disconnect during grace");
    f.observer->reconnectToHost();
    expect(waitFor([&] { return f.observer->connected() && f.observer->view().cardSelection && f.observer->view().cardSelection->requestId == selection.requestId; }),
           "reconnected selector receives the same selection request");
    const auto& restored = *f.observer->view().cardSelection;
    expect(restored.options.size() == selection.options.size() && restored.options.front().optionId == selection.options.front().optionId,
           "selection options are restored from authoritative state");
    expect(f.observer->submitCardSelection(restored.requestId, restored.options.front().optionId).accepted, "new socket completes restored selection");
    expect(waitFor([&] { return !engine.pendingCardSelection() && engine.pendingResponse(); }), "restored selection settles once and Slash continues");
}

void selectionTimeoutWhileDisconnectedUsesFallback()
{
    Fixture f;
    f.server.setInteractionTimeoutForTesting(120);
    auto& engine = f.server.session().testingEngine();
    engine.testingSetGender(2, Gender::Male); engine.testingSetGender(3, Gender::Female);
    engine.testingEquip(2, std::make_shared<Card>("timeout-double-sword", "雌雄双股剑", CardType::Weapon, Suit::Spade, 2, EquipmentData {EquipmentSlot::Weapon, 2}));
    engine.players().at(1)->addCard(std::make_shared<Card>("timeout-selection-slash", "Slash", CardType::Slash, Suit::Club, 7));
    engine.players().at(2)->addCard(std::make_shared<Card>("timeout-selection-card", "Selection Card", CardType::Peach, Suit::Heart, 3));
    expect(f.reconnecting->submit(PlayCardAction {2, "timeout-selection-slash", {3}}).accepted, "P2 creates timeout selection");
    expect(waitFor([&] { return f.observer->view().cardSelection.has_value(); }), "P3 receives timeout selection");
    f.observer->disconnectForReconnect();
    expect(waitFor([&] { return !engine.pendingCardSelection() && engine.pendingResponse(); }, 2000), "selection remains pending until its interaction timeout then uses fallback");
}

void advanceToP1Play(Fixture& f)
{
    auto& engine = f.server.session().testingEngine();
    expect(f.reconnecting->submit(EndPlayPhaseAction {2}).accepted, "P2 ends its empty setup turn");
    expect(waitFor([&] { return engine.currentPlayer()->id() == 2 && engine.currentPhase() == Phase::Discard; }), "P2 reaches setup discard");
    expect(waitFor([&] { return !f.reconnecting->actionPending(); }), "P2 receives the setup-turn action result before submitting discard");
    std::vector<CardId> ids;
    for (int index = 0; index < engine.requiredDiscardCount(); ++index) ids.push_back(engine.player(2)->handCards().at(index)->id());
    expect(f.reconnecting->submit(DiscardAction {2, std::move(ids)}).accepted, "P2 completes setup discard");
    expect(waitFor([&] { return engine.currentPlayer()->id() == 3 && engine.currentPhase() == Phase::Play; }), "P3 receives setup turn");
    expect(f.observer->submit(EndPlayPhaseAction {3}).accepted, "P3 ends its empty setup turn");
    expect(waitFor([&] { return engine.currentPlayer()->id() == 1 && engine.currentPhase() == Phase::Play; }), "P1 receives the authored complex-flow turn");
    f.server.broadcastViews();
}

void passNullificationChain(Fixture& f)
{
    auto& engine = f.server.session().testingEngine();
    while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
        const auto request = *engine.pendingResponse();
        if (request.responder == 1) {
            expect(f.host.submit(RespondAction {1, request.requestId, std::nullopt}).accepted, "host passes Nullification");
        } else if (request.responder == 2) {
            expect(f.reconnecting->submit(RespondAction {2, request.requestId, std::nullopt}).accepted, "P2 passes Nullification");
        } else {
            expect(f.observer->submit(RespondAction {3, request.requestId, std::nullopt}).accepted, "P3 passes Nullification");
        }
        expect(waitFor([&] { return !engine.pendingResponse() || engine.pendingResponse()->requestId != request.requestId; }), "Nullification pass advances exactly once");
    }
}

void reconnectDuringPeachRescueRestoresRequest()
{
    Fixture f;
    auto& engine = f.server.session().testingEngine();
    engine.testingSetHp(1, 1);
    engine.players().at(1)->addCard(std::make_shared<Card>("d-peach-slash", "Slash", CardType::Slash, Suit::Spade, 3));
    engine.players().at(2)->addCard(std::make_shared<Card>("d-peach-private", "Peach", CardType::Peach, Suit::Heart, 4));
    expect(f.reconnecting->submit(PlayCardAction {2, "d-peach-slash", {1}}).accepted, "P2 starts lethal Slash");
    expect(waitFor([&] { f.host.refresh(); return f.host.view().response && f.host.view().response->type == ResponseType::Dodge; }), "P1 receives Dodge");
    expect(f.host.submit(RespondAction {1, f.host.view().response->requestId, std::nullopt}).accepted, "P1 declines Dodge");
    expect(waitFor([&] { f.host.refresh(); return f.host.view().response && f.host.view().response->type == ResponseType::PeachRescue; }), "Peach rescue starts");
    expect(f.host.submit(RespondAction {1, f.host.view().response->requestId, std::nullopt}).accepted, "dying P1 passes its rescue chance");
    expect(waitFor([&] { return f.reconnecting->view().response && f.reconnecting->view().response->isResponder
        && f.reconnecting->view().response->type == ResponseType::PeachRescue
        && f.reconnecting->view().timeoutRemainingMs > 0; }), "P2 is the next rescuer with an initialized deadline");
    const auto rescueId = f.reconnecting->view().response->requestId;
    const auto timeoutBefore = f.reconnecting->view().timeoutRemainingMs;
    f.reconnect();
    const auto& restored = *f.reconnecting->view().response;
    const bool restoredRescue = engine.dyingContext() && restored.requestId == rescueId && restored.responder == 2
        && restored.type == ResponseType::PeachRescue && f.reconnecting->view().timeoutRemainingMs <= timeoutBefore
        && std::any_of(restored.selectableCards.begin(), restored.selectableCards.end(), [](const auto& card) { return card.id == "RECONNECT_PRIVATE_CARD"; });
    if (!restoredRescue) std::cerr << "Peach reconnect diagnostic: dying=" << bool(engine.dyingContext())
        << " request=" << restored.requestId << '/' << rescueId << " responder=" << restored.responder
        << " type=" << static_cast<int>(restored.type) << " timeout=" << f.reconnecting->view().timeoutRemainingMs
        << '/' << timeoutBefore << " cards=" << restored.selectableCards.size() << '\n';
    expect(restoredRescue,
           "Peach rescue reconnect retains dying context, responder, request and deadline");
    expect(f.reconnecting->submit(RespondAction {2, rescueId, std::nullopt}).accepted, "reconnected P2 passes rescue");
    expect(waitFor([&] { return f.observer->view().response && f.observer->view().response->isResponder && f.observer->view().response->type == ResponseType::PeachRescue; }), "rescue advances to P3 once");
    const auto p3Id = f.observer->view().response->requestId;
    expect(p3Id != rescueId && f.observer->view().response->selectableCards.size() == 1 && f.observer->view().response->selectableCards.front().id == "d-peach-private", "new rescuer gets a fresh private Peach request");
    expect(f.observer->submit(RespondAction {3, p3Id, "d-peach-private"}).accepted, "P3 saves dying player");
    expect(waitFor([&] { return !engine.dyingContext() && !engine.pendingResponse() && engine.player(1)->isAlive() && engine.player(1)->hp() == 1; }), "Peach rescue settles once");
}

void peachRescueTimeoutAndGraceRace()
{
    Fixture f;
    auto& engine = f.server.session().testingEngine();
    engine.testingSetHp(1, 1); f.server.setInteractionTimeoutForTesting(120);
    engine.players().at(1)->addCard(std::make_shared<Card>("d-timeout-slash", "Slash", CardType::Slash, Suit::Spade, 3));
    expect(f.reconnecting->submit(PlayCardAction {2, "d-timeout-slash", {1}}).accepted, "timeout flow starts Slash");
    expect(waitFor([&] { f.host.refresh(); return f.host.view().response && f.host.view().response->type == ResponseType::Dodge; }), "timeout flow gets Dodge");
    expect(f.host.submit(RespondAction {1, f.host.view().response->requestId, std::nullopt}).accepted, "timeout flow declines Dodge");
    expect(waitFor([&] { f.host.refresh(); return f.host.view().response && f.host.view().response->type == ResponseType::PeachRescue; }), "timeout flow enters rescue");
    expect(f.host.submit(RespondAction {1, f.host.view().response->requestId, std::nullopt}).accepted, "P1 passes timeout rescue");
    expect(waitFor([&] { return f.reconnecting->view().response && f.reconnecting->view().response->isResponder; }), "P2 owns timeout rescue");
    const auto expiredId = f.reconnecting->view().response->requestId;
    f.reconnecting->disconnectForReconnect();
    expect(waitFor([&] { return f.server.testingReconnectReservationCount() == 1; }), "grace protects disconnected Peach responder");
    expect(waitFor([&] { return f.observer->view().response && f.observer->view().response->isResponder && f.observer->view().response->requestId != expiredId; }, 1500), "interaction timeout first advances Peach rescue once");
    f.reconnecting->reconnectToHost();
    expect(waitFor([&] { return f.reconnecting->connected() && f.reconnecting->view().response && f.reconnecting->view().response->requestId != expiredId; }), "reconnect after interaction timeout sees only new state");
    f.server.setInteractionTimeoutForTesting(5000); f.server.setReconnectGracePeriodForTesting(120);
    f.observer->disconnectForReconnect();
    expect(waitFor([&] { return f.server.testingReconnectReservationCount() == 1; }), "second rescuer obtains grace reservation");
    expect(waitFor([&] { return f.server.testingReconnectReservationCount() == 0 && !engine.pendingResponse(); }, 1500), "grace timeout first applies Peach fallback exactly once");
}

void reconnectDuringNullificationRounds()
{
    std::cerr << "R3G_BUILD_MARKER_20260904_A\nR3G_STEP_1_CASE_ENTER\n" << std::flush;
    Fixture f; advanceToP1Play(f);
    // This test verifies restoration of the same authoritative Nullification
    // request.  Keep its deadline outside the bounded reconnect handshake so
    // the dedicated timeout-race tests remain the only timeout oracle here.
    f.server.setInteractionTimeoutForTesting(12000);
    auto& engine = f.server.session().testingEngine();
    engine.players().at(0)->addCard(std::make_shared<Card>("d-null-trick", "Ex Nihilo", CardType::ExNihilo, Suit::Heart, 1));
    engine.players().at(1)->addCard(std::make_shared<Card>("d-null-one", "Nullification", CardType::Nullification, Suit::Spade, 2));
    engine.players().at(2)->addCard(std::make_shared<Card>("d-null-two", "Nullification", CardType::Nullification, Suit::Club, 3));
    expect(f.host.submit(PlayCardAction {1, "d-null-trick", {}}).accepted, "P1 starts cancellable trick");
    expect(waitFor([&] { return f.reconnecting->view().response && f.reconnecting->view().response->isResponder && f.reconnecting->view().response->type == ResponseType::Nullification; }), "P2 receives first Nullification request");
    const auto firstId = f.reconnecting->view().response->requestId;
    const auto expectedSelectableCardIds = [&] { std::vector<CardId> ids; for (const auto& card : f.reconnecting->view().response->selectableCards) ids.push_back(card.id); return ids; }();
    std::cerr << "R3G_STEP_2_EXPECTED_CAPTURED\n" << std::flush;
    struct TraceRecord {
        std::uint64_t requestId {};
        int responseType {-1};
        std::vector<CardId> cards;
    };
    std::vector<TraceRecord> serverPayloadTrace, deserializedTrace, appliedTrace;
    const auto cardsFromPayload = [] (const QJsonObject& response) {
        std::vector<CardId> cards;
        for (const auto& card : response["cards"].toArray()) cards.push_back(card.toObject()["id"].toString().toStdString());
        return cards;
    };
    QObject::connect(&f.server, &GameServer::viewPayloadPrepared, [&] (PlayerId viewer, const QJsonObject& payload) {
        if (viewer != 2 || !payload.contains("response")) return;
        const auto response = payload["response"].toObject();
        serverPayloadTrace.push_back(TraceRecord {std::uint64_t(response["request"].toInteger()), response["type"].toInt(), cardsFromPayload(response)});
    });
    f.reconnecting->setDeserializedViewObserverForTesting([&] (const PlayerViewState& view) {
        if (!view.response) return;
        std::vector<CardId> cards; for (const auto& card : view.response->selectableCards) cards.push_back(card.id);
        deserializedTrace.push_back(TraceRecord {view.response->requestId, int(view.response->type), std::move(cards)});
    });
    f.reconnecting->setAppliedViewObserverForTesting([&] (const PlayerViewState& view, bool) {
        if (!view.response) return;
        std::vector<CardId> cards; for (const auto& card : view.response->selectableCards) cards.push_back(card.id);
        appliedTrace.push_back(TraceRecord {view.response->requestId, int(view.response->type), std::move(cards)});
    });
    const auto matchingRecords = [&] (const std::vector<TraceRecord>& records) {
        std::vector<const TraceRecord*> matches;
        for (const auto& record : records) if (record.requestId == firstId && record.responseType == int(ResponseType::Nullification)) matches.push_back(&record);
        return matches;
    };
    const auto traceDiagnostics = [&] {
        const auto response = engine.pendingResponse();
        const auto& clientResponse = f.reconnecting->view().response;
        std::ostringstream stream;
        const auto appendCards = [&] (const std::vector<CardId>& cards) { for (const auto& id : cards) stream << ' ' << id; };
        const auto appendRecords = [&] (const char* label, const std::vector<TraceRecord>& records) {
            const auto matches = matchingRecords(records);
            stream << label << " total=" << records.size() << " matching=" << matches.size() << '\n';
            for (const auto* record : matches) { stream << "  request=" << record->requestId << " type=" << record->responseType << " cards=" << record->cards.size() << ':'; appendCards(record->cards); stream << '\n'; }
        };
        stream << "TRACE expectedRequest=" << firstId << " expectedCards=" << expectedSelectableCardIds.size() << ':'; appendCards(expectedSelectableCardIds); stream << '\n';
        stream << "authority pending=" << bool(response) << " request=" << (response ? response->requestId : 0) << " type=" << (response ? int(response->type) : -1) << " responder=" << (response ? response->responder : 0) << '\n';
        appendRecords("serverPayload", serverPayloadTrace);
        appendRecords("deserialize", deserializedTrace);
        appendRecords("apply", appliedTrace);
        stream << "final request=" << (clientResponse ? clientResponse->requestId : 0) << " type=" << (clientResponse ? int(clientResponse->type) : -1) << " cards=" << (clientResponse ? clientResponse->selectableCards.size() : 0) << ':';
        if (clientResponse) for (const auto& card : clientResponse->selectableCards) stream << ' ' << card.id;
        stream << " actionPending=" << f.reconnecting->actionPending() << '\n';
        return stream.str();
    };
    f.reconnect();
    std::cerr << "R3G_STEP_5_BEFORE_WAIT_ASSERT\n" << std::flush;
    const auto restored = waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->requestId == firstId
                                && engine.pendingResponse()->responder == 2
                                && f.reconnecting->view().response && f.reconnecting->view().response->requestId == firstId
                                && [&] { std::vector<CardId> cards; for (const auto& card : f.reconnecting->view().response->selectableCards) cards.push_back(card.id); return cards == expectedSelectableCardIds; }(); }, 5000);
    if (!restored) {
        std::cerr << "R3G_STEP_3_BEFORE_TRACE_DUMP\n" << std::flush;
        std::cerr << traceDiagnostics() << std::endl;
        std::cerr << "R3G_STEP_4_AFTER_TRACE_DUMP\n" << std::flush;
        fail("reconnect restores first Nullification privately");
    }
    const auto serverMatches = matchingRecords(serverPayloadTrace);
    const auto deserializeMatches = matchingRecords(deserializedTrace);
    const auto applyMatches = matchingRecords(appliedTrace);
    const auto hasExpectedCards = [&] (const std::vector<const TraceRecord*>& records) {
        return std::any_of(records.begin(), records.end(), [&] (const TraceRecord* record) { return record->cards == expectedSelectableCardIds; });
    };
    if (!hasExpectedCards(serverMatches) || !hasExpectedCards(deserializeMatches) || !hasExpectedCards(applyMatches)) {
        std::cerr << "R3H_BEFORE_RETENTION_TRACE_DUMP\n" << traceDiagnostics() << std::endl;
        fail("transport trace retains private Nullification cards");
    }
    const auto& observerResponse = f.observer->view().response;
    expect(!observerResponse || observerResponse->requestId != firstId || observerResponse->selectableCards.empty(), "non-responder cannot observe private Nullification cards");
    expect(f.reconnecting->submit(RespondAction {2, firstId, "d-null-one"}).accepted, "P2 plays first Nullification");
    expect(waitFor([&] { return engine.nullificationChain() && engine.nullificationChain()->nullificationCount == 1 && f.observer->view().response && f.observer->view().response->isResponder; }), "first Nullification starts a new chain round");
    const auto secondId = f.observer->view().response->requestId;
    expect(secondId != firstId, "new Nullification round gets new request ID");
    f.observer->disconnectForReconnect();
    expect(waitFor([&] { return f.server.testingReconnectReservationCount() == 1; }), "second-round responder is reserved");
    f.observer->reconnectToHost();
    expect(waitFor([&] { return f.observer->connected() && f.observer->view().response && f.observer->view().response->requestId == secondId; }), "reconnect restores second, not first, Nullification round");
    expect(f.observer->submit(RespondAction {3, secondId, "d-null-two"}).accepted, "P3 counters Nullification");
    expect(waitFor([&] { return !f.observer->actionPending() && (!engine.pendingResponse() || engine.pendingResponse()->requestId != secondId); }), "second Nullification action settles and advances authoritative request state");
    int drivenNullificationSteps = 0;
    while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
        expect(++drivenNullificationSteps <= 12, "Nullification chain has a bounded authoritative termination");
        const auto request = *engine.pendingResponse();
        const auto responder = engine.player(request.responder);
        std::optional<CardId> legal;
        for (const auto& card : responder->handCards()) if (card && card->type() == CardType::Nullification) { legal = card->id(); break; }
        expect(engine.submitAction(RespondAction {request.responder, request.requestId, legal}).accepted, "authoritative legal responder advances remaining Nullification chain");
    }
    expect(!engine.nullificationChain(), "Nullification chain terminates after all authoritative responders are handled");
    expect(!engine.submitAction(RespondAction {2, firstId, std::nullopt}).accepted, "historical Nullification request is rejected after the chain resolves");
}

void reconnectDuringDuelAndAoeResponses()
{
    for (const auto [type, replyCardType] : std::initializer_list<std::pair<CardType, CardType>> {{CardType::Duel, CardType::FireSlash}, {CardType::Duel, CardType::ThunderSlash}, {CardType::BarbarianInvasion, CardType::FireSlash}, {CardType::ArrowBarrage, CardType::Dodge}}) {
        Fixture f; advanceToP1Play(f);
        auto& engine = f.server.session().testingEngine();
        for (const auto& player : engine.players()) {
            std::vector<CardId> ids;
            for (const auto& card : player->handCards()) ids.push_back(card->id());
            for (const auto& id : ids) player->removeCard(id);
        }
        const char* active = type == CardType::Duel ? "d-duel" : (type == CardType::BarbarianInvasion ? "d-barbarian" : "d-arrow");
        const char* reply = replyCardType == CardType::FireSlash ? "d-fire" : replyCardType == CardType::ThunderSlash ? "d-thunder" : replyCardType == CardType::Dodge ? "d-dodge" : "d-slash";
        const auto responseType = type == CardType::ArrowBarrage ? ResponseType::Dodge : ResponseType::Slash;
        engine.players().at(0)->addCard(std::make_shared<Card>(active, active, type, Suit::Heart, 1));
        engine.players().at(1)->addCard(std::make_shared<Card>(reply, reply, replyCardType, Suit::Spade, 2));
        engine.players().at(0)->addCard(std::make_shared<Card>("d-source-slash", "Slash", CardType::Slash, Suit::Club, 4));
        expect(f.host.submit(PlayCardAction {1, active, type == CardType::Duel ? std::vector<PlayerId>{2} : std::vector<PlayerId>{}}).accepted, "complex card starts");
        passNullificationChain(f);
        expect(waitFor([&] { return f.reconnecting->view().response && f.reconnecting->view().response->isResponder && f.reconnecting->view().response->type == responseType; }), "P2 receives first complex response");
        const auto firstId = f.reconnecting->view().response->requestId;
        expect(std::any_of(f.reconnecting->view().response->selectableCards.begin(), f.reconnecting->view().response->selectableCards.end(), [reply] (const auto& card) { return card.id == reply; }), "response snapshot contains the legal private response card");
        f.reconnect();
        expect(f.reconnecting->view().response && f.reconnecting->view().response->requestId == firstId && std::any_of(f.reconnecting->view().response->selectableCards.begin(), f.reconnecting->view().response->selectableCards.end(), [reply] (const auto& card) { return card.id == reply; }), "complex response request and private cards survive reconnect");
        expect(f.reconnecting->submit(RespondAction {2, firstId, reply}).accepted, "reconnected responder continues complex flow");
        expect(waitFor([&] { return !f.reconnecting->actionPending(); }), "P2 complex response action result settles before target advancement inspection");
        expect(waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->requestId != firstId; }), "complex flow advances to a fresh authoritative request");
        auto later = *engine.pendingResponse();
        if (type != CardType::Duel) {
            expect(later.type == ResponseType::Nullification && later.responder == 2,
                   "the next AOE target first opens P2's uniform Nullification request");
            passNullificationChain(f);
            const auto reachedP3 = waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->responder == 3 && engine.pendingResponse()->type == responseType; });
            if (!reachedP3) {
                std::cerr << "AOE_DIAG type=" << (type == CardType::BarbarianInvasion ? "SavageAssault" : "ArcheryAttack");
                if (const auto& request = engine.pendingResponse()) std::cerr << " pendingType=" << static_cast<int>(request->type) << " responder=" << request->responder << " requestId=" << request->requestId;
                else std::cerr << " pending=NONE";
                if (const auto& aoe = engine.multiTargetEffect()) std::cerr << " targetIndex=" << aoe->currentTargetIndex << " targets=" << aoe->orderedTargets.size();
                else std::cerr << " aoe=NONE";
                std::cerr << " chain=" << (engine.nullificationChain() ? engine.nullificationChain()->nullificationCount : -1);
                for (const auto& player : engine.players()) { int nulls = 0; for (const auto& card : player->handCards()) if (card && card->type() == CardType::Nullification) ++nulls; std::cerr << " P" << player->id() << "N=" << nulls; }
                std::cerr << std::endl;
                fail("AOE reaches P3's authoritative response after scoped Nullification processing");
            }
            later = *engine.pendingResponse();
        }
        expect(f.reconnecting->submit(RespondAction {2, firstId, std::nullopt}).accepted, "old complex request reaches rejection path");
        expect(waitFor([&] { return engine.pendingResponse() && engine.pendingResponse()->requestId == later.requestId && engine.pendingResponse()->responder == later.responder; }), "old complex request cannot alter target/order");
        if (type == CardType::Duel) {
            expect(later.responder == 1 && later.type == ResponseType::Slash, "Duel alternates to P1 after P2 response");
            expect(f.host.submit(RespondAction {1, later.requestId, "d-source-slash"}).accepted, "P1 continues Duel round");
            expect(waitFor([&] { return f.reconnecting->view().response && f.reconnecting->view().response->isResponder && f.reconnecting->view().response->requestId != firstId; }), "later Duel response returns to P2");
            f.reconnect();
            expect(f.reconnecting->view().response && f.reconnecting->view().response->isResponder, "later Duel round also survives reconnect");
        } else expect(later.responder == 3 && later.type == responseType, "AOE P3 receives the correct authoritative response type");
    }
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    reconnectDuringPlayPhaseRestoresViewState();
    reconnectRestoresPrivateHand();
    oldSocketCannotActAfterReconnect();
    reconnectDoesNotDuplicateGameState();
    reconnectViewStateContainsRecentEquipmentEffects();
    reconnectDuringResponseRestoresRequest();
    responseTimeoutWhileDisconnectedInvalidatesOldRequest();
    reconnectDuringSelectionRestoresOptions();
    selectionTimeoutWhileDisconnectedUsesFallback();
    reconnectDuringPeachRescueRestoresRequest();
    peachRescueTimeoutAndGraceRace();
    reconnectDuringNullificationRounds();
    reconnectDuringDuelAndAoeResponses();
    std::cout << "BasicSanguoshaGameReconnectTests PASS\n";
}
