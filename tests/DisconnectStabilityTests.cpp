#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

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
void expect(bool value, const char* message) { if (!value) fail(message); }
bool waitFor(const std::function<bool()>& condition, int milliseconds = 3000)
{
    if (condition()) return true;
    QEventLoop loop; QTimer timeout, poll;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    timeout.start(milliseconds); poll.start(10); loop.exec(); return condition();
}

struct ThreePlayerGame {
    GameServer server;
    GameClientController host;
    std::unique_ptr<NetworkGameClient> p2, p3;

    ThreePlayerGame() : host(server.session(), 1)
    {
        QString error;
        expect(server.listen(error, 0) && server.setTargetPlayerCount(3), "disconnect test server starts");
        p2 = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort()); p2->connectToHost();
        expect(waitFor([&] { return p2->selfPlayerId() == 2 && server.connectedHumanCount() == 2; }), "P2 joins");
        p3 = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort()); p3->connectToHost();
        expect(waitFor([&] { return p3->selfPlayerId() == 3 && server.connectedHumanCount() == 3; }), "P3 joins");
        expect(server.startLobbyGame() && waitFor([&] { return p2->connected() && p3->connected(); }), "three-player game starts");
        auto& engine = server.session().testingEngine();
        for (const auto& player : engine.players()) {
            std::vector<CardId> ids;
            for (const auto& card : player->handCards()) ids.push_back(card->id());
            for (const auto& id : ids) player->removeCard(id);
        }
    }
    ~ThreePlayerGame()
    {
        if (p2) p2->disconnectFromHost();
        if (p3) p3->disconnectFromHost();
        waitFor([&] { return server.connectedHumanCount() == 1; });
        server.close();
    }
    void advanceToP2()
    {
        expect(host.submit(EndPlayPhaseAction {1}).accepted, "host ends Play phase");
        server.broadcastViews();
        expect(waitFor([&] { return server.session().testingEngine().currentPlayer()->id() == 2
                                  && server.session().testingEngine().currentPhase() == Phase::Play; }), "P2 gets Play phase");
    }
};

void clientDisconnectInLobbyReleasesSeat()
{
    GameServer server; QString error;
    expect(server.listen(error, 0) && server.setTargetPlayerCount(3), "lobby server starts");
    server.setReconnectGracePeriodForTesting(50);
    NetworkGameClient first("127.0.0.1", server.serverPort()); first.connectToHost();
    expect(waitFor([&] { return first.selfPlayerId() == 2; }), "first lobby client gets seat two");
    first.disconnectFromHost();
    expect(waitFor([&] { return server.connectedHumanCount() == 1 && server.testingReconnectReservationCount() == 1; }), "lobby disconnect reserves seat during grace");
    expect(waitFor([&] { return server.testingReconnectReservationCount() == 0; }), "grace cleanup releases the reserved seat");
    NetworkGameClient replacement("127.0.0.1", server.serverPort()); replacement.connectToHost();
    expect(waitFor([&] { return replacement.selfPlayerId() == 2 && server.connectedHumanCount() == 2; }), "replacement gets released smallest seat");
    replacement.disconnectFromHost();
    expect(waitFor([&] { return server.connectedHumanCount() == 1; }), "replacement disconnect settles before fixture destruction");
    server.close();
}

