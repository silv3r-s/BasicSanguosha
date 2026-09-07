#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>

#include "client/GameClientController.h"
#include "network/GameServer.h"
#include "network/NetworkGameClient.h"

using namespace sanguosha;
using namespace sanguosha::network;

namespace {
[[noreturn]] void fail(const char* message) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
void ok(bool condition, const char* message) { if (!condition) fail(message); }
bool waitFor(const std::function<bool()>& condition, int milliseconds = 3000)
{
    if (condition()) return true;
    QEventLoop loop; QTimer timeout, poll; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    timeout.start(milliseconds); poll.start(10); loop.exec(); return condition();
}
std::shared_ptr<Card> iceSword()
{
    return std::make_shared<Card>("ice-network", "寒冰剑", CardType::Weapon, Suit::Spade, 2,
                                  EquipmentData {EquipmentSlot::Weapon, 2});
}
std::shared_ptr<Card> mount()
{
    return std::make_shared<Card>("ice-network-mount", "赤兔", CardType::OffensiveHorse, Suit::Heart, 5,
                                  EquipmentData {EquipmentSlot::OffensiveHorse, 1});
}

struct Fixture {
    GameServer server;
    GameClientController host;
    std::unique_ptr<NetworkGameClient> actor, target, observer;

    Fixture() : host(server.session(), 1)
    {
        QString error;
        ok(server.listen(error, 0) && server.setTargetPlayerCount(4), "Ice Sword TCP server starts");
        actor = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort()); actor->connectToHost();
        ok(waitFor([&] { return server.connectedHumanCount() == 2 && actor->selfPlayerId() == 2; }), "actor receives deterministic seat P2");
        target = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort()); target->connectToHost();
        ok(waitFor([&] { return server.connectedHumanCount() == 3 && target->selfPlayerId() == 3; }), "target receives deterministic seat P3");
        observer = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort()); observer->connectToHost();
        ok(waitFor([&] { return server.connectedHumanCount() == 4 && observer->selfPlayerId() == 4; }), "observer receives deterministic seat P4");
        ok(server.startLobbyGame() && waitFor([&] { return actor->connected() && target->connected() && observer->connected(); }), "TCP clients enter game");

        auto& engine = server.session().testingEngine();
        for (const auto& player : engine.players()) {
            std::vector<CardId> ids;
            for (const auto& card : player->handCards()) ids.push_back(card->id());
            for (const auto& id : ids) player->removeCard(id);
        }
        engine.testingEquip(2, iceSword());
        engine.players()[1]->addCard(std::make_shared<Card>("ICE_NETWORK_SLASH", "actor private slash", CardType::Slash, Suit::Club, 7));
        engine.players()[2]->addCard(std::make_shared<Card>("ICE_SECRET_CARD_A", "ICE SECRET NAME A", CardType::Peach, Suit::Spade, 9));
        engine.players()[2]->addCard(std::make_shared<Card>("ICE_SECRET_CARD_B", "ICE SECRET NAME B", CardType::Dodge, Suit::Diamond, 12));
        ok(host.submit(EndPlayPhaseAction {1}).accepted, "advance to remote actor Play phase");
        server.broadcastViews();
        ok(waitFor([&] { return actor->view().currentTurnPlayer == 2 && actor->view().currentPhase == Phase::Play; }), "actor has a live Play phase");
    }

    void beginPrompt()
    {
        ok(actor->submit(PlayCardAction {2, "ICE_NETWORK_SLASH", {3}}).accepted, "remote actor uses a real TCP Slash");
        auto& engine = server.session().testingEngine();
        ok(waitFor([&] { return engine.pendingResponse().has_value(); }), "target receives Dodge response");
        const auto dodge = *engine.pendingResponse();
        ok(dodge.responder == 3 && target->submit(RespondAction {3, dodge.requestId, std::nullopt}).accepted, "remote target passes Dodge");
        ok(waitFor([&] { return engine.pendingCardSelection().has_value() && actor->view().cardSelection.has_value(); }), "Ice Sword prompt reaches TCP actor");
    }

    const CardSelectionView& prompt() const { return *actor->view().cardSelection; }
    void activate()
    {
        const auto id = prompt().requestId;
        ok(actor->submitCardSelection(id, 1).accepted, "remote actor activates Ice Sword");
        ok(waitFor([&] { return actor->view().cardSelection && actor->view().cardSelection->purpose == CardSelectionPurpose::IceSwordDiscard; }), "Ice Sword discard reaches TCP actor");
    }
};