void multipleLobbyDisconnectsRemainStable()
{
    GameServer server; QString error;
    expect(server.listen(error, 0) && server.setTargetPlayerCount(4), "multi-disconnect lobby server starts");
    server.setReconnectGracePeriodForTesting(50);
    NetworkGameClient p2("127.0.0.1", server.serverPort()), p3("127.0.0.1", server.serverPort()), p4("127.0.0.1", server.serverPort());
    p2.connectToHost(); expect(waitFor([&] { return p2.selfPlayerId() == 2; }), "P2 joins lobby");
    p3.connectToHost(); expect(waitFor([&] { return p3.selfPlayerId() == 3; }), "P3 joins lobby");
    p4.connectToHost(); expect(waitFor([&] { return p4.selfPlayerId() == 4; }), "P4 joins lobby");
    p3.disconnectFromHost(); p2.disconnectFromHost();
    expect(waitFor([&] { return server.connectedHumanCount() == 2 && server.testingReconnectReservationCount() == 2; }), "two lobby disconnects reserve both seats during grace");
    expect(waitFor([&] { return server.testingReconnectReservationCount() == 0; }), "grace cleanup releases both reserved seats");
    NetworkGameClient a("127.0.0.1", server.serverPort()), b("127.0.0.1", server.serverPort());
    a.connectToHost(); expect(waitFor([&] { return a.selfPlayerId() == 2; }), "first replacement gets seat two");
    b.connectToHost(); expect(waitFor([&] { return b.selfPlayerId() == 3 && server.connectedHumanCount() == 4; }), "second replacement gets seat three");
    a.disconnectFromHost(); b.disconnectFromHost(); p4.disconnectFromHost();
    expect(waitFor([&] { return server.connectedHumanCount() == 1; }), "multi-disconnect lobby teardown settles");
    server.close();
}

void nonCurrentPlayerDisconnectDuringGameDoesNotCrash()
{
    ThreePlayerGame f;
    f.p3->disconnectFromHost();
    expect(waitFor([&] { return !f.p3->connected() && f.server.connectedHumanCount() == 2; }), "non-current client disconnects");
    f.server.broadcastViews();
    f.host.refresh();
    const auto& players = f.host.view().players;
    expect(players.size() == 3 && !players[2].connected && !f.p2->view().players[2].connected && f.server.session().testingEngine().currentPlayer()->id() == 1,
           "non-current disconnect remains visible without breaking current turn");
}

void currentPlayerDisconnectDuringPlayIsHandledSafely()
{
    ThreePlayerGame f; f.server.setReconnectGracePeriodForTesting(50); f.advanceToP2();
    f.p2->disconnectFromHost();
    expect(waitFor([&] { return !f.p2->connected() && f.server.testingReconnectReservationCount() == 1
                              && f.server.session().testingEngine().currentPlayer()->id() == 2
                              && f.server.session().testingEngine().currentPhase() == Phase::Play; }),
           "current player remains recoverable during reconnect grace");
    expect(waitFor([&] { return f.server.testingReconnectReservationCount() == 0 && f.server.session().testingEngine().currentPlayer()->id() == 3; }),
           "expired current-player reservation uses the existing timeout progression");
}

void responderDisconnectDuringResponseActsAsSafePass()
{
    ThreePlayerGame f; f.server.setReconnectGracePeriodForTesting(50);
    auto& engine = f.server.session().testingEngine();
    engine.players()[0]->addCard(std::make_shared<Card>("disconnect-slash", "Slash", CardType::Slash, Suit::Club, 7));
    expect(f.host.submit(PlayCardAction {1, "disconnect-slash", {2}}).accepted && engine.pendingResponse(), "host creates Dodge response");
    f.p2->disconnectFromHost();
    expect(waitFor([&] { return engine.pendingResponse() && f.server.testingReconnectReservationCount() == 1; }), "response is retained during reconnect grace");
    expect(waitFor([&] { return !engine.pendingResponse() && engine.player(2)->hp() == 3; }), "expired response reservation safely passes Dodge and resolves Slash");
}

void selectionActorDisconnectDoesNotLeavePendingRequest()
{
    ThreePlayerGame f; f.server.setReconnectGracePeriodForTesting(50); f.advanceToP2();
    auto& engine = f.server.session().testingEngine();
    engine.testingEquip(2, std::make_shared<Card>("disconnect-kirin", "\xE9\xBA\x92\xE9\xBA\x9F\xE5\xBC\x93", CardType::Weapon, Suit::Spade, 1, EquipmentData {EquipmentSlot::Weapon, 5}));
    engine.testingEquip(3, std::make_shared<Card>("disconnect-mount", "Mount", CardType::OffensiveHorse, Suit::Heart, 1, EquipmentData {EquipmentSlot::OffensiveHorse, 1}));
    engine.players()[1]->addCard(std::make_shared<Card>("disconnect-kirin-slash", "Slash", CardType::Slash, Suit::Club, 7));
    expect(f.p2->submit(PlayCardAction {2, "disconnect-kirin-slash", {3}}).accepted, "P2 starts Kirin Slash");
    expect(waitFor([&] { return engine.pendingResponse().has_value(); }), "P3 receives Dodge response");
    const auto request = *engine.pendingResponse();
    expect(f.p3->submit(RespondAction {3, request.requestId, std::nullopt}).accepted, "P3 passes Dodge");
    expect(waitFor([&] { return engine.pendingCardSelection().has_value(); }), "Kirin selection is pending for P2");
    f.p2->disconnectFromHost();
    expect(waitFor([&] { return engine.pendingCardSelection() && f.server.testingReconnectReservationCount() == 1; }), "selection is retained during reconnect grace");
    expect(waitFor([&] { return !engine.pendingCardSelection() && !engine.kirinBowContext() && engine.player(3)->equipment(EquipmentSlot::OffensiveHorse); }),
           "expired selection reservation resolves Kirin selection as Pass without discarding mount");
}

void disconnectedClientActionsAreRejected()
{
    ThreePlayerGame f;
    f.p2->disconnectFromHost();
    expect(waitFor([&] { return !f.p2->connected(); }), "P2 disconnects before replay");
    const auto before = f.server.session().testingEngine().player(1)->handCards().size();
    const auto result = f.p2->submit(EndPlayPhaseAction {2});
    expect(!result.accepted && f.server.session().testingEngine().player(1)->handCards().size() == before,
           "disconnected client cannot submit an action or mutate authoritative state");
}

void clientsHandleHostShutdown()
{
    GameServer server; QString error;
    expect(server.listen(error, 0), "shutdown server starts");
    NetworkGameClient client("127.0.0.1", server.serverPort()); client.connectToHost();
    expect(waitFor([&] { return client.selfPlayerId() == 2; }), "client joins before host shutdown");
    server.close();
    expect(waitFor([&] { return client.connectionState() == ConnectionState::ConnectionLost || client.connectionState() == ConnectionState::Disconnected; })
               && !client.connected() && !client.view().response && !client.view().cardSelection && client.view().timeoutKind == TimeoutKind::None,
           "client exits stale interaction state when host shuts down");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto run = [](const char* name, const auto& test) {
        std::cout << "[RUN] " << name << std::endl;
        test();
        std::cout << "[PASS] " << name << std::endl;
    };
    run("clientDisconnectInLobbyReleasesSeat", clientDisconnectInLobbyReleasesSeat);
    run("multipleLobbyDisconnectsRemainStable", multipleLobbyDisconnectsRemainStable);
    run("nonCurrentPlayerDisconnectDuringGameDoesNotCrash", nonCurrentPlayerDisconnectDuringGameDoesNotCrash);
    run("currentPlayerDisconnectDuringPlayIsHandledSafely", currentPlayerDisconnectDuringPlayIsHandledSafely);
    run("responderDisconnectDuringResponseActsAsSafePass", responderDisconnectDuringResponseActsAsSafePass);
    run("selectionActorDisconnectDoesNotLeavePendingRequest", selectionActorDisconnectDoesNotLeavePendingRequest);
    run("disconnectedClientActionsAreRejected", disconnectedClientActionsAreRejected);
    run("clientsHandleHostShutdown", clientsHandleHostShutdown);
    std::cout << "BasicSanguoshaDisconnectStabilityTests PASS\n";
}