void iceSwordNetworkActorReceivesPrompt()
{
    Fixture f; f.beginPrompt(); const auto& selection = f.prompt();
    ok(selection.purpose == CardSelectionPurpose::IceSwordPrompt && selection.targetId == 3 && selection.minCount == 0 && selection.maxCount == 1
           && selection.prompt.find("寒冰剑") != std::string::npos && selection.options.size() == 1
           && selection.options[0].displayName == "Activate Ice Sword" && !selection.options[0].hidden
           && f.server.session().testingEngine().player(3)->hp() == 4,
       "actor receives the pending Ice Sword prompt before damage");
}
void iceSwordNetworkNonActorCannotActivate()
{
    Fixture f; f.beginPrompt(); const auto id = f.prompt().requestId;
    ok(!f.target->view().cardSelection && !f.observer->view().cardSelection, "non-actors receive no Ice Sword interaction");
    f.target->submitCardSelection(id, 1);
    ok(waitFor([&] { return !f.target->actionPending(); }) && f.server.session().testingEngine().pendingCardSelection()
           && f.server.session().testingEngine().pendingCardSelection()->requester == 2 && f.server.session().testingEngine().player(3)->hp() == 4,
       "non-actor TCP activation is rejected without preventing damage");
    ok(f.actor->submitCardSelection(id, std::vector<SelectionOptionId> {}).accepted && waitFor([&] { return f.server.session().testingEngine().player(3)->hp() == 3; }), "actor remains able to pass its own prompt");
}
void iceSwordNetworkActivateStartsDiscard()
{
    Fixture f; f.beginPrompt(); const auto promptId = f.prompt().requestId; f.activate(); const auto& discard = *f.actor->view().cardSelection;
    ok(f.server.session().testingEngine().player(3)->hp() == 4 && discard.purpose == CardSelectionPurpose::IceSwordDiscard && discard.requestId != promptId
           && discard.targetId == 3 && discard.minCount == 1 && discard.maxCount == 1,
       "remote Activate prevents damage and creates a new discard request");
}
void iceSwordNetworkShowsPublicEquipmentCandidate()
{
    Fixture f; f.server.session().testingEngine().testingEquip(3, mount()); f.beginPrompt(); f.activate();
    const auto discard = *f.actor->view().cardSelection;
    const auto option = std::find_if(discard.options.begin(), discard.options.end(), [](const auto& value) { return value.equipmentSlot == EquipmentSlot::OffensiveHorse && value.displayName == "赤兔" && !value.hidden; });
    const auto before = f.server.session().testingEngine().deck().discardPileSize();
    ok(option != discard.options.end() && f.actor->submitCardSelection(discard.requestId, option->optionId).accepted
           && waitFor([&] { return !f.server.session().testingEngine().player(3)->equipment(EquipmentSlot::OffensiveHorse); })
           && f.server.session().testingEngine().deck().discardPileSize() == before + 1,
       "public equipment candidate is serialized and discarded through TCP");
}
void iceSwordNetworkKeepsTargetHandHidden()
{
    Fixture f; f.beginPrompt(); f.activate(); const auto& selection = *f.actor->view().cardSelection;
    int hidden = 0; for (const auto& option : selection.options) if (option.hidden) { ++hidden; ok(option.displayName.empty(), "hidden option omits card display name"); }
    const auto actorPayload = QJsonDocument(f.server.viewPayloadFor(2)).toJson(QJsonDocument::Compact);
    const auto observerPayload = QJsonDocument(f.server.viewPayloadFor(4)).toJson(QJsonDocument::Compact);
    ok(hidden == 2 && !actorPayload.contains("ICE_SECRET_CARD_A") && !actorPayload.contains("ICE_SECRET_NAME") && !observerPayload.contains("ICE_SECRET_CARD_A") && !observerPayload.contains("ICE_SECRET_NAME"),
       "serialized Ice Sword selection hides target hand identities from actor and observer");
}
void iceSwordNetworkCanDiscardHiddenHandCard()
{
    Fixture f; f.beginPrompt(); f.activate(); const auto discard = *f.actor->view().cardSelection;
    const auto hidden = std::find_if(discard.options.begin(), discard.options.end(), [](const auto& value) { return value.hidden; });
    const auto before = f.server.session().testingEngine().player(3)->handCards().size();
    ok(hidden != discard.options.end() && f.actor->submitCardSelection(discard.requestId, hidden->optionId).accepted
           && waitFor([&] { return f.server.session().testingEngine().player(3)->handCards().size() == before - 1; })
           && f.actor->view().cardSelection && f.actor->view().cardSelection->purpose == CardSelectionPurpose::IceSwordDiscard,
       "server resolves an opaque hidden option to exactly one target hand card");
}
void iceSwordNetworkRefreshesSecondDiscard()
{
    Fixture f; f.beginPrompt(); f.activate(); const auto first = *f.actor->view().cardSelection;
    ok(f.actor->submitCardSelection(first.requestId, first.options.front().optionId).accepted, "first TCP discard accepted");
    ok(waitFor([&] { return f.actor->view().cardSelection && f.actor->view().cardSelection->requestId != first.requestId; }), "second request arrives");
    const auto second = *f.actor->view().cardSelection; const auto hand = f.server.session().testingEngine().player(3)->handCards().size();
    f.actor->submitCardSelection(first.requestId, first.options.front().optionId);
    ok(waitFor([&] { return !f.actor->actionPending(); }) && f.actor->view().cardSelection && f.actor->view().cardSelection->requestId == second.requestId
           && f.server.session().testingEngine().player(3)->handCards().size() == hand,
       "stale first discard request cannot mutate the refreshed second request");
}
void iceSwordNetworkRejectsStalePrompt()
{
    Fixture f; f.beginPrompt(); const auto old = f.prompt().requestId;
    ok(f.actor->submitCardSelection(old, std::vector<SelectionOptionId> {}).accepted && waitFor([&] { return !f.server.session().testingEngine().pendingCardSelection(); }), "actor passes prompt");
    f.actor->submitCardSelection(old, 1);
    ok(waitFor([&] { return !f.actor->actionPending(); }) && f.server.session().testingEngine().player(3)->hp() == 3
           && !f.server.session().testingEngine().iceSwordContext(), "stale prompt cannot prevent or replay damage");
}
void iceSwordNetworkRejectsStaleDiscard()
{
    Fixture f; f.beginPrompt(); f.activate(); const auto first = *f.actor->view().cardSelection;
    ok(f.actor->submitCardSelection(first.requestId, first.options.front().optionId).accepted, "first discard accepted");
    ok(waitFor([&] { return f.actor->view().cardSelection && f.actor->view().cardSelection->requestId != first.requestId; }), "second discard pending");
    const auto second = *f.actor->view().cardSelection; const auto hand = f.server.session().testingEngine().player(3)->handCards().size();
    f.actor->submitCardSelection(first.requestId, first.options.front().optionId);
    ok(waitFor([&] { return !f.actor->actionPending(); }) && f.actor->view().cardSelection && f.actor->view().cardSelection->requestId == second.requestId
           && f.server.session().testingEngine().player(3)->handCards().size() == hand,
       "stale discard is rejected and leaves the new request authoritative");
}
void iceSwordNetworkRejectsRequestAfterReturnToLobby()
{
    Fixture f; f.beginPrompt(); const auto old = f.prompt().requestId;
    ok(f.server.returnToLobby() && !f.server.gameStarted(), "host returns to lobby during Ice Sword prompt");
    ok(waitFor([&] { return !f.actor->lobbyState().gameStarted && !f.actor->view().cardSelection; }), "remote actor receives lobby state without Ice Sword interaction");
    ok(f.actor->submitCardSelection(old, 1).accepted, "remote client sends its stale Ice Sword request for authoritative rejection");
    ok(waitFor([&] { return !f.actor->actionPending(); }) && !f.server.gameStarted(), "stale lobby request is authoritatively rejected without restarting the game");
    ok(!f.server.session().testingEngine().iceSwordContext() && !f.server.session().testingEngine().pendingCardSelection(), "stale TCP request cannot restore Ice Sword after return to lobby");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    iceSwordNetworkActorReceivesPrompt(); iceSwordNetworkNonActorCannotActivate(); iceSwordNetworkActivateStartsDiscard();
    iceSwordNetworkShowsPublicEquipmentCandidate(); iceSwordNetworkKeepsTargetHandHidden(); iceSwordNetworkCanDiscardHiddenHandCard();
    iceSwordNetworkRefreshesSecondDiscard(); iceSwordNetworkRejectsStalePrompt(); iceSwordNetworkRejectsStaleDiscard(); iceSwordNetworkRejectsRequestAfterReturnToLobby();
    std::cout << "BasicSanguoshaIceSwordNetworkTests PASS\n";
}
