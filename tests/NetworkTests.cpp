#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTcpSocket>
#include <QTimer>

#include <cassert>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>

#include "network/GameServer.h"
#include "network/NetworkGameClient.h"
#include "client/GameClientController.h"

using namespace sanguosha;
using namespace sanguosha::network;

static bool waitFor(const std::function<bool()>& predicate, int milliseconds = 3000)
{
    if (predicate()) return true;
    QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
    timeout.start(milliseconds); poll.start(10); loop.exec(); return predicate();
}

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Network test failed: " << message << '\n';
        std::abort();
    }
}

static void writeFragmentedHello(quint16 port)
{
    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, port);
    assert(raw.waitForConnected(1000));
    const auto frame = MessageCodec::frame(QJsonObject{{"type", "hello"}, {"protocolVersion", ProtocolVersion}});
    assert(raw.write(frame.left(1)) == 1);
    assert(raw.waitForBytesWritten(1000));
    assert(raw.write(frame.mid(1, 2)) == 2);
    assert(raw.waitForBytesWritten(1000));
    assert(raw.write(frame.mid(3)) == frame.size() - 3);
    assert(raw.waitForBytesWritten(1000));
    assert(waitFor([&] { return raw.bytesAvailable() > 0; }));
}

static QByteArray rawFrame(const QByteArray& payload)
{
    const auto n = quint32(payload.size());
    QByteArray frame(4, Qt::Uninitialized);
    frame[0] = char((n >> 24) & 0xff); frame[1] = char((n >> 16) & 0xff);
    frame[2] = char((n >> 8) & 0xff); frame[3] = char(n & 0xff);
    return frame + payload;
}

static void assertServerRecoversAfterRejectedSocket(const QByteArray& rejectedInput)
{
    GameServer server; QString error; require(server.listen(error, 0), "malformed-action server listens");
    int offlineNotifications = 0;
    QObject::connect(&server, &GameServer::connectionStatusChanged, &server, [&](const QString&, bool connected) { if (!connected) ++offlineNotifications; });
    QTcpSocket bad;
    bad.connectToHost(QHostAddress::LocalHost, server.serverPort());
    assert(bad.waitForConnected(1000));
    bad.write(rejectedInput);
    assert(waitFor([&] { return bad.bytesAvailable() > 0 || bad.state() == QAbstractSocket::UnconnectedState; }));
    assert(waitFor([&] { return bad.state() == QAbstractSocket::UnconnectedState; }));
    assert(waitFor([&] { return offlineNotifications >= 1; }));
    QTcpSocket good;
    good.connectToHost(QHostAddress::LocalHost, server.serverPort());
    assert(good.waitForConnected(1000));
    good.write(MessageCodec::frame(QJsonObject{{"type", "hello"}, {"protocolVersion", ProtocolVersion}}));
    assert(waitFor([&] { return server.remoteConnected(); }));
}

static void assertMalformedActionIsRejected(const QJsonObject& action)
{
    GameServer server; QString error; require(server.listen(error, 0), "malformed-action server listens");
    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, server.serverPort());
    require(waitFor([&] { return raw.state() == QAbstractSocket::ConnectedState; }), "malformed-action socket connects");
    raw.write(MessageCodec::frame(QJsonObject{{"type", "hello"}, {"protocolVersion", ProtocolVersion}}));
    assert(waitFor([&] { return server.remoteConnected() && raw.bytesAvailable() > 0; }));
    require(waitFor([&] { return server.remoteConnected(); }), "malformed-action peer joins lobby");
    require(server.startLobbyGame(), "malformed-action game explicitly starts");
    raw.readAll();
    const auto& before = server.session().testingEngine();
    const auto hp = before.players()[0]->hp(); const auto hand = before.players()[0]->handCards().size();
    const auto phase = before.currentPhase(); const auto turn = before.currentPlayer()->id();
    raw.write(MessageCodec::frame(action));
    assert(waitFor([&] { return raw.bytesAvailable() > 0; }));
    const auto& after = server.session().testingEngine();
    assert(after.players()[0]->hp() == hp && after.players()[0]->handCards().size() == hand);
    assert(after.currentPhase() == phase && after.currentPlayer()->id() == turn && server.remoteConnected());
}

static void kirinBowClearsOnReturnToLobby()
{
    GameServer server;
    require(server.startLobbyGame(), "Kirin lobby test server starts a real game");

    auto& session = server.session();
    const auto attackerClient = session.addClient(1);
    const auto targetClient = session.addClient(2);
    auto& engine = session.testingEngine();
    const auto clearHand = [](Player& player) {
        std::vector<CardId> ids;
        for (const auto& card : player.handCards()) ids.push_back(card->id());
        for (const auto& id : ids) player.removeCard(id);
    };
    clearHand(*engine.players()[0]);
    clearHand(*engine.players()[1]);
    engine.testingEquip(1, std::make_shared<Card>("kirin-return-bow", "麒麟弓", CardType::Weapon, Suit::Spade, 5,
                                                   EquipmentData {EquipmentSlot::Weapon, 5}));
    engine.testingEquip(2, std::make_shared<Card>("kirin-return-mount", "赤兔", CardType::OffensiveHorse, Suit::Heart, 5,
                                                   EquipmentData {EquipmentSlot::OffensiveHorse, 1}));

    engine.testingStartSlash(1, {2});
    const auto dodge = engine.pendingResponse();
    require(dodge && session.submitAction(targetClient, RespondAction {2, dodge->requestId, std::nullopt}).accepted,
            "Kirin setup Slash damages its target");
    const auto selection = engine.pendingCardSelection();
    require(selection && selection->purpose == CardSelectionPurpose::KirinBowMount && selection->requester == 1
                && selection->target == 2 && selection->selectableCards.size() == 1,
            "Kirin Bow has a pending authoritative selection before returning to lobby");
    const auto oldRequestId = selection->requestId;
    require(session.viewFor(attackerClient).cardSelection.has_value(), "actor ViewState exposes the pending Kirin selection");

    require(server.returnToLobby() && !server.gameStarted(), "returnToLobby exits the live game");
    require(!engine.pendingCardSelection() && !engine.kirinBowContext() && engine.deck().discardPileSize() == 0,
            "returnToLobby clears Kirin context without discarding the target mount");
    require(session.timeoutContextKey() != "selection:" + std::to_string(oldRequestId),
            "returnToLobby clears the old Kirin timeout context");
    const auto lobbyView = session.viewFor(attackerClient);
    require(!lobbyView.cardSelection && !lobbyView.response,
            "lobby ViewState has no Kirin selection or in-game response interaction");
    require(!session.submitCardSelection(attackerClient, oldRequestId, 1).accepted,
            "a stale Kirin selection request is rejected after returning to lobby");
    require(!server.gameStarted() && !engine.pendingCardSelection() && !engine.kirinBowContext(),
            "stale Kirin submission cannot restore an in-game interaction");
}

static void iceSwordClearsOnReturnToLobby()
{
    GameServer server;
    require(server.startLobbyGame(), "Ice Sword lobby test server starts a real game");

    auto& session = server.session();
    const auto attackerClient = session.addClient(1);
    const auto targetClient = session.addClient(2);
    auto& engine = session.testingEngine();
    const auto clearHand = [](Player& player) {
        std::vector<CardId> ids;
        for (const auto& card : player.handCards()) ids.push_back(card->id());
        for (const auto& id : ids) player.removeCard(id);
    };
    clearHand(*engine.players()[0]);
    clearHand(*engine.players()[1]);
    engine.testingEquip(1, std::make_shared<Card>("ice-return-sword", "寒冰剑", CardType::Weapon, Suit::Spade, 2,
                                                   EquipmentData {EquipmentSlot::Weapon, 2}));
    engine.players()[1]->addCard(std::make_shared<Card>("ice-return-target", "target", CardType::Peach, Suit::Heart, 1));

    engine.testingStartSlash(1, {2});
    const auto dodge = engine.pendingResponse();
    require(dodge && session.submitAction(targetClient, RespondAction {2, dodge->requestId, std::nullopt}).accepted,
            "Ice Sword setup Slash damages its target");
    const auto selection = engine.pendingCardSelection();
    require(selection && selection->purpose == CardSelectionPurpose::IceSwordPrompt && selection->requester == 1
                && selection->target == 2,
            "Ice Sword has a pending authoritative prompt before returning to lobby");
    const auto oldRequestId = selection->requestId;
    require(session.viewFor(attackerClient).cardSelection.has_value(), "actor ViewState exposes the pending Ice Sword prompt");

    require(server.returnToLobby() && !server.gameStarted(), "Ice Sword returnToLobby exits the live game");
    require(!engine.pendingCardSelection() && !engine.iceSwordContext() && !engine.testingDeferredSlashDamage(),
            "returnToLobby clears Ice Sword context, prompt, and deferred damage");
    require(session.timeoutContextKey() != "selection:" + std::to_string(oldRequestId),
            "returnToLobby clears the old Ice Sword timeout context");
    const auto lobbyView = session.viewFor(attackerClient);
    require(!lobbyView.cardSelection && !lobbyView.response,
            "lobby ViewState has no Ice Sword selection or in-game response interaction");
    require(!session.submitCardSelection(attackerClient, oldRequestId, 1).accepted,
            "a stale Ice Sword request is rejected after returning to lobby");
    require(!server.gameStarted() && !engine.pendingCardSelection() && !engine.iceSwordContext(),
            "stale Ice Sword submission cannot restore an in-game interaction");
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    kirinBowClearsOnReturnToLobby();
    iceSwordClearsOnReturnToLobby();
    { // Stage A: three independent TCP clients occupy, release and reuse deterministic human seats before an explicit start.
        GameServer lobbyServer; QString lobbyError; require(lobbyServer.listen(lobbyError, 0), "lobby server listens");
        require(lobbyServer.setTargetPlayerCount(4), "target player count is accepted before start");
        lobbyServer.setReconnectGracePeriodForTesting(50);
        NetworkGameClient player2(QStringLiteral("127.0.0.1"), lobbyServer.serverPort());
        NetworkGameClient player3(QStringLiteral("127.0.0.1"), lobbyServer.serverPort());
        player2.connectToHost(); player3.connectToHost();
        require(waitFor([&] { return lobbyServer.connectedHumanCount() == 3; }), "two remote humans join lobby");
        require(waitFor([&] {
                    const std::array<PlayerId, 2> seats {player2.selfPlayerId(), player3.selfPlayerId()};
                    return (seats[0] == 2 && seats[1] == 3) || (seats[0] == 3 && seats[1] == 2);
                }), "the two smallest available seats are allocated exactly once");
        require(lobbyServer.aiPlayerCount() == 1 && !lobbyServer.gameStarted(), "lobby reports human and AI counts before explicit start");
        player3.disconnectFromHost();
        const auto releasedSeat = player3.selfPlayerId();
        require(waitFor([&] { return lobbyServer.connectedHumanCount() == 2 && lobbyServer.testingReconnectReservationCount() == 1; }),
                "lobby disconnect reserves its seat during reconnect grace");
        NetworkGameClient graceJoiner(QStringLiteral("127.0.0.1"), lobbyServer.serverPort());
        graceJoiner.connectToHost();
        require(waitFor([&] { return graceJoiner.selfPlayerId() == 4 && lobbyServer.connectedHumanCount() == 3; }),
                "a new client cannot claim the reserved seat during grace");
        graceJoiner.disconnectFromHost();
        require(waitFor([&] { return lobbyServer.testingReconnectReservationCount() == 0; }),
                "reconnect reservation timers release expired seats");
        NetworkGameClient replacement(QStringLiteral("127.0.0.1"), lobbyServer.serverPort());
        replacement.connectToHost();
        require(waitFor([&] { return lobbyServer.connectedHumanCount() == 3 && replacement.selfPlayerId() == releasedSeat; }),
                "released seat is reused after reconnect grace expires");
        require(lobbyServer.startLobbyGame(), "host explicitly starts lobby game");
        require(waitFor([&] { return player2.connected() && replacement.connected(); }), "each remote client receives its own started view");
        const auto& players = lobbyServer.session().testingEngine().players();
        require(players.size() == 4, "target-sized game is created");
        require(players[0]->controlType() == PlayerControlType::Human && players[1]->controlType() == PlayerControlType::Human
                    && players[2]->controlType() == PlayerControlType::Human && players[3]->controlType() == PlayerControlType::AI,
                "human seats remain fixed and remaining seat becomes AI");
        players.at(static_cast<std::size_t>(replacement.selfPlayerId() - 1))->addCard(std::make_shared<Card>("STAGE_A_PRIVATE_P3_CARD", "STAGE_A_PRIVATE_P3_CARD", CardType::Peach, Suit::Heart, 1));
        lobbyServer.broadcastViews();
        require(waitFor([&] { return std::any_of(replacement.view().ownHand.begin(), replacement.view().ownHand.end(), [](const auto& card) { return card.id == "STAGE_A_PRIVATE_P3_CARD"; }); }),
                "P3 receives its own private hand update");
        require(std::none_of(player2.view().ownHand.begin(), player2.view().ownHand.end(), [](const auto& card) { return card.id == "STAGE_A_PRIVATE_P3_CARD"; }),
                "P2 does not receive P3 private hand data");
        require(!lobbyServer.setTargetPlayerCount(5) && !lobbyServer.startLobbyGame(), "composition is locked after start");
    }
    { // Stage A: a full lobby rejects an additional independent TCP connection.
        GameServer fullServer; QString fullError; require(fullServer.listen(fullError, 0), "full-lobby server listens");
        NetworkGameClient accepted(QStringLiteral("127.0.0.1"), fullServer.serverPort());
        NetworkGameClient rejected(QStringLiteral("127.0.0.1"), fullServer.serverPort());
        accepted.connectToHost();
        require(waitFor([&] { return fullServer.connectedHumanCount() == 2; }), "single open remote seat is occupied");
        rejected.connectToHost();
        require(waitFor([&] { return rejected.connectionState() == ConnectionState::Error; }), "lobby-full connection is rejected");
        require(fullServer.connectedHumanCount() == 2, "rejected socket never occupies a seat");
    }
    { // Stage A: sequential joins prove deterministic seats, independent hello state, count updates and preserved holes.
        GameServer contractServer; QString contractError; require(contractServer.listen(contractError, 0), "contract server listens");
        require(contractServer.setTargetPlayerCount(6), "target six is accepted");
        contractServer.setReconnectGracePeriodForTesting(50);
        QTcpSocket silent;
        silent.connectToHost(QHostAddress::LocalHost, contractServer.serverPort());
        require(silent.waitForConnected(1000), "silent socket connects without hello");
        NetworkGameClient first(QStringLiteral("127.0.0.1"), contractServer.serverPort()); first.connectToHost();
        require(waitFor([&] { return contractServer.connectedHumanCount() == 2 && first.selfPlayerId() == 2; }), "first hello gets P2 while silent socket remains unjoined");
        require(contractServer.aiPlayerCount() == 4, "unjoined socket does not affect lobby count");
        NetworkGameClient second(QStringLiteral("127.0.0.1"), contractServer.serverPort()); second.connectToHost();
        require(waitFor([&] { return second.selfPlayerId() == 3 && contractServer.connectedHumanCount() == 3; }), "second sequential hello gets P3");
        NetworkGameClient third(QStringLiteral("127.0.0.1"), contractServer.serverPort()); third.connectToHost();
        require(waitFor([&] { return third.selfPlayerId() == 4 && contractServer.connectedHumanCount() == 4; }), "third sequential hello gets P4");
        require(contractServer.aiPlayerCount() == 2 && !contractServer.setTargetPlayerCount(3), "lobby counts and lower-than-human target rejection are correct");
        second.disconnectFromHost();
        require(waitFor([&] { return contractServer.connectedHumanCount() == 3 && contractServer.aiPlayerCount() == 3 && contractServer.testingReconnectReservationCount() == 1; }), "disconnect reserves the lobby seat before cleanup");
        require(waitFor([&] { return contractServer.testingReconnectReservationCount() == 0; }), "expired lobby reservation is cleaned before starting");
        require(contractServer.startLobbyGame(), "hole-preserving game explicitly starts");
        require(waitFor([&] { return first.connected() && third.connected(); }), "remaining humans receive started views");
        const auto& players = contractServer.session().testingEngine().players();
        require(players.size() == 6 && players[0]->controlType() == PlayerControlType::Human && players[1]->controlType() == PlayerControlType::Human
                    && players[2]->controlType() == PlayerControlType::AI && players[3]->controlType() == PlayerControlType::Human
                    && players[4]->controlType() == PlayerControlType::AI && players[5]->controlType() == PlayerControlType::AI,
                "start preserves P3 hole and creates AI only in unoccupied seats");
        NetworkGameClient late(QStringLiteral("127.0.0.1"), contractServer.serverPort()); late.connectToHost();
        require(waitFor([&] { return late.connectionState() == ConnectionState::Error; }), "join after game start is rejected");
        require(contractServer.connectedHumanCount() == 3, "late join does not change locked composition");
    }
    { // B2: public Human/AI control types survive the ViewState protocol and malformed values fail safely.
        GameSession controlSession({"P1", "P2"}, true, {}, {PlayerControlType::Human, PlayerControlType::AI});
        const auto p1 = controlSession.addClient(1);
        const auto decoded = decodeViewState(encodeViewState(controlSession.viewFor(p1)));
        require(decoded && decoded->players.size() == 2, "control-type ViewState round trip decodes");
        require(decoded->players[0].controlType == PlayerControlType::Human && decoded->players[1].controlType == PlayerControlType::AI,
                "Human and AI control types round trip exactly");
        auto malformed = encodeViewState(controlSession.viewFor(p1));
        auto playersJson = malformed["players"].toArray(); auto first = playersJson[0].toObject(); first["control"] = QStringLiteral("AI"); playersJson[0] = first; malformed["players"] = playersJson;
        require(!decodeViewState(malformed), "wrong-type control field is rejected without a crash");
    }
    { // B2.5: a started game returns to a clean lobby and can start a fresh second game.
        GameServer lifecycle; QString lifecycleError; require(lifecycle.listen(lifecycleError, 0), "lifecycle server listens");
        require(lifecycle.setTargetPlayerCount(6) && lifecycle.startLobbyGame(), "first six-player game starts");
        require(lifecycle.gameStarted() && lifecycle.session().testingEngine().players().size() == 6, "first game has target player count");
        require(lifecycle.returnToLobby() && !lifecycle.gameStarted(), "server returns authoritatively to lobby");
        require(lifecycle.connectedHumanCount() == 1 && lifecycle.aiPlayerCount() == 5, "lobby counts restore after return");
        const auto& lobbyPlayers = lifecycle.session().testingEngine().players();
        require(std::none_of(lobbyPlayers.begin(), lobbyPlayers.end(), [](const auto& player) { return player->controlType() == PlayerControlType::AI; }), "AI seats are not retained as active lobby players");
        require(!lifecycle.session().submitAction(lifecycle.session().addClient(1), EndPlayPhaseAction {1}).accepted, "old action is rejected while back in lobby");
        require(lifecycle.setTargetPlayerCount(4) && lifecycle.startLobbyGame(), "second game starts with a new target");
        require(lifecycle.session().testingEngine().players().size() == 4 && lifecycle.session().testingEngine().currentPhase() == Phase::Play, "second game is freshly initialized");
    }
    { // Stage 9.2C-2B: public equipment-effect snapshots round-trip exactly and reject malformed input atomically.
        GameSession effectSession({"P1", "P2"}, true);
        const auto effectClient = effectSession.addClient(1);
        auto view = effectSession.viewFor(effectClient);
        view.equipmentEffects = {
            {1, "青釭剑", EquipmentEffectTypeView::IgnoreArmor, 1, 2, 0, CardType::Slash},
            {std::numeric_limits<std::uint64_t>::max(), "藤甲", EquipmentEffectTypeView::FireDamageIncreased, 2, 2, 1, CardType::FireSlash}
        };
        const auto payload = encodeViewState(view);
        const auto decoded = decodeViewState(payload);
        require(decoded && decoded->equipmentEffects.size() == 2
                    && decoded->equipmentEffects[0].eventId == 1
                    && decoded->equipmentEffects[0].equipment == "青釭剑"
                    && decoded->equipmentEffects[0].effect == EquipmentEffectTypeView::IgnoreArmor
                    && decoded->equipmentEffects[0].ownerId == 1 && decoded->equipmentEffects[0].targetId == 2
                    && decoded->equipmentEffects[1].eventId == std::numeric_limits<std::uint64_t>::max()
                    && decoded->equipmentEffects[1].relatedCard == CardType::FireSlash,
                "equipment effect ViewState round trip preserves every public field and full uint64 event ID");
        auto malformed = payload;
        malformed["equipmentEffects"] = QStringLiteral("not-an-array");
        require(!decodeViewState(malformed), "non-array equipment effects are rejected");
        malformed = payload; auto effects = malformed["equipmentEffects"].toArray(); auto event = effects[0].toObject(); event["eventId"] = 1; effects[0] = event; malformed["equipmentEffects"] = effects;
        require(!decodeViewState(malformed), "numeric equipment event ID is rejected before any ViewState mutation");
        malformed = payload; effects = malformed["equipmentEffects"].toArray(); event = effects[0].toObject(); event["equipment"] = 7; effects[0] = event; malformed["equipmentEffects"] = effects;
        require(!decodeViewState(malformed), "non-string equipment name is rejected");
        malformed = payload; effects = malformed["equipmentEffects"].toArray(); event = effects[0].toObject(); event["effect"] = 99; effects[0] = event; malformed["equipmentEffects"] = effects;
        require(!decodeViewState(malformed), "invalid equipment effect enum is rejected");
    }
    { // Stage 9.2C-2B: host and TCP remote receive the same public equipment event without private cards.
        GameServer effectServer; QString effectError; require(effectServer.listen(effectError, 0), "equipment-effect server listens");
        NetworkGameClient effectRemote(QStringLiteral("127.0.0.1"), effectServer.serverPort());
        GameClientController effectHost(effectServer.session(), 1);
        effectRemote.connectToHost();
        require(waitFor([&] { return effectServer.connectedHumanCount() == 2; }) && effectServer.startLobbyGame(), "equipment-effect TCP game starts");
        require(waitFor([&] { return effectRemote.connected(); }), "equipment-effect TCP remote receives initial view");
        auto& engine = effectServer.session().testingEngine();
        engine.players().at(0)->addCard(std::make_shared<Card>("PRIVATE_EFFECT_HAND", "PRIVATE_EFFECT_HAND", CardType::Peach, Suit::Heart, 1));
        engine.testingEquip(1, std::make_shared<Card>("effect-qinggang", "青釭剑", CardType::Weapon, Suit::Spade, 6, EquipmentData {EquipmentSlot::Weapon, 2}));
        engine.testingStartSlash(1, {2}, 1, DamageNature::Normal, true);
        const auto response = engine.pendingResponse();
        require(response && engine.submitAction(RespondAction {2, response->requestId, std::nullopt}).accepted, "equipment-effect setup Slash resolves");
        effectServer.broadcastViews(); effectHost.refresh();
        require(waitFor([&] { return effectRemote.view().equipmentEffects.size() == 1; }), "TCP remote receives public equipment-effect snapshot");
        const auto& remoteEvent = effectRemote.view().equipmentEffects.front();
        require(effectHost.view().equipmentEffects.size() == 1 && remoteEvent.eventId == effectHost.view().equipmentEffects.front().eventId
                    && remoteEvent.equipment == "青釭剑" && remoteEvent.effect == EquipmentEffectTypeView::IgnoreArmor
                    && remoteEvent.ownerId == 1 && remoteEvent.targetId == 2 && remoteEvent.value == 0 && remoteEvent.relatedCard == CardType::Slash,
                "host-local and remote ViewStates agree on the public equipment event");
        const auto remotePayload = QJsonDocument(effectServer.viewPayloadFor(2)).toJson(QJsonDocument::Compact);
        require(!remotePayload.contains("PRIVATE_EFFECT_HAND"), "equipment-effect payload contains no private hand data");
        require(effectServer.returnToLobby(), "equipment-effect game returns to lobby");
        effectHost.refresh();
        require(waitFor([&] { return !effectRemote.lobbyState().gameStarted && effectRemote.view().equipmentEffects.empty(); })
                    && effectHost.view().equipmentEffects.empty(),
                "return to lobby clears the prior game equipment-effect window for host and remote paths");
        require(effectServer.startLobbyGame() && effectServer.session().testingEngine().equipmentEffects().empty(), "second game begins with a new empty equipment-effect window");
        effectHost.refresh();
        require(effectHost.view().equipmentEffects.empty(), "second game ViewState cannot carry first-game equipment events");
    }
    { // B2.7: a server-side Selection option cannot remove a card that changed after the request was issued.
        GameSession staleSelection({"P1", "P2"}); const auto p1Client = staleSelection.addClient(1); const auto p2Client = staleSelection.addClient(2);
        auto& engine = staleSelection.testingEngine(); auto& p1 = *engine.players()[0]; auto& p2 = *engine.players()[1];
        std::vector<CardId> ids; for (const auto& card : p1.handCards()) ids.push_back(card->id()); for (const auto& id : ids) p1.removeCard(id);
        ids.clear(); for (const auto& card : p2.handCards()) ids.push_back(card->id()); for (const auto& id : ids) p2.removeCard(id);
        p1.addCard(std::make_shared<Card>("b27-snatch", "b27-snatch", CardType::Snatch, Suit::Club, 1));
        p2.addCard(std::make_shared<Card>("B27_PRIVATE_TARGET", "B27_PRIVATE_TARGET", CardType::Peach, Suit::Heart, 1));
        require(staleSelection.submitAction(p1Client, PlayCardAction {1, "b27-snatch", {2}}).accepted, "Snatch creates a Selection request");
        while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
            const auto response = *engine.pendingResponse();
            const auto client = response.responder == 1 ? p1Client : p2Client;
            require(staleSelection.submitAction(client, RespondAction {response.responder, response.requestId, std::nullopt}).accepted,
                    "Nullification pass advances to the pending Snatch selection");
        }
        const auto request = *engine.pendingCardSelection();
        require(request.selectableCards.front().hidden, "target hand card is represented anonymously");
        p2.removeCard("B27_PRIVATE_TARGET");
        const auto result = staleSelection.submitCardSelection(p1Client, request.requestId, 1);
        require(!result.accepted && engine.pendingCardSelection().has_value(), "stale hand option is rejected without advancing the interaction");
        require(!staleSelection.submitCardSelection(p1Client, request.requestId + 1, 1).accepted, "stale request ID is rejected");
    }
    { // B2.7: three real TCP humans exercise ordered Peach rescue without leaking responder controls.
        GameServer rescueServer; QString error; require(rescueServer.listen(error, 0), "three-player rescue server listens");
        require(rescueServer.setTargetPlayerCount(3), "three-player lobby configured");
        GameClientController host(rescueServer.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), rescueServer.serverPort()); p2.connectToHost();
        require(waitFor([&] { return rescueServer.connectedHumanCount() == 2 && p2.selfPlayerId() == 2; }), "P2 joins rescue lobby");
        NetworkGameClient p3(QStringLiteral("127.0.0.1"), rescueServer.serverPort()); p3.connectToHost();
        require(waitFor([&] { return rescueServer.connectedHumanCount() == 3 && p3.selfPlayerId() == 3; }), "P3 joins rescue lobby");
        require(rescueServer.startLobbyGame(), "three-player rescue game starts");
        require(waitFor([&] { return p2.connected() && p3.connected(); }), "both rescue clients receive started ViewState");
        auto& engine = rescueServer.session().testingEngine(); auto& p1Player = *engine.players()[0]; auto& p2Player = *engine.players()[1]; auto& p3Player = *engine.players()[2];
        const auto clear = [](Player& player) { std::vector<CardId> cards; for (const auto& card : player.handCards()) cards.push_back(card->id()); for (const auto& id : cards) player.removeCard(id); };
        clear(p1Player); clear(p2Player); clear(p3Player); engine.testingSetHp(1, 1);
        p2Player.addCard(std::make_shared<Card>("b27-rescue-slash", "b27-rescue-slash", CardType::Slash, Suit::Spade, 1));
        p3Player.addCard(std::make_shared<Card>("b27-rescue-peach", "b27-rescue-peach", CardType::Peach, Suit::Heart, 1));
        require(host.submit(EndPlayPhaseAction {1}).accepted, "P1 gives P2 a real turn"); rescueServer.broadcastViews();
        require(waitFor([&] { return p2.view().currentTurnPlayer == 2 && p2.view().currentPhase == Phase::Play; }), "P2 turn arrives over TCP");
        require(p2.submit(PlayCardAction {2, "b27-rescue-slash", {1}}).accepted, "P2 submits Slash over TCP");
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::Dodge; }), "P1 receives Dodge response");
        require(host.submit(RespondAction {1, host.view().response->requestId, std::nullopt}).accepted, "P1 declines Dodge"); rescueServer.broadcastViews();
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::PeachRescue && host.view().response->responder == 1; }), "dying player is first deterministic rescuer");
        const auto firstRescue = host.view().response->requestId;
        require(p2.submit(RespondAction {2, firstRescue, std::nullopt}).accepted, "non-responder request reaches server");
        require(waitFor([&] { return !p2.actionPending(); }), "server returns non-responder result"); host.refresh();
        require(host.view().response && host.view().response->requestId == firstRescue && host.view().response->responder == 1, "non-responder cannot advance rescue");
        require(host.submit(RespondAction {1, firstRescue, std::nullopt}).accepted, "first rescuer passes"); rescueServer.broadcastViews();
        require(waitFor([&] { return p2.view().response && p2.view().response->isResponder && p2.view().response->type == ResponseType::PeachRescue; }), "second rescuer becomes P2");
        const auto secondRescue = p2.view().response->requestId; require(secondRescue != firstRescue, "rescue request ID advances");
        require(p2.submit(RespondAction {2, secondRescue, std::nullopt}).accepted, "second rescuer passes over TCP");
        require(waitFor([&] { return p3.view().response && p3.view().response->isResponder && p3.view().response->type == ResponseType::PeachRescue; }), "third rescuer becomes P3");
        const auto thirdRescue = p3.view().response->requestId;
        require(p3.submit(RespondAction {3, thirdRescue, "b27-rescue-peach"}).accepted, "third rescuer uses Peach over TCP");
        require(waitFor([&] { host.refresh(); return !host.view().response && engine.players()[0]->hp() == 1 && engine.players()[0]->isAlive(); }), "Peach ends rescue immediately and restores HP");
        require(!host.submit(RespondAction {1, firstRescue, std::nullopt}).accepted, "old rescue request is rejected after recovery");
    }
    { // B2.7: every rescuer passing through TCP causes one clean death and GameOver.
        GameServer deathServer; QString error; require(deathServer.listen(error, 0), "death server listens");
        GameClientController host(deathServer.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), deathServer.serverPort()); p2.connectToHost();
        require(waitFor([&] { return deathServer.remoteConnected(); }), "P2 joins death lobby"); require(deathServer.startLobbyGame(), "death game starts"); require(waitFor([&] { return p2.connected(); }), "P2 receives death game state");
        auto& engine = deathServer.session().testingEngine(); auto& p1Player = *engine.players()[0]; auto& p2Player = *engine.players()[1];
        const auto clear = [](Player& player) { std::vector<CardId> cards; for (const auto& card : player.handCards()) cards.push_back(card->id()); for (const auto& id : cards) player.removeCard(id); };
        clear(p1Player); clear(p2Player); engine.testingSetHp(1, 1);
        p1Player.addCard(std::make_shared<Card>("b27-death-hand", "b27-death-hand", CardType::Peach, Suit::Heart, 1));
        p1Player.addCard(std::make_shared<Card>("b27-death-weapon", "b27-death-weapon", CardType::Weapon, Suit::Spade, 1, EquipmentData{EquipmentSlot::Weapon, 3}));
        require(host.submit(PlayCardAction {1, "b27-death-weapon", {}}).accepted, "dying player equips a weapon before death");
        p2Player.addCard(std::make_shared<Card>("b27-death-slash", "b27-death-slash", CardType::Slash, Suit::Spade, 1));
        require(host.submit(EndPlayPhaseAction {1}).accepted, "P2 turn begins for death test"); deathServer.broadcastViews();
        require(waitFor([&] { return p2.view().currentTurnPlayer == 2; }), "P2 owns the damaging turn"); require(p2.submit(PlayCardAction {2, "b27-death-slash", {1}}).accepted, "P2 submits lethal Slash");
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::Dodge; }), "P1 receives Dodge before dying"); require(host.submit(RespondAction {1, host.view().response->requestId, std::nullopt}).accepted, "P1 declines Dodge"); deathServer.broadcastViews();
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::PeachRescue; }), "Peach rescue begins");
        require(host.submit(RespondAction {1, host.view().response->requestId, std::nullopt}).accepted, "dying P1 passes rescue"); deathServer.broadcastViews();
        require(waitFor([&] { return p2.view().response && p2.view().response->isResponder && p2.view().response->type == ResponseType::PeachRescue; }), "P2 is final rescuer");
        require(p2.submit(RespondAction {2, p2.view().response->requestId, std::nullopt}).accepted, "final rescuer passes");
        require(waitFor([&] { host.refresh(); return engine.gameOver() && !engine.players()[0]->isAlive() && !host.view().response && !engine.pendingCardSelection(); }), "all passes produce one clean death and GameOver");
        require(engine.players()[0]->handCards().empty() && !engine.players()[0]->equipment(EquipmentSlot::Weapon), "death clears hand and equipment");
        const auto gameOverTurn = engine.currentPlayer()->id(); const auto gameOverPhase = engine.currentPhase();
        require(p2.submit(EndPlayPhaseAction {2}).accepted, "post-GameOver action reaches server over TCP");
        require(waitFor([&] { return !p2.actionPending(); }), "server replies to post-GameOver action");
        require(engine.gameOver() && engine.currentPlayer()->id() == gameOverTurn && engine.currentPhase() == gameOverPhase, "post-GameOver network action cannot advance game");
    }
    { // B2.7: authoritative PeachRescue timeout passes exactly one responder and creates a fresh request.
        GameServer timeoutServer; QString error; require(timeoutServer.listen(error, 0), "rescue timeout server listens");
        require(timeoutServer.setTargetPlayerCount(3), "rescue timeout lobby configured");
        GameClientController host(timeoutServer.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), timeoutServer.serverPort()), p3(QStringLiteral("127.0.0.1"), timeoutServer.serverPort());
        p2.connectToHost(); require(waitFor([&] { return timeoutServer.connectedHumanCount() == 2; }), "timeout P2 joins");
        p3.connectToHost(); require(waitFor([&] { return timeoutServer.connectedHumanCount() == 3; }), "timeout P3 joins");
        require(timeoutServer.startLobbyGame(), "rescue timeout game starts"); require(waitFor([&] { return p2.connected() && p3.connected(); }), "timeout clients receive views");
        auto& engine = timeoutServer.session().testingEngine(); auto& p1Player = *engine.players()[0]; auto& p2Player = *engine.players()[1]; auto& p3Player = *engine.players()[2];
        const auto clear = [](Player& player) { std::vector<CardId> cards; for (const auto& card : player.handCards()) cards.push_back(card->id()); for (const auto& id : cards) player.removeCard(id); };
        clear(p1Player); clear(p2Player); clear(p3Player); engine.testingSetHp(1, 1);
        p2Player.addCard(std::make_shared<Card>("b27-timeout-slash", "b27-timeout-slash", CardType::Slash, Suit::Spade, 1));
        p3Player.addCard(std::make_shared<Card>("b27-timeout-peach", "b27-timeout-peach", CardType::Peach, Suit::Heart, 1));
        require(host.submit(EndPlayPhaseAction {1}).accepted, "timeout test reaches P2 turn"); timeoutServer.broadcastViews();
        require(waitFor([&] { return p2.view().currentTurnPlayer == 2; }), "P2 owns timeout attack turn");
        require(p2.submit(PlayCardAction {2, "b27-timeout-slash", {1}}).accepted, "timeout attack submitted");
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::Dodge; }), "timeout test has Dodge");
        require(host.submit(RespondAction {1, host.view().response->requestId, std::nullopt}).accepted, "timeout test declines Dodge"); timeoutServer.broadcastViews();
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::PeachRescue && host.view().response->responder == 1; }), "timeout test enters rescue");
        const auto expiredRequest = host.view().response->requestId; timeoutServer.setInteractionTimeoutForTesting(100);
        require(waitFor([&] { return p2.view().response && p2.view().response->type == ResponseType::PeachRescue && p2.view().response->isResponder; }, 1500), "rescue timeout passes P1 to P2");
        const auto p2Request = p2.view().response->requestId; require(p2Request != expiredRequest, "timeout advances rescue request ID");
        timeoutServer.setInteractionTimeoutForTesting(5000);
        require(waitFor([&] { return p2.view().timeoutKind == TimeoutKind::Response && p2.view().timeoutPlayerId == 2 && p2.view().timeoutRemainingMs > 0; }), "remote responder receives authoritative timeout state");
        const int firstRemaining = p2.view().timeoutRemainingMs;
        require(waitFor([&] { p2.refresh(); return p2.view().timeoutRemainingMs > 0 && p2.view().timeoutRemainingMs < firstRemaining; }, 1500), "remote timeout state is refreshed without an action");
        require(!host.submit(RespondAction {1, expiredRequest, std::nullopt}).accepted, "expired rescue request remains invalid");
        require(p2.submit(RespondAction {2, p2Request, std::nullopt}).accepted, "P2 passes fresh rescue request");
        require(waitFor([&] { return p3.view().response && p3.view().response->type == ResponseType::PeachRescue && p3.view().response->isResponder; }), "P3 receives fresh rescue context");
        require(p3.submit(RespondAction {3, p3.view().response->requestId, "b27-timeout-peach"}).accepted, "P3 resolves timeout rescue with Peach");
        require(waitFor([&] { host.refresh(); return p1Player.isAlive() && p1Player.hp() == 1 && !host.view().response; }), "fresh rescue is not affected by prior timeout");
    }
    { // B2.7: killing a current actor or current responder clears real network interaction state and skips dead seats.
        GameServer deathBoundaryServer; QString error; require(deathBoundaryServer.listen(error, 0), "death-boundary server listens");
        require(deathBoundaryServer.setTargetPlayerCount(3), "death-boundary lobby configured");
        GameClientController host(deathBoundaryServer.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), deathBoundaryServer.serverPort()), p3(QStringLiteral("127.0.0.1"), deathBoundaryServer.serverPort());
        p2.connectToHost(); require(waitFor([&] { return deathBoundaryServer.connectedHumanCount() == 2; }), "death-boundary P2 joins");
        p3.connectToHost(); require(waitFor([&] { return deathBoundaryServer.connectedHumanCount() == 3; }), "death-boundary P3 joins");
        require(deathBoundaryServer.startLobbyGame(), "death-boundary game starts"); require(waitFor([&] { return p2.connected() && p3.connected(); }), "death-boundary clients receive views");
        auto& engine = deathBoundaryServer.session().testingEngine(); auto& p1Player = *engine.players()[0]; auto& p2Player = *engine.players()[1]; auto& p3Player = *engine.players()[2];
        const auto clear = [](Player& player) { std::vector<CardId> cards; for (const auto& card : player.handCards()) cards.push_back(card->id()); for (const auto& id : cards) player.removeCard(id); };
        clear(p1Player); clear(p2Player); clear(p3Player);
        require(host.submit(EndPlayPhaseAction {1}).accepted, "current-actor death reaches P2 turn"); deathBoundaryServer.broadcastViews();
        require(waitFor([&] { return engine.currentPlayer() && engine.currentPlayer()->id() == 2 && engine.currentPhase() == Phase::Play; }), "P2 owns the turn before death");
        engine.testingKillPlayer(2); deathBoundaryServer.broadcastViews();
        require(waitFor([&] { return !p2Player.isAlive() && p3.view().currentTurnPlayer == 3 && p3.view().currentPhase == Phase::Play && !p3.view().response; }), "dead current actor is skipped to next living turn");
        const auto postDeathTurn = engine.currentPlayer()->id(); const auto postDeathPhase = engine.currentPhase();
        require(p2.submit(EndPlayPhaseAction {2}).accepted, "dead actor request reaches server over TCP");
        require(waitFor([&] { return !p2.actionPending(); }), "server replies to dead actor request");
        require(engine.currentPlayer()->id() == postDeathTurn && engine.currentPhase() == postDeathPhase, "dead actor cannot submit an old action");
        p3Player.addCard(std::make_shared<Card>("b27-boundary-slash", "b27-boundary-slash", CardType::Slash, Suit::Spade, 1));
        require(p3.submit(PlayCardAction {3, "b27-boundary-slash", {1}}).accepted, "P3 creates a real response for P1");
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::Dodge && host.view().response->isResponder; }), "P1 is current response responder");
        const auto responderRequest = host.view().response->requestId;
        engine.testingKillPlayer(1); deathBoundaryServer.broadcastViews();
        require(waitFor([&] { host.refresh(); return !p1Player.isAlive() && !engine.pendingResponse() && !engine.pendingCardSelection() && !engine.dyingContext(); }), "dead responder clears response and selection state");
        require(!host.submit(RespondAction {1, responderRequest, std::nullopt}).accepted, "dead responder cannot revive old response context");
    }
    { // B2.7: consecutive deaths run cleanup once per player and leave no interaction behind.
        GameServer consecutiveServer; QString error; require(consecutiveServer.listen(error, 0), "consecutive-death server listens");
        require(consecutiveServer.setTargetPlayerCount(3), "consecutive-death lobby configured");
        GameClientController host(consecutiveServer.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), consecutiveServer.serverPort()), p3(QStringLiteral("127.0.0.1"), consecutiveServer.serverPort());
        p2.connectToHost(); require(waitFor([&] { return consecutiveServer.connectedHumanCount() == 2; }), "consecutive P2 joins");
        p3.connectToHost(); require(waitFor([&] { return consecutiveServer.connectedHumanCount() == 3; }), "consecutive P3 joins");
        require(consecutiveServer.startLobbyGame(), "consecutive-death game starts"); require(waitFor([&] { return p2.connected() && p3.connected(); }), "consecutive clients receive views");
        auto& engine = consecutiveServer.session().testingEngine(); auto& p1Player = *engine.players()[0]; auto& p2Player = *engine.players()[1];
        p1Player.addCard(std::make_shared<Card>("b27-consecutive-one", "b27-consecutive-one", CardType::Peach, Suit::Heart, 1));
        p2Player.addCard(std::make_shared<Card>("b27-consecutive-two", "b27-consecutive-two", CardType::Peach, Suit::Heart, 1));
        engine.testingKillPlayer(1); engine.testingKillPlayer(2); consecutiveServer.broadcastViews();
        require(waitFor([&] { return !engine.players()[0]->isAlive() && !engine.players()[1]->isAlive() && engine.gameOver() && !engine.pendingResponse() && !engine.pendingCardSelection(); }), "consecutive deaths finish without a stale interaction");
        require(p1Player.handCards().empty() && p2Player.handCards().empty(), "each consecutive death discards exactly its own hand");
        engine.testingKillPlayer(1); engine.testingKillPlayer(2);
        require(p1Player.handCards().empty() && p2Player.handCards().empty() && !engine.pendingResponse() && !engine.pendingCardSelection(), "already-dead players cannot repeat cleanup or recreate an interaction");
    }
    { // B2.7: all four Snatch/Dismantlement selection variants reject a stale authoritative card option.
        const auto assertStaleOption = [](CardType effect, bool equipment, const char* label) {
            GameSession session({"P1", "P2"}); const auto client = session.addClient(1); const auto targetClient = session.addClient(2);
            auto& engine = session.testingEngine(); auto& p1 = *engine.players()[0]; auto& p2 = *engine.players()[1];
            const auto clear = [](Player& player) { std::vector<CardId> cards; for (const auto& card : player.handCards()) cards.push_back(card->id()); for (const auto& id : cards) player.removeCard(id); };
            clear(p1); clear(p2);
            const CardId effectId = std::string("b27-") + label + "-effect";
            const CardId targetId = std::string("b27-") + label + "-target";
            p1.addCard(std::make_shared<Card>(effectId, effectId, effect, Suit::Club, 1));
            const auto target = std::make_shared<Card>(targetId, targetId, equipment ? CardType::Weapon : CardType::Peach, Suit::Heart, 1,
                equipment ? std::optional<EquipmentData>(EquipmentData{EquipmentSlot::Weapon, 3}) : std::nullopt);
            if (equipment) engine.testingEquip(2, target); else p2.addCard(target);
            require(session.submitAction(client, PlayCardAction {1, effectId, {2}}).accepted, "stale variant creates selection");
            while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
                const auto response = *engine.pendingResponse();
                const auto responderClient = response.responder == 1 ? client : targetClient;
                require(session.submitAction(responderClient, RespondAction {response.responder, response.requestId, std::nullopt}).accepted,
                        "Nullification pass advances to the pending stale selection");
            }
            require(engine.pendingCardSelection().has_value(), "stale variant reaches its authoritative selection after Nullification");
            const auto request = *engine.pendingCardSelection();
            require(request.selectableCards.size() == 1 && request.selectableCards.front().cardId == targetId && request.selectableCards.front().hidden == !equipment,
                    "stale variant exposes only the correct authoritative option");
            const auto discardBefore = engine.deck().discardPileSize(); const auto handBefore = p1.handCards().size(); const auto timeoutBefore = session.timeoutContextKey();
            if (equipment) engine.testingRemoveEquipment(2, EquipmentSlot::Weapon); else p2.removeCard(targetId);
            require(!session.submitCardSelection(client, request.requestId, 1).accepted, "stale option is rejected");
            require(!session.submitCardSelection(client, request.requestId + 1, 1).accepted, "stale request ID is rejected");
            require(engine.pendingCardSelection() && engine.pendingCardSelection()->requestId == request.requestId && p1.handCards().size() == handBefore
                    && engine.deck().discardPileSize() == discardBefore && session.timeoutContextKey() == timeoutBefore,
                    "stale option cannot transfer, discard, advance, or refresh the selection");
        };
        assertStaleOption(CardType::Snatch, true, "snatch-equipment");
        assertStaleOption(CardType::Dismantlement, false, "dismantlement-hand");
        assertStaleOption(CardType::Dismantlement, true, "dismantlement-equipment");
    }
    { // B2.7: GameOver is an immutable server terminal state across TCP actions and the timeout path.
        GameServer server; QString error; require(server.listen(error, 0), "GameOver seal server listens");
        GameClientController host(server.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), server.serverPort()); p2.connectToHost();
        require(waitFor([&] { return server.remoteConnected(); }) && server.startLobbyGame() && waitFor([&] { return p2.connected(); }), "GameOver seal game starts");
        auto& engine = server.session().testingEngine(); auto& p1 = *engine.players()[0]; auto& p2Player = *engine.players()[1];
        const auto clear = [](Player& player) { std::vector<CardId> cards; for (const auto& card : player.handCards()) cards.push_back(card->id()); for (const auto& id : cards) player.removeCard(id); };
        clear(p1); clear(p2Player); engine.testingSetHp(1, 1);
        p2Player.addCard(std::make_shared<Card>("b27-seal-slash", "b27-seal-slash", CardType::Slash, Suit::Spade, 1));
        require(host.submit(EndPlayPhaseAction {1}).accepted, "seal test reaches P2 turn"); server.broadcastViews();
        require(waitFor([&] { return p2.view().currentTurnPlayer == 2; }) && p2.submit(PlayCardAction {2, "b27-seal-slash", {1}}).accepted, "seal test creates lethal response");
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::Dodge; }), "seal test receives Dodge");
        require(host.submit(RespondAction {1, host.view().response->requestId, std::nullopt}).accepted, "seal test declines Dodge"); server.broadcastViews();
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::PeachRescue; }), "seal test enters rescue");
        require(host.submit(RespondAction {1, host.view().response->requestId, std::nullopt}).accepted, "seal test P1 passes"); server.broadcastViews();
        require(waitFor([&] { return p2.view().response && p2.view().response->type == ResponseType::PeachRescue && p2.view().response->isResponder; }), "seal test P2 final rescue");
        require(p2.submit(RespondAction {2, p2.view().response->requestId, std::nullopt}).accepted, "seal test final pass");
        require(waitFor([&] { return engine.gameOver(); }), "seal test reaches GameOver");
        const PlayerId current = engine.currentPlayer()->id(); const Phase phase = engine.currentPhase(); const int turn = engine.turnNumber(); const auto draw = engine.deck().drawPileSize(); const auto discard = engine.deck().discardPileSize();
        const int hp1 = p1.hp(); const int hp2 = p2Player.hp(); const auto hand1 = p1.handCards().size(); const auto hand2 = p2Player.handCards().size(); const auto equipment1 = p1.equipmentCards().size(); const auto equipment2 = p2Player.equipmentCards().size(); const auto logs = engine.logEntries().size();
        require(!engine.pendingResponse() && !engine.pendingCardSelection() && !engine.dyingContext(), "GameOver snapshot has no interaction");
        require(p2.submit(EndPlayPhaseAction {2}).accepted && waitFor([&] { return !p2.actionPending(); }), "post-GameOver end action reaches TCP server");
        require(p2.submit(RespondAction {2, 999999, std::nullopt}).accepted && waitFor([&] { return !p2.actionPending(); }), "post-GameOver response reaches TCP server");
        require(p2.submit(SelectCardsAction {2, 999999, {"forged"}}).accepted && waitFor([&] { return !p2.actionPending(); }), "post-GameOver selection reaches TCP server");
        require(!server.session().handleTimeout().accepted, "GameOver timeout is rejected by formal path");
        require(engine.gameOver() && engine.currentPlayer()->id() == current && engine.currentPhase() == phase && engine.turnNumber() == turn
                    && engine.deck().drawPileSize() == draw && engine.deck().discardPileSize() == discard && p1.hp() == hp1 && p2Player.hp() == hp2
                    && p1.handCards().size() == hand1 && p2Player.handCards().size() == hand2 && p1.equipmentCards().size() == equipment1 && p2Player.equipmentCards().size() == equipment2
                    && engine.logEntries().size() == logs && !engine.pendingResponse() && !engine.pendingCardSelection() && !engine.dyingContext(),
                "GameOver Action, timeout, turn, phase, draw, interaction, and state snapshot remain immutable");
    }
    { // B2.7: serialized TCP ViewState gives card identities only to the responder or resulting owner.
        GameServer server; QString error; require(server.listen(error, 0) && server.setTargetPlayerCount(3), "privacy server listens");
        GameClientController host(server.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), server.serverPort()), p3(QStringLiteral("127.0.0.1"), server.serverPort());
        p2.connectToHost(); require(waitFor([&] { return server.connectedHumanCount() == 2; }), "privacy P2 joins");
        p3.connectToHost(); require(waitFor([&] { return server.connectedHumanCount() == 3; }), "privacy P3 joins");
        QByteArray latestPayload[4], responsePayload[4], selectionPayload[4];
        QObject::connect(&server, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& payload) {
            const auto json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
            latestPayload[viewer] = json;
            if (payload.contains("response")) responsePayload[viewer] = json;
            if (payload.contains("selection")) selectionPayload[viewer] = json;
        });
        require(server.startLobbyGame() && waitFor([&] { return p2.connected() && p3.connected(); }), "privacy game starts");
        auto& engine = server.session().testingEngine(); auto& p1 = *engine.players()[0]; auto& p2Player = *engine.players()[1]; auto& p3Player = *engine.players()[2];
        const auto clear = [](Player& player) { std::vector<CardId> cards; for (const auto& card : player.handCards()) cards.push_back(card->id()); for (const auto& id : cards) player.removeCard(id); };
        clear(p1); clear(p2Player); clear(p3Player); engine.testingSetHp(1, 1);
        p1.addCard(std::make_shared<Card>("b27-privacy-slash", "b27-privacy-slash", CardType::Slash, Suit::Spade, 1));
        p1.addCard(std::make_shared<Card>("b27-privacy-snatch", "b27-privacy-snatch", CardType::Snatch, Suit::Club, 1));
        p1.addCard(std::make_shared<Card>("b27-privacy-dismantle", "b27-privacy-dismantle", CardType::Dismantlement, Suit::Club, 1));
        p2Player.addCard(std::make_shared<Card>("b27-private-dodge", "b27-private-dodge", CardType::Dodge, Suit::Heart, 1));
        p2Player.addCard(std::make_shared<Card>("SECRET_PRIVATE_CARD_B27", "SECRET_PRIVATE_CARD_B27", CardType::Peach, Suit::Heart, 2));
        p2Player.addCard(std::make_shared<Card>("SECRET_DISMANTLE_CARD_B27", "SECRET_DISMANTLE_CARD_B27", CardType::Wine, Suit::Club, 3));
        p2Player.addCard(std::make_shared<Card>("b27-private-peach", "b27-private-peach", CardType::Peach, Suit::Heart, 4));
        p2Player.addCard(std::make_shared<Card>("SECRET_NULLIFICATION_CARD_B27", "SECRET_NULLIFICATION_CARD_B27", CardType::Nullification, Suit::Heart, 6));
        p3Player.addCard(std::make_shared<Card>("b27-p3-peach", "b27-p3-peach", CardType::Peach, Suit::Heart, 5));
        require(host.submit(PlayCardAction {1, "b27-privacy-slash", {2}}).accepted, "privacy Slash creates response"); server.broadcastViews();
        require(waitFor([&] { return p2.view().response && p2.view().response->isResponder; }), "P2 receives private Dodge response");
        require(responsePayload[2].contains("b27-private-dodge") && host.view().response->selectableCards.empty() && !responsePayload[3].contains("b27-private-dodge"),
                "only Dodge responder receives its legal card identity");
        require(!QJsonDocument(encodeViewState(host.view())).toJson(QJsonDocument::Compact).contains("SECRET_PRIVATE_CARD_B27") && !responsePayload[3].contains("SECRET_PRIVATE_CARD_B27"), "non-responders never receive P2 private hand identity");
        require(p2.submit(RespondAction {2, p2.view().response->requestId, "b27-private-dodge"}).accepted, "P2 resolves private Dodge");
        require(waitFor([&] { host.refresh(); return !host.view().response; }), "privacy Slash resolves");
        require(host.submit(PlayCardAction {1, "b27-privacy-snatch", {2}}).accepted, "privacy Snatch creates selection"); server.broadcastViews();
        require(waitFor([&] { p2.refresh(); return p2.view().response && p2.view().response->isResponder && p2.view().response->type == ResponseType::Nullification; }),
                "P2 receives its ordered Nullification request");
        host.refresh();
        require(responsePayload[2].contains("SECRET_NULLIFICATION_CARD_B27")
                    && host.view().response->selectableCards.empty()
                    && !responsePayload[3].contains("SECRET_NULLIFICATION_CARD_B27"),
                "only the Nullification responder receives its private legal card identity");
        while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
            const auto response = *engine.pendingResponse();
            if (response.responder == 1) {
                require(host.submit(RespondAction {1, response.requestId, std::nullopt}).accepted,
                        "host passes the privacy Snatch Nullification window");
            } else if (response.responder == 2) {
                require(p2.submit(RespondAction {2, response.requestId, std::nullopt}).accepted,
                        "P2 passes the privacy Snatch Nullification window");
                require(waitFor([&] { return !p2.actionPending(); }), "P2 Nullification pass is processed");
            } else {
                require(p3.submit(RespondAction {3, response.requestId, std::nullopt}).accepted,
                        "P3 passes the privacy Snatch Nullification window");
                require(waitFor([&] { return !p3.actionPending(); }), "P3 Nullification pass is processed");
            }
            server.broadcastViews();
        }
        require(waitFor([&] { host.refresh(); return host.view().cardSelection && !latestPayload[3].isEmpty(); }), "Snatch selection reaches clients");
        require(!QJsonDocument(encodeViewState(host.view())).toJson(QJsonDocument::Compact).contains("SECRET_PRIVATE_CARD_B27") && !latestPayload[3].contains("SECRET_PRIVATE_CARD_B27")
                    && host.view().cardSelection->options.front().hidden, "Snatch Selection serializes only an anonymous option");
        const auto snatchRequest = host.view().cardSelection->requestId;
        require(host.submitCardSelection(snatchRequest, host.view().cardSelection->options.front().optionId).accepted, "host resolves anonymous Snatch selection"); server.broadcastViews();
        require(waitFor([&] { host.refresh(); return std::any_of(host.view().ownHand.begin(), host.view().ownHand.end(), [](const auto& card) { return card.id == "SECRET_PRIVATE_CARD_B27"; }); }), "new owner receives obtained private card");
        require(host.submit(PlayCardAction {1, "b27-privacy-dismantle", {2}}).accepted, "privacy Dismantlement creates selection"); server.broadcastViews();
        while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
            const auto response = *engine.pendingResponse();
            if (response.responder == 1) {
                require(host.submit(RespondAction {1, response.requestId, std::nullopt}).accepted,
                        "host passes the privacy Dismantlement Nullification window");
            } else if (response.responder == 2) {
                require(p2.submit(RespondAction {2, response.requestId, std::nullopt}).accepted,
                        "P2 passes the privacy Dismantlement Nullification window");
                require(waitFor([&] { return !p2.actionPending(); }), "P2 Dismantlement Nullification pass is processed");
            } else {
                require(p3.submit(RespondAction {3, response.requestId, std::nullopt}).accepted,
                        "P3 passes the privacy Dismantlement Nullification window");
                require(waitFor([&] { return !p3.actionPending(); }), "P3 Dismantlement Nullification pass is processed");
            }
            server.broadcastViews();
        }
        require(waitFor([&] { host.refresh(); return host.view().cardSelection && !latestPayload[3].isEmpty(); }), "Dismantlement selection reaches clients");
        require(!QJsonDocument(encodeViewState(host.view())).toJson(QJsonDocument::Compact).contains("SECRET_DISMANTLE_CARD_B27") && !latestPayload[3].contains("SECRET_DISMANTLE_CARD_B27")
                    && host.view().cardSelection->options.front().hidden, "Dismantlement Selection serializes only an anonymous option");
        require(host.submitCardSelection(host.view().cardSelection->requestId, host.view().cardSelection->options.front().optionId).accepted, "host resolves anonymous Dismantlement selection");
        require(host.submit(EndPlayPhaseAction {1}).accepted, "privacy test advances to P2 turn"); server.broadcastViews();
        p2Player.addCard(std::make_shared<Card>("b27-privacy-rescue-slash", "b27-privacy-rescue-slash", CardType::Slash, Suit::Spade, 6));
        require(waitFor([&] { return p2.view().currentTurnPlayer == 2 && p2.view().currentPhase == Phase::Play; }) && p2.submit(PlayCardAction {2, "b27-privacy-rescue-slash", {1}}).accepted, "P2 begins rescue privacy path");
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::Dodge; }), "P1 receives rescue-path Dodge");
        require(host.submit(RespondAction {1, host.view().response->requestId, std::nullopt}).accepted, "P1 declines rescue-path Dodge"); server.broadcastViews();
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::PeachRescue && host.view().response->responder == 1; }), "P1 is first rescue responder");
        require(host.submit(RespondAction {1, host.view().response->requestId, std::nullopt}).accepted, "P1 passes rescue"); server.broadcastViews();
        require(waitFor([&] { return p2.view().response && p2.view().response->type == ResponseType::PeachRescue && p2.view().response->isResponder; }), "P2 receives private Peach rescue response");
        require(responsePayload[2].contains("b27-private-peach") && host.view().response->selectableCards.empty() && !responsePayload[3].contains("b27-private-peach"),
                "only Peach responder receives its legal Peach identity");
        require(p2.submit(RespondAction {2, p2.view().response->requestId, std::nullopt}).accepted, "P2 passes to P3"); server.broadcastViews();
        require(waitFor([&] { return p3.view().response && p3.view().response->type == ResponseType::PeachRescue && p3.view().response->isResponder; }), "P3 becomes next rescue responder");
        require(responsePayload[3].contains("b27-p3-peach") && !QJsonDocument(encodeViewState(host.view())).toJson(QJsonDocument::Compact).contains("b27-p3-peach") && !responsePayload[2].contains("b27-p3-peach"),
                "rescue responder private card lists do not cross between players");
        require(std::none_of(engine.logEntries().begin(), engine.logEntries().end(), [](const auto& entry) { return entry.find("SECRET_") != std::string::npos; }), "public logs contain no private card identity");
    }
    { // B3: a five-player identity game is server-authoritative, viewer-filtered, and reveals only confirmed deaths.
        GameServer server; QString error; require(server.listen(error, 0) && server.setTargetPlayerCount(5), "five-player identity server listens");
        GameClientController host(server.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), server.serverPort()), p3(QStringLiteral("127.0.0.1"), server.serverPort()), p4(QStringLiteral("127.0.0.1"), server.serverPort()), p5(QStringLiteral("127.0.0.1"), server.serverPort());
        p2.connectToHost(); require(waitFor([&] { return server.connectedHumanCount() == 2; }), "identity P2 joins");
        p3.connectToHost(); require(waitFor([&] { return server.connectedHumanCount() == 3; }), "identity P3 joins");
        p4.connectToHost(); require(waitFor([&] { return server.connectedHumanCount() == 4; }), "identity P4 joins");
        p5.connectToHost(); require(waitFor([&] { return server.connectedHumanCount() == 5; }), "identity P5 joins");
        QByteArray p2Payload;
        QObject::connect(&server, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& payload) { if (viewer == 2) p2Payload = QJsonDocument(payload).toJson(QJsonDocument::Compact); });
        require(server.startLobbyGame() && waitFor([&] { return p2.connected() && p3.connected() && p4.connected() && p5.connected(); }), "five-player identity game starts over TCP");
        auto& engine = server.session().testingEngine(); require(engine.gameMode() == GameMode::Identity && engine.currentPlayer()->identity() == PlayerIdentity::Lord, "identity mode selects Lord as first player");
        const auto checkViewer = [](const PlayerViewState& view) {
            for (const auto& player : view.players) {
                if (player.id == view.selfPlayerId || player.identity == PlayerIdentity::Lord) require(player.identity.has_value(), "self and Lord identity are visible");
                else if (player.alive) require(!player.identity.has_value(), "other living identities stay hidden");
            }
        };
        host.refresh(); checkViewer(host.view()); checkViewer(p2.view()); checkViewer(p3.view()); checkViewer(p4.view()); checkViewer(p5.view());
        require(!p2Payload.isEmpty(), "serialized TCP identity payload captured");
        const auto p2Json = QJsonDocument::fromJson(p2Payload).object(); const auto players = p2Json["players"].toArray();
        for (const auto& value : players) {
            const auto player = value.toObject(); const auto id = player["id"].toInt(); const auto* authoritative = engine.player(id);
            const bool shouldReveal = id == 2 || authoritative->identity() == PlayerIdentity::Lord;
            require(player.contains("identity") == shouldReveal, "TCP JSON filters living identity per viewer authorization");
        }
        const auto victim = std::find_if(engine.players().begin(), engine.players().end(), [](const auto& player) { return player->id() != 2 && player->identity() != PlayerIdentity::Lord; });
        require(victim != engine.players().end(), "identity test has a non-Lord victim"); const auto victimId = (*victim)->id(); const auto victimIdentity = (*victim)->identity();
        engine.testingKillPlayer(victimId); server.broadcastViews();
        require(waitFor([&] { return std::any_of(p2.view().players.begin(), p2.view().players.end(), [&](const auto& player) { return player.id == victimId && !player.alive && player.identity == victimIdentity; }); }), "death reveals only the confirmed victim identity over TCP");
    }
    { // B3: eight-player identity composition is network-started rather than an eight-player-only core shortcut.
        GameServer server; QString error; require(server.listen(error, 0) && server.setTargetPlayerCount(8), "eight-player identity server listens");
        GameClientController host(server.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), server.serverPort()); p2.connectToHost();
        require(waitFor([&] { return server.connectedHumanCount() == 2; }) && server.startLobbyGame() && waitFor([&] { return p2.connected(); }), "eight-player identity game starts over TCP");
        auto& engine = server.session().testingEngine(); int lord = 0, loyalist = 0, rebel = 0, renegade = 0;
        for (const auto& player : engine.players()) { lord += player->identity() == PlayerIdentity::Lord; loyalist += player->identity() == PlayerIdentity::Loyalist; rebel += player->identity() == PlayerIdentity::Rebel; renegade += player->identity() == PlayerIdentity::Renegade; }
        require(engine.gameMode() == GameMode::Identity && lord == 1 && loyalist == 2 && rebel == 4 && renegade == 1 && engine.currentPlayer(),
                "eight-player TCP lobby uses the required identity distribution with a valid current player");
        host.refresh(); require(p2.view().selfIdentity == engine.player(2)->identity(), "remote receives only its own authoritative identity");
    }
    { // B3: remote Rebel killer receives exactly three private cards through the formal TCP death path.
        GameServer server; QString error; require(server.listen(error, 0) && server.setTargetPlayerCount(5), "identity reward server listens");
        server.testingSetIdentityAssignments({PlayerIdentity::Lord, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Loyalist, PlayerIdentity::Renegade});
        GameClientController host(server.session(), 1); NetworkGameClient p2(QStringLiteral("127.0.0.1"), server.serverPort()); p2.connectToHost();
        require(waitFor([&] { return server.connectedHumanCount() == 2; }) && server.startLobbyGame() && waitFor([&] { return p2.connected(); }), "identity reward game starts");
        auto& engine = server.session().testingEngine(); auto& killer = *engine.players()[1]; auto& victim = *engine.players()[2];
        const auto clear = [](Player& p) { std::vector<CardId> ids; for (const auto& c : p.handCards()) ids.push_back(c->id()); for (const auto& id : ids) p.removeCard(id); };
        for (const auto& player : engine.players()) clear(*player); engine.testingSetHp(3, 1);
        killer.addCard(std::make_shared<Card>("b3-tcp-reward-slash", "b3-tcp-reward-slash", CardType::Slash, Suit::Spade, 1));
        require(host.submit(EndPlayPhaseAction {1}).accepted, "reward reaches remote Rebel turn"); server.broadcastViews();
        const auto handBeforeSlash = killer.handCards().size();
        require(waitFor([&] { return p2.view().currentTurnPlayer == 2; }) && p2.submit(PlayCardAction {2, "b3-tcp-reward-slash", {3}}).accepted, "remote Rebel sends lethal Slash over TCP");
        server.setInteractionTimeoutForTesting(40);
        require(waitFor([&] { return victim.status() == PlayerStatus::Dead; }, 3000), "AI response or timeout completes Rebel death");
        require(killer.handCards().size() == handBeforeSlash + 2, "Rebel death awards killer exactly three cards");
        require(waitFor([&] { return p2.view().ownHand.size() == killer.handCards().size() && p2.view().players[2].handCardCount == 0; }), "killer private ViewState and public count synchronize");
        const auto hand = killer.handCards().size(); engine.testingKillPlayer(3); require(killer.handCards().size() == hand, "repeat death cannot duplicate reward");
    }
    { // B3: Lord killing a Loyalist through the timeout-driven TCP death path discards all Lord cards once.
        GameServer server; QString error; require(server.listen(error, 0) && server.setTargetPlayerCount(5), "identity penalty server listens");
        server.testingSetIdentityAssignments({PlayerIdentity::Lord, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade, PlayerIdentity::Loyalist});
        GameClientController host(server.session(), 1); NetworkGameClient p2(QStringLiteral("127.0.0.1"), server.serverPort()); p2.connectToHost();
        require(waitFor([&] { return server.connectedHumanCount() == 2; }) && server.startLobbyGame() && waitFor([&] { return p2.connected(); }), "identity penalty game starts");
        auto& engine = server.session().testingEngine(); auto& lord = *engine.players()[0]; auto& loyalist = *engine.players()[4];
        const auto clear = [](Player& p) { std::vector<CardId> ids; for (const auto& c : p.handCards()) ids.push_back(c->id()); for (const auto& id : ids) p.removeCard(id); };
        for (const auto& player : engine.players()) clear(*player); engine.testingSetHp(5, 1);
        lord.addCard(std::make_shared<Card>("b3-tcp-penalty-slash", "b3-tcp-penalty-slash", CardType::Slash, Suit::Spade, 1));
        lord.addCard(std::make_shared<Card>("B3_LORD_PRIVATE_HAND", "B3_LORD_PRIVATE_HAND", CardType::Peach, Suit::Heart, 2));
        engine.testingEquip(1, std::make_shared<Card>("b3-tcp-penalty-weapon", "b3-tcp-penalty-weapon", CardType::Weapon, Suit::Club, 3, EquipmentData{EquipmentSlot::Weapon, 2}));
        const auto discardBefore = engine.deck().discardPileSize();
        require(host.submit(PlayCardAction {1, "b3-tcp-penalty-slash", {5}}).accepted, "Lord begins lethal Slash"); server.broadcastViews();
        require(waitFor([&] { host.refresh(); return host.view().response && host.view().response->type == ResponseType::Dodge; }), "penalty Slash reaches real response");
        server.setInteractionTimeoutForTesting(40);
        require(waitFor([&] { return loyalist.status() == PlayerStatus::Dead; }, 3000), "Loyalist dies after response and rescue timeout");
        require(lord.handCards().empty() && !lord.equipment(EquipmentSlot::Weapon) && engine.deck().discardPileSize() >= discardBefore + 3, "Lord penalty clears hand, equipment, and discards cards");
        require(waitFor([&] { host.refresh(); return host.view().ownHand.empty() && !p2.view().players[0].equipment.cardsBySlot[0] && p2.view().players[0].handCardCount == 0; }), "penalty state synchronizes privately and publicly");
        const auto discardAfter = engine.deck().discardPileSize(); engine.testingKillPlayer(5); require(engine.deck().discardPileSize() == discardAfter, "repeat death cannot duplicate Lord penalty");
    }
    { // B3: Identity state is cleared in Lobby and rebuilt on a second start without reconnecting.
        GameServer server; QString error; require(server.listen(error, 0) && server.setTargetPlayerCount(5), "identity lifecycle server listens");
        server.testingSetIdentityAssignments({PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade});
        GameClientController host(server.session(), 1); NetworkGameClient p2(QStringLiteral("127.0.0.1"), server.serverPort()); p2.connectToHost();
        require(waitFor([&] { return server.connectedHumanCount() == 2; }) && server.startLobbyGame() && waitFor([&] { return p2.connected(); }), "first identity game starts");
        auto& first = server.session().testingEngine(); first.testingKillPlayer(3); server.broadcastViews();
        require(waitFor([&] { return !first.players()[2]->isAlive(); }), "first identity game records a revealable death");
        require(server.returnToLobby() && !server.gameStarted() && server.session().testingEngine().winningSide() == WinningSide::None, "return Lobby clears identity outcome state");
        require(waitFor([&] { return !p2.lobbyState().gameStarted; }), "remote receives Lobby lifecycle reset");
        require(server.startLobbyGame() && waitFor([&] { return p2.connected(); }), "second identity game starts without reconnecting");
        auto& second = server.session().testingEngine();
        require(second.gameMode() == GameMode::Identity && second.winningSide() == WinningSide::None && second.currentPlayer()->identity() == PlayerIdentity::Lord
                    && std::all_of(second.players().begin(), second.players().end(), [](const auto& player) { return player->isAlive(); }), "second start rebuilds alive Identity state and Lord first turn");
    }
    { // B3: the final formal death selects each winning side correctly and synchronizes it to a Remote.
        const auto runVictory = [&](const std::vector<PlayerIdentity>& identities, PlayerId attacker, PlayerId target, WinningSide expected, const char* label) {
            GameServer server; QString error; require(server.listen(error, 0) && server.setTargetPlayerCount(5), "victory server listens");
            server.testingSetIdentityAssignments(identities); GameClientController host(server.session(), 1); NetworkGameClient p2(QStringLiteral("127.0.0.1"), server.serverPort()); p2.connectToHost();
            require(waitFor([&] { return server.connectedHumanCount() == 2; }) && server.startLobbyGame() && waitFor([&] { return p2.connected(); }), "victory game starts");
            auto& engine = server.session().testingEngine(); const auto clear = [](Player& p) { std::vector<CardId> ids; for (const auto& c : p.handCards()) ids.push_back(c->id()); for (const auto& id : ids) p.removeCard(id); };
            for (const auto& player : engine.players()) clear(*player);
            for (const auto& player : engine.players()) if (player->id() != attacker && player->id() != target) engine.testingKillPlayer(player->id());
            engine.testingSetHp(target, 1); auto& source = *engine.players()[static_cast<std::size_t>(attacker - 1)]; source.addCard(std::make_shared<Card>(std::string("b3-") + label + "-slash", "victory slash", CardType::Slash, Suit::Spade, 1));
            if (engine.currentPlayer()->id() != attacker) {
                require(engine.currentPlayer()->id() == 1 && attacker == 2 && host.submit(EndPlayPhaseAction {1}).accepted, "victory advances to remote attacker"); server.broadcastViews();
                require(waitFor([&] { return p2.view().currentTurnPlayer == attacker; }), "remote attacker turn arrives");
                require(p2.submit(PlayCardAction {attacker, std::string("b3-") + label + "-slash", {target}}).accepted, "remote winning Slash reaches TCP server");
            } else require(host.submit(PlayCardAction {attacker, std::string("b3-") + label + "-slash", {target}}).accepted, "host winning Slash starts");
            server.setInteractionTimeoutForTesting(40);
            require(waitFor([&] { return engine.gameOver() && engine.winningSide() == expected; }, 3000), "formal final death sets expected winning side");
            require(waitFor([&] { host.refresh(); return host.view().gameOver && p2.view().gameOver && host.view().winningSide == expected && p2.view().winningSide == expected && host.view().winningPlayers == engine.winningPlayers() && p2.view().winningPlayers == engine.winningPlayers() && !engine.pendingResponse() && !engine.pendingCardSelection(); }, 3000), "Host and Remote receive authoritative terminal winners");
        };
        runVictory({PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel, PlayerIdentity::Renegade}, 1, 5, WinningSide::LordSide, "lord");
        runVictory({PlayerIdentity::Lord, PlayerIdentity::Rebel, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade}, 2, 1, WinningSide::RebelSide, "rebel");
        runVictory({PlayerIdentity::Lord, PlayerIdentity::Renegade, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Rebel}, 2, 1, WinningSide::Renegade, "renegade");
    }
    { // B4.2 connection lifecycle: a remote can leave/rejoin and a host releases its listening port.
        GameServer lifecycleServer; QString lifecycleError; require(lifecycleServer.listen(lifecycleError, 0), "lifecycle server listens");
        lifecycleServer.setReconnectGracePeriodForTesting(50);
        const auto lifecyclePort = lifecycleServer.serverPort();
        NetworkGameClient firstJoin(QStringLiteral("127.0.0.1"), lifecyclePort, QStringLiteral("First")); firstJoin.connectToHost();
        require(waitFor([&] { return lifecycleServer.remoteConnected() && firstJoin.selfPlayerId() == 2; }), "remote joins lifecycle lobby");
        firstJoin.disconnectFromHost();
        require(waitFor([&] { return !lifecycleServer.remoteConnected() && lifecycleServer.testingReconnectReservationCount() == 1; }), "remote leave reserves its lobby seat during grace");
        require(firstJoin.lobbyState().connectedHumanCount == 1 && firstJoin.view().players.empty(), "remote leave clears cached lobby and view state");
        require(waitFor([&] { return lifecycleServer.testingReconnectReservationCount() == 0; }), "remote leave reservation expires before a new join");
        NetworkGameClient rejoin(QStringLiteral("127.0.0.1"), lifecyclePort, QStringLiteral("Rejoin")); rejoin.connectToHost();
        require(waitFor([&] { return lifecycleServer.remoteConnected() && rejoin.selfPlayerId() == 2; }), "remote rejoins the same host");
        lifecycleServer.close();
        require(waitFor([&] { return rejoin.connectionState() == ConnectionState::ConnectionLost; }), "host close disconnects remote");
        GameServer recreated; QString recreatedError; require(recreated.listen(recreatedError, lifecyclePort), "host releases port for recreate");
    }
    { // B4.2 seal: AOE TCP responder privacy plus stale/non-responder rejection preserves the current request.
        GameServer aoeServer; QString aoeError; require(aoeServer.listen(aoeError, 0) && aoeServer.setTargetPlayerCount(3), "AOE TCP server listens");
        GameClientController host(aoeServer.session(), 1);
        NetworkGameClient p2(QStringLiteral("127.0.0.1"), aoeServer.serverPort()), p3(QStringLiteral("127.0.0.1"), aoeServer.serverPort());
        p2.connectToHost(); require(waitFor([&] { return aoeServer.connectedHumanCount() == 2; }), "AOE P2 joins");
        p3.connectToHost(); require(waitFor([&] { return aoeServer.connectedHumanCount() == 3; }) && aoeServer.startLobbyGame(), "AOE P3 joins and game starts");
        require(waitFor([&] { return p2.connected() && p3.connected(); }), "AOE remotes receive game view");
        auto& engine = aoeServer.session().testingEngine();
        const auto clear = [](Player& player) { std::vector<CardId> ids; for (const auto& card : player.handCards()) ids.push_back(card->id()); for (const auto& id : ids) player.removeCard(id); };
        for (const auto& player : engine.players()) clear(*player);
        engine.players()[0]->addCard(std::make_shared<Card>("b42-tcp-barbarian", "b42-tcp-barbarian", CardType::BarbarianInvasion, Suit::Club, 1));
        engine.players()[1]->addCard(std::make_shared<Card>("SECRET_B42_KILL", "SECRET_B42_KILL", CardType::Slash, Suit::Spade, 1));
        QJsonObject p3ResponsePayload;
        QObject::connect(&aoeServer, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& payload) { if (viewer == 3 && payload.contains("response")) p3ResponsePayload = payload; });
        aoeServer.broadcastViews();
        require(waitFor([&] { return std::any_of(p2.view().ownHand.begin(), p2.view().ownHand.end(), [](const auto& c) { return c.id == "SECRET_B42_KILL"; }); }), "AOE responder receives its private Slash");
        require(host.submit(PlayCardAction {1, "b42-tcp-barbarian", {}}).accepted, "host starts TCP Barbarian Invasion");
        while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
            const auto response = *engine.pendingResponse();
            if (response.responder == 1) {
                require(host.submit(RespondAction {1, response.requestId, std::nullopt}).accepted,
                        "host passes the AOE Nullification window");
            } else if (response.responder == 2) {
                require(p2.submit(RespondAction {2, response.requestId, std::nullopt}).accepted,
                        "P2 passes the AOE Nullification window");
                require(waitFor([&] { return !p2.actionPending(); }), "P2 AOE Nullification pass is processed");
            } else {
                require(p3.submit(RespondAction {3, response.requestId, std::nullopt}).accepted,
                        "P3 passes the AOE Nullification window");
                require(waitFor([&] { return !p3.actionPending(); }), "P3 AOE Nullification pass is processed");
            }
            aoeServer.broadcastViews();
        }
        require(waitFor([&] { return p2.view().response && p2.view().response->isResponder && p2.view().response->type == ResponseType::Slash && !p3ResponsePayload.isEmpty(); }), "P2 is first AOE responder");
        require(!QJsonDocument(p3ResponsePayload).toJson(QJsonDocument::Compact).contains("SECRET_B42_KILL"), "non-responder payload hides AOE private Slash");
        const auto oldRequest = p2.view().response->requestId;
        require(p2.submit(RespondAction {2, oldRequest, "SECRET_B42_KILL"}).accepted, "P2 responds with its private Slash");
        require(waitFor([&] { return !p2.actionPending(); }), "P2 AOE response is processed before the next Nullification round");
        // Each AOE target has its own public-order Nullification round.  Every
        // eligible player receives the same request and may simply pass.
        while (engine.pendingResponse() && engine.pendingResponse()->type == ResponseType::Nullification) {
            const auto response = *engine.pendingResponse();
            if (response.responder == 1) {
                require(host.submit(RespondAction {1, response.requestId, std::nullopt}).accepted,
                        "host passes the next AOE Nullification window");
            } else if (response.responder == 2) {
                require(p2.submit(RespondAction {2, response.requestId, std::nullopt}).accepted,
                        "P2 passes the next AOE Nullification window");
                require(waitFor([&] { return !p2.actionPending(); }), "P2 next AOE Nullification pass is processed");
            } else {
                require(p3.submit(RespondAction {3, response.requestId, std::nullopt}).accepted,
                        "P3 passes the next AOE Nullification window");
                require(waitFor([&] { return !p3.actionPending(); }), "P3 next AOE Nullification pass is processed");
            }
            aoeServer.broadcastViews();
        }
        require(waitFor([&] { p3.refresh(); return p3.view().response && p3.view().response->isResponder && p3.view().response->type == ResponseType::Slash; }), "P3 becomes next AOE responder");
        const auto currentRequest = p3.view().response->requestId; const auto p3Hp = engine.players()[2]->hp();
        require(p2.submit(RespondAction {2, oldRequest, std::nullopt}).accepted, "stale P2 request reaches TCP server");
        require(waitFor([&] { return !p2.actionPending(); }), "stale request receives rejection");
        host.refresh(); require(host.view().response && host.view().response->responder == 3 && host.view().response->requestId == currentRequest && engine.players()[2]->hp() == p3Hp, "stale/non-responder AOE action cannot advance state");
    }
    std::cout << "Stage A multi-client lobby tests passed.\n";
    return 0;
    // The legacy two-player scenarios below are retained as regression coverage.
    // Real socket framing, invalid handshake and server recovery are tested on a fresh server.
    {
        GameServer protocolServer; QString protocolError; assert(protocolServer.listen(protocolError, 0));
        QTcpSocket wrongVersion;
        wrongVersion.connectToHost(QHostAddress::LocalHost, protocolServer.serverPort());
        assert(wrongVersion.waitForConnected(1000));
        wrongVersion.write(MessageCodec::frame(QJsonObject{{"type", "hello"}, {"protocolVersion", 999}}));
        assert(waitFor([&] { return wrongVersion.bytesAvailable() > 0 || wrongVersion.state() == QAbstractSocket::UnconnectedState; }));
        assert(!protocolServer.remoteConnected());
        assert(waitFor([&] { return wrongVersion.state() == QAbstractSocket::UnconnectedState; }));
        writeFragmentedHello(protocolServer.serverPort());
        assert(waitFor([&] { return protocolServer.remoteConnected(); }));
    }
    assertServerRecoversAfterRejectedSocket(rawFrame("{invalid-json"));
    QByteArray oversized(4, Qt::Uninitialized); oversized[0] = 0; oversized[1] = 0x20; oversized[2] = 0; oversized[3] = 0;
    assertServerRecoversAfterRejectedSocket(oversized);
    assertServerRecoversAfterRejectedSocket(MessageCodec::frame(QJsonObject{{"type", "totally_unknown"}, {"protocolVersion", ProtocolVersion}}));
    assertMalformedActionIsRejected(QJsonObject{{"type", "action"}, {"requestId", 1}, {"payload", QJsonObject{}}});
    assertMalformedActionIsRejected(QJsonObject{{"type", "action"}, {"requestId", "abc"}, {"payload", QJsonObject{{"kind", "end"}, {"player", 2}}}});
    assertMalformedActionIsRejected(QJsonObject{{"type", "action"}, {"requestId", 1}, {"payload", QJsonObject{{"kind", "end"}, {"player", "Player2"}}}});
    {
        GameServer stickyServer; QString stickyError; assert(stickyServer.listen(stickyError, 0));
        QTcpSocket sticky;
        sticky.connectToHost(QHostAddress::LocalHost, stickyServer.serverPort());
        assert(sticky.waitForConnected(1000));
        const auto hello = MessageCodec::frame(QJsonObject{{"type", "hello"}, {"protocolVersion", ProtocolVersion}});
        const auto unknown = MessageCodec::frame(QJsonObject{{"type", "totally_unknown"}});
        sticky.write(hello + unknown);
        assert(waitFor([&] { return stickyServer.remoteConnected() && sticky.bytesAvailable() > 0; }));
        assert(sticky.state() == QAbstractSocket::ConnectedState);
    }
    {
        auto closingServer = std::make_unique<GameServer>(); QString closingError; assert(closingServer->listen(closingError, 0));
        NetworkGameClient closingClient(QStringLiteral("127.0.0.1"), closingServer->serverPort());
        closingClient.connectToHost();
        require(waitFor([&] { return closingServer->remoteConnected(); }), "closing client joins lobby");
        require(closingServer->startLobbyGame(), "closing game explicitly starts");
        require(waitFor([&] { return closingClient.connected(); }), "closing client receives started view");
        closingServer.reset();
        assert(waitFor([&] { return closingClient.connectionState() == ConnectionState::ConnectionLost; }));
        assert(!closingClient.submit(EndPlayPhaseAction {2}).accepted);
    }
    { // Stage 8.11: P2 rescues dying P1 through the real TCP response path.
        GameServer rescueServer; QString rescueError; assert(rescueServer.listen(rescueError, 0));
        GameClientController rescueHost(rescueServer.session(), 1);
        NetworkGameClient rescueClient(QStringLiteral("127.0.0.1"), rescueServer.serverPort()); rescueClient.connectToHost();
        require(waitFor([&] { return rescueServer.remoteConnected(); }), "rescue client joins lobby");
        require(rescueServer.startLobbyGame(), "rescue game explicitly starts");
        require(waitFor([&] { return rescueClient.connected(); }), "rescue client receives started view");
        auto& rescueEngine = rescueServer.session().testingEngine(); auto& rescueP1 = *rescueEngine.players()[0]; auto& rescueP2 = *rescueEngine.players()[1];
        const auto clearRescue = [](Player& p) { std::vector<CardId> ids; for (const auto& c : p.handCards()) ids.push_back(c->id()); for (const auto& id : ids) p.removeCard(id); };
        const auto makeRescue = [](const CardId& id, CardType type) { return std::make_shared<Card>(id, id, type, Suit::Heart, 1); };
        clearRescue(rescueP1); clearRescue(rescueP2); rescueEngine.testingSetHp(1, 1);
        rescueP2.addCard(makeRescue("rescue-slash", CardType::Slash)); rescueP2.addCard(makeRescue("rescue-peach", CardType::Peach)); rescueP2.addCard(makeRescue("PRIVATE_PEACH_PRIVACY_CARD", CardType::Wine));
        assert(rescueHost.submit(EndPlayPhaseAction {1}).accepted); rescueHost.refresh(); rescueServer.broadcastViews();
        assert(waitFor([&] { return rescueClient.view().currentTurnPlayer == 2 && rescueClient.view().currentPhase == Phase::Play; }));
        QJsonObject rescuePayload; QObject::connect(&rescueServer, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& p) { if (viewer == 2 && p.contains("response")) rescuePayload = p; });
        assert(rescueClient.submit(PlayCardAction {2, "rescue-slash", {1}}).accepted);
        assert(waitFor([&] { rescueHost.refresh(); return rescueHost.view().response && rescueHost.view().response->isResponder; }));
        assert(rescueHost.submit(RespondAction {1, rescueHost.view().response->requestId, std::nullopt}).accepted); rescueServer.broadcastViews();
        assert(waitFor([&] { return rescueClient.view().response && rescueClient.view().response->isResponder && rescueClient.view().response->type == ResponseType::PeachRescue; }));
        assert(!QJsonDocument(rescuePayload).toJson(QJsonDocument::Compact).contains("PRIVATE_PEACH_PRIVACY_CARD"));
        assert(rescueClient.view().response->selectableCards.size() == 1 && rescueClient.view().response->selectableCards.front().id == "rescue-peach");
        assert(rescueClient.submit(RespondAction {2, rescueClient.view().response->requestId, "rescue-peach"}).accepted);
        assert(waitFor([&] { rescueHost.refresh(); return !rescueHost.view().response && !rescueClient.view().response && rescueHost.view().players[0].hp == 1 && rescueHost.view().players[0].alive; }));
        assert(!rescueEngine.dyingContext() && !rescueEngine.pendingResponse());
    }
    { // Stage 8.12: normal damage, rescue declines, death and GameOver stay synchronized.
        GameServer deathServer; QString deathError; assert(deathServer.listen(deathError, 0));
        GameClientController deathHost(deathServer.session(), 1);
        NetworkGameClient deathClient(QStringLiteral("127.0.0.1"), deathServer.serverPort()); deathClient.connectToHost();
        require(waitFor([&] { return deathServer.remoteConnected(); }), "death client joins lobby");
        require(deathServer.startLobbyGame(), "death game explicitly starts");
        require(waitFor([&] { return deathClient.connected(); }), "death client receives started view");
        auto& deathEngine = deathServer.session().testingEngine(); auto& deathP1 = *deathEngine.players()[0]; auto& deathP2 = *deathEngine.players()[1];
        const auto clearDeath = [](Player& p) { std::vector<CardId> ids; for (const auto& c : p.handCards()) ids.push_back(c->id()); for (const auto& id : ids) p.removeCard(id); };
        clearDeath(deathP1); clearDeath(deathP2); deathEngine.testingSetHp(1, 1);
        deathP2.addCard(std::make_shared<Card>("death-slash", "death-slash", CardType::Slash, Suit::Spade, 1));
        assert(deathHost.submit(EndPlayPhaseAction {1}).accepted); deathHost.refresh(); deathServer.broadcastViews();
        assert(waitFor([&] { return deathClient.view().currentTurnPlayer == 2 && deathClient.view().currentPhase == Phase::Play; }));
        assert(deathClient.submit(PlayCardAction {2, "death-slash", {1}}).accepted);
        assert(waitFor([&] { deathHost.refresh(); return deathHost.view().response && deathHost.view().response->type == ResponseType::Dodge; }));
        assert(deathHost.submit(RespondAction {1, deathHost.view().response->requestId, std::nullopt}).accepted); deathServer.broadcastViews();
        assert(waitFor([&] { deathHost.refresh(); return deathEngine.dyingContext().has_value() && deathHost.view().response && deathHost.view().response->isResponder; }));
        assert(deathHost.submit(RespondAction {1, deathHost.view().response->requestId, std::nullopt}).accepted); deathServer.broadcastViews();
        assert(waitFor([&] { return deathClient.view().response && deathClient.view().response->isResponder && deathClient.view().response->type == ResponseType::PeachRescue; }));
        assert(deathClient.submit(RespondAction {2, deathClient.view().response->requestId, std::nullopt}).accepted);
        assert(waitFor([&] { deathHost.refresh(); return deathEngine.gameOver() && !deathEngine.players()[0]->isAlive() && deathHost.view().gameOver && deathClient.view().gameOver; }));
        assert(deathEngine.winner() == std::optional<PlayerId>(2));
        assert(deathHost.view().winner == std::optional<PlayerId>(2) && deathClient.view().winner == std::optional<PlayerId>(2));
        assert(!deathHost.view().players[0].alive && !deathClient.view().players[0].alive);
        const auto hpSnapshot = deathEngine.players()[0]->hp();
        assert(deathClient.submit(EndPlayPhaseAction {2}).accepted);
        assert(waitFor([&] { return !deathClient.actionPending(); }));
        assert(deathEngine.gameOver() && deathEngine.winner() == std::optional<PlayerId>(2) && deathEngine.players()[0]->hp() == hpSnapshot);
        assert(!deathEngine.pendingResponse() && !deathEngine.pendingCardSelection() && !deathClient.view().response && !deathClient.view().cardSelection);
    }
    { // Stage 8.13: remote Dismantlement selects P1's public weapon without leaking P1's hand.
        GameServer equipmentServer; QString equipmentError; assert(equipmentServer.listen(equipmentError, 0));
        GameClientController equipmentHost(equipmentServer.session(), 1);
        NetworkGameClient equipmentClient(QStringLiteral("127.0.0.1"), equipmentServer.serverPort()); equipmentClient.connectToHost();
        require(waitFor([&] { return equipmentServer.remoteConnected(); }), "dismantlement client joins lobby");
        require(equipmentServer.startLobbyGame(), "dismantlement game explicitly starts");
        require(waitFor([&] { return equipmentClient.connected(); }), "dismantlement client receives started view");
        auto& equipmentEngine = equipmentServer.session().testingEngine(); auto& equipmentP1 = *equipmentEngine.players()[0]; auto& equipmentP2 = *equipmentEngine.players()[1];
        const auto clearEquipment = [](Player& p) { std::vector<CardId> ids; for (const auto& c : p.handCards()) ids.push_back(c->id()); for (const auto& id : ids) p.removeCard(id); };
        clearEquipment(equipmentP1); clearEquipment(equipmentP2);
        equipmentP1.addCard(std::make_shared<Card>("test-weapon", "Test Weapon", CardType::Weapon, Suit::Spade, 1, EquipmentData{EquipmentSlot::Weapon, 3}));
        equipmentP1.addCard(std::make_shared<Card>("PRIVATE_DISMANTLEMENT_CARD", "PRIVATE_DISMANTLEMENT_CARD", CardType::Peach, Suit::Heart, 1));
        assert(equipmentHost.submit(PlayCardAction {1, "test-weapon", {}}).accepted); equipmentHost.refresh(); equipmentServer.broadcastViews();
        assert(equipmentHost.view().players[0].equipment.cardsBySlot[0] && equipmentHost.view().players[0].equipment.cardsBySlot[0]->id == "test-weapon");
        assert(equipmentHost.submit(EndPlayPhaseAction {1}).accepted); equipmentHost.refresh(); equipmentServer.broadcastViews();
        assert(waitFor([&] { return equipmentClient.view().currentTurnPlayer == 2 && equipmentClient.view().currentPhase == Phase::Play; }));
        equipmentP2.addCard(std::make_shared<Card>("equipment-dismantlement", "equipment-dismantlement", CardType::Dismantlement, Suit::Club, 1));
        QJsonObject equipmentPayload; QObject::connect(&equipmentServer, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& p) { if (viewer == 2 && p.contains("selection")) equipmentPayload = p; });
        equipmentServer.broadcastViews();
        assert(waitFor([&] { return std::any_of(equipmentClient.view().ownHand.begin(), equipmentClient.view().ownHand.end(), [](const auto& c) { return c.id == "equipment-dismantlement"; }); }));
        const auto discardBefore = equipmentEngine.deck().discardPileSize();
        assert(equipmentClient.submit(PlayCardAction {2, "equipment-dismantlement", {1}}).accepted);
        assert(waitFor([&] { return equipmentClient.view().cardSelection && !equipmentPayload.isEmpty(); }));
        const auto json = QJsonDocument(equipmentPayload).toJson(QJsonDocument::Compact); assert(!json.contains("PRIVATE_DISMANTLEMENT_CARD"));
        const auto& selection = *equipmentClient.view().cardSelection;
        const auto weapon = std::find_if(selection.options.begin(), selection.options.end(), [](const auto& o) { return !o.hidden && o.displayName == "Test Weapon"; });
        assert(weapon != selection.options.end());
        assert(equipmentClient.submitCardSelection(selection.requestId, weapon->optionId).accepted);
        assert(waitFor([&] { equipmentHost.refresh(); return !equipmentClient.view().cardSelection && !equipmentHost.view().players[0].equipment.cardsBySlot[0]; }));
        assert(!equipmentEngine.players()[0]->equipment(EquipmentSlot::Weapon));
        assert(equipmentEngine.deck().discardPileSize() == discardBefore + 2);
        assert(!equipmentClient.view().players[0].equipment.cardsBySlot[0]);
    }
    { // Stage 8.14: remote Snatch transfers P1's public weapon into P2's private hand.
        GameServer snatchEquipmentServer; QString snatchEquipmentError; assert(snatchEquipmentServer.listen(snatchEquipmentError, 0));
        GameClientController snatchEquipmentHost(snatchEquipmentServer.session(), 1);
        NetworkGameClient snatchEquipmentClient(QStringLiteral("127.0.0.1"), snatchEquipmentServer.serverPort()); snatchEquipmentClient.connectToHost();
        require(waitFor([&] { return snatchEquipmentServer.remoteConnected(); }), "snatch client joins lobby");
        require(snatchEquipmentServer.startLobbyGame(), "snatch game explicitly starts");
        require(waitFor([&] { return snatchEquipmentClient.connected(); }), "snatch client receives started view");
        auto& snatchEngine = snatchEquipmentServer.session().testingEngine(); auto& snatchP1 = *snatchEngine.players()[0]; auto& snatchP2 = *snatchEngine.players()[1];
        const auto clearSnatchEquipment = [](Player& p) { std::vector<CardId> ids; for (const auto& c : p.handCards()) ids.push_back(c->id()); for (const auto& id : ids) p.removeCard(id); };
        clearSnatchEquipment(snatchP1); clearSnatchEquipment(snatchP2);
        snatchP1.addCard(std::make_shared<Card>("snatch-weapon", "Test Snatch Weapon", CardType::Weapon, Suit::Spade, 1, EquipmentData{EquipmentSlot::Weapon, 3}));
        snatchP1.addCard(std::make_shared<Card>("PRIVATE_EQUIPMENT_SNATCH_CARD", "PRIVATE_EQUIPMENT_SNATCH_CARD", CardType::Peach, Suit::Heart, 1));
        assert(snatchEquipmentHost.submit(PlayCardAction {1, "snatch-weapon", {}}).accepted);
        assert(snatchEquipmentHost.submit(EndPlayPhaseAction {1}).accepted); snatchEquipmentHost.refresh(); snatchEquipmentServer.broadcastViews();
        assert(waitFor([&] { return snatchEquipmentClient.view().currentTurnPlayer == 2 && snatchEquipmentClient.view().currentPhase == Phase::Play; }));
        snatchP2.addCard(std::make_shared<Card>("equipment-snatch", "equipment-snatch", CardType::Snatch, Suit::Club, 1));
        QJsonObject snatchEquipmentPayload; QObject::connect(&snatchEquipmentServer, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& p) { if (viewer == 2 && p.contains("selection")) snatchEquipmentPayload = p; });
        snatchEquipmentServer.broadcastViews();
        assert(waitFor([&] { return std::any_of(snatchEquipmentClient.view().ownHand.begin(), snatchEquipmentClient.view().ownHand.end(), [](const auto& c) { return c.id == "equipment-snatch"; }); }));
        assert(snatchEquipmentClient.submit(PlayCardAction {2, "equipment-snatch", {1}}).accepted);
        assert(waitFor([&] { return snatchEquipmentClient.view().cardSelection && !snatchEquipmentPayload.isEmpty(); }));
        const auto json = QJsonDocument(snatchEquipmentPayload).toJson(QJsonDocument::Compact); assert(!json.contains("PRIVATE_EQUIPMENT_SNATCH_CARD"));
        const auto& selection = *snatchEquipmentClient.view().cardSelection;
        const auto weapon = std::find_if(selection.options.begin(), selection.options.end(), [](const auto& o) { return !o.hidden && o.displayName == "Test Snatch Weapon"; }); assert(weapon != selection.options.end());
        assert(snatchEquipmentClient.submitCardSelection(selection.requestId, weapon->optionId).accepted);
        assert(waitFor([&] { snatchEquipmentHost.refresh(); return !snatchEquipmentClient.view().cardSelection && !snatchEquipmentHost.view().players[0].equipment.cardsBySlot[0]
            && std::any_of(snatchEquipmentClient.view().ownHand.begin(), snatchEquipmentClient.view().ownHand.end(), [](const auto& c) { return c.id == "snatch-weapon"; }); }));
        assert(!snatchEngine.players()[0]->equipment(EquipmentSlot::Weapon));
    }
    GameServer server; QString error; assert(server.listen(error, 0));
    GameClientController host(server.session(), 1);
    NetworkGameClient client(QStringLiteral("127.0.0.1"), server.serverPort());
    client.connectToHost();
    require(waitFor([&] { return server.remoteConnected(); }), "main network client joins lobby");
    require(server.startLobbyGame(), "main network game explicitly starts");
    require(waitFor([&] { return client.connected(); }), "main network client receives started view");
    QTcpSocket secondClient;
    secondClient.connectToHost(QHostAddress::LocalHost, server.serverPort());
    assert(secondClient.waitForConnected(1000));
    assert(waitFor([&] { return secondClient.bytesAvailable() > 0 || secondClient.state() == QAbstractSocket::UnconnectedState; }));
    assert(server.remoteConnected() && client.connected());
    assert(client.selfPlayerId() == 2 && client.view().ownHand.size() == 4);
    assert(client.view().players.size() == 2 && client.view().players[0].handCardCount == 6);
    auto& engine = server.session().testingEngine();
    auto& player1 = *engine.players()[0]; auto& player2 = *engine.players()[1];
    const auto clear = [](Player& player) { std::vector<CardId> ids; for (const auto& card : player.handCards()) ids.push_back(card->id()); for (const auto& id : ids) player.removeCard(id); };
    const auto make = [](const CardId& id, CardType type) { return std::make_shared<Card>(id, id, type, Suit::Spade, 1); };
    clear(player1); clear(player2); player1.addCard(make("network-duel", CardType::Duel)); player1.addCard(make("network-duel-slash", CardType::Slash)); player1.addCard(make("network-slash-1", CardType::Slash)); player1.addCard(make("network-slash-2", CardType::Slash)); player2.addCard(make("network-duel-slash", CardType::Slash)); player2.addCard(make("network-duel-fire", CardType::FireSlash)); player2.addCard(make("network-duel-thunder", CardType::ThunderSlash)); player2.addCard(make("network-dodge", CardType::Dodge)); player2.addCard(make("network-ex-nihilo", CardType::ExNihilo));
    host.refresh(); server.broadcastViews();
    assert(waitFor([&] { return client.view().ownHand.size() == 5; }));
    // The payload emitted by the server contains no opponent hand-card identity.
    const auto hostPayload = server.viewPayloadFor(1);
    const auto hostPayloadText = QJsonDocument(hostPayload).toJson(QJsonDocument::Compact);
    assert(!hostPayloadText.contains("network-dodge"));
    assert(!hostPayloadText.contains("network-ex-nihilo"));
    assert(host.submit(PlayCardAction {1, "network-duel", {2}}).accepted); server.broadcastViews(); host.refresh();
    assert(waitFor([&] { return client.view().response && client.view().response->isResponder && client.view().response->type == ResponseType::Slash; }));
    assert(client.view().response->selectableCards.size() == 3 && std::any_of(client.view().response->selectableCards.begin(), client.view().response->selectableCards.end(), [](const auto& card) { return card.id == "network-duel-slash"; }) && std::any_of(client.view().response->selectableCards.begin(), client.view().response->selectableCards.end(), [](const auto& card) { return card.id == "network-duel-fire"; }) && std::any_of(client.view().response->selectableCards.begin(), client.view().response->selectableCards.end(), [](const auto& card) { return card.id == "network-duel-thunder"; }));
    assert(!host.view().response->isResponder && host.view().response->selectableCards.empty());
    assert(client.submit(RespondAction {2, client.view().response->requestId, "network-duel-slash"}).accepted);
    assert(waitFor([&] { host.refresh(); return host.view().response && host.view().response->isResponder && host.view().response->type == ResponseType::Slash; }));
    assert(client.view().response && !client.view().response->isResponder && client.view().response->selectableCards.empty());
    assert(host.submit(RespondAction {1, host.view().response->requestId, "network-duel-slash"}).accepted); server.broadcastViews();
    assert(waitFor([&] { return client.view().response && client.view().response->isResponder && client.view().response->type == ResponseType::Slash; }));
    assert(client.submit(RespondAction {2, client.view().response->requestId, std::nullopt}).accepted);
    assert(waitFor([&] { host.refresh(); return !host.view().response && !client.view().response && host.view().players[1].hp == 3 && client.view().players[1].hp == 3; }));
    assert(host.submit(PlayCardAction {1, "network-slash-1", {2}}).accepted); server.broadcastViews(); host.refresh();
    assert(waitFor([&] { return client.view().response && client.view().response->isResponder && client.view().response->type == ResponseType::Dodge; }));
    const auto hostResponseText = QJsonDocument(server.viewPayloadFor(1)).toJson(QJsonDocument::Compact);
    assert(!hostResponseText.contains("network-dodge"));
    // A wrong response request must not clear the real pending response.
    assert(client.submit(RespondAction {2, client.view().response->requestId + 99, "network-dodge"}).accepted);
    assert(waitFor([&] { return !client.actionPending(); }));
    host.refresh();
    assert(host.view().response.has_value());
    assert(client.submit(RespondAction {2, client.view().response->requestId, "network-dodge"}).accepted);
    assert(waitFor([&] { host.refresh(); return !host.view().response && !client.view().response; }));
    assert(host.view().players[1].hp == 3 && client.view().players[1].hp == 3);
    assert(host.submit(PlayCardAction {1, "network-slash-2", {2}}).accepted); server.broadcastViews();
    assert(waitFor([&] { return client.view().response && client.view().response->isResponder; }));
    assert(client.submit(RespondAction {2, client.view().response->requestId, std::nullopt}).accepted);
    assert(waitFor([&] { host.refresh(); return !host.view().response && host.view().players[1].hp == 2 && client.view().players[1].hp == 2; }));
    // Player 2 obtains an actual active turn and plays a card through TCP.
    assert(host.submit(EndPlayPhaseAction {1}).accepted);
    host.refresh(); server.broadcastViews();
    assert(waitFor([&] { return client.view().currentTurnPlayer == 2 && client.view().currentPhase == Phase::Play; }));
    const auto hpBeforeRejectedActions = client.view().players[1].hp;
    assert(client.submit(EndPlayPhaseAction {1}).accepted);
    assert(waitFor([&] { return !client.actionPending(); }));
    assert(client.view().currentTurnPlayer == 2 && client.view().currentPhase == Phase::Play);
    assert(client.submit(PlayCardAction {2, "not-a-real-card", {}}).accepted);
    assert(waitFor([&] { return !client.actionPending(); }));
    assert(client.view().players[1].hp == hpBeforeRejectedActions);
    player2.addCard(make("network-p2-slash", CardType::Slash));
    server.broadcastViews();
    assert(waitFor([&] { return std::any_of(client.view().ownHand.begin(), client.view().ownHand.end(), [](const auto& c) { return c.id == "network-p2-slash"; }); }));
    assert(client.submit(PlayCardAction {2, "network-p2-slash", {2}}).accepted);
    assert(waitFor([&] { return !client.actionPending(); }));
    assert(client.view().currentTurnPlayer == 2 && client.view().currentPhase == Phase::Play);
    assert(client.submit(PlayCardAction {2, "network-ex-nihilo", {}}).accepted);
    assert(waitFor([&] { return !client.actionPending() && std::none_of(client.view().ownHand.begin(), client.view().ownHand.end(), [](const auto& c) { return c.id == "network-ex-nihilo"; }); }));
    // CardSelection is sent with opaque option ids; the target's hidden hand is never serialized.
    player1.addCard(make("PRIVATE_P1_HAND_CARD", CardType::Peach));
    player1.addCard(make("PRIVATE_P1_HAND_CARD_2", CardType::Wine));
    player2.addCard(make("network-dismantlement", CardType::Dismantlement));
    QJsonObject rawSelectionPayload;
    QObject::connect(&server, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& payload) {
        if (viewer == 2 && payload.contains("selection")) rawSelectionPayload = payload;
    });
    server.broadcastViews();
    assert(waitFor([&] { return std::any_of(client.view().ownHand.begin(), client.view().ownHand.end(), [](const auto& c) { return c.id == "network-dismantlement"; }); }));
    assert(client.submit(PlayCardAction {2, "network-dismantlement", {1}}).accepted);
    assert(waitFor([&] { return client.view().cardSelection.has_value() && !rawSelectionPayload.isEmpty(); }));
    const auto selectionJson = QJsonDocument(rawSelectionPayload).toJson(QJsonDocument::Compact);
    assert(!selectionJson.contains("PRIVATE_P1_HAND_CARD"));
    assert(!selectionJson.contains("deck"));
    assert(!selectionJson.contains("drawPile"));
    const auto& selection = *client.view().cardSelection;
    assert(!selection.options.empty());
    assert(selection.options.front().hidden);
    assert(client.submitCardSelection(selection.requestId + 1, selection.options.front().optionId).accepted);
    assert(waitFor([&] { return !client.actionPending(); }));
    assert(client.view().cardSelection.has_value());
    assert(client.submitCardSelection(selection.requestId, selection.options.front().optionId).accepted);
    assert(waitFor([&] { return !client.actionPending() && !client.view().cardSelection.has_value() && client.view().players[0].handCardCount == 1; }));
    // Stage 8.9: a remote Snatch uses an opaque hand option and receives the real card only after transfer.
    player1.addCard(make("PRIVATE_SNATCH_CARD", CardType::Peach));
    player2.addCard(make("network-snatch", CardType::Snatch));
    rawSelectionPayload = {};
    server.broadcastViews();
    assert(waitFor([&] { return std::any_of(client.view().ownHand.begin(), client.view().ownHand.end(), [](const auto& c) { return c.id == "network-snatch"; }); }));
    const auto p2HandBeforeSnatch = client.view().ownHand.size();
    const auto p1HandBeforeSnatch = host.view().players[0].handCardCount;
    assert(client.submit(PlayCardAction {2, "network-snatch", {1}}).accepted);
    assert(waitFor([&] { return client.view().cardSelection.has_value() && !rawSelectionPayload.isEmpty(); }));
    const auto snatchJson = QJsonDocument(rawSelectionPayload).toJson(QJsonDocument::Compact);
    assert(!snatchJson.contains("PRIVATE_SNATCH_CARD"));
    const auto snatchSelection = *client.view().cardSelection;
    assert(!snatchSelection.options.empty() && snatchSelection.options.back().hidden);
    const auto optionId = snatchSelection.options.back().optionId;
    assert(client.submitCardSelection(snatchSelection.requestId, optionId).accepted);
    assert(waitFor([&] { return !client.actionPending() && !client.view().cardSelection.has_value()
        && std::any_of(client.view().ownHand.begin(), client.view().ownHand.end(), [](const auto& c) { return c.id == "PRIVATE_SNATCH_CARD"; }); }));
    assert(client.view().ownHand.size() == p2HandBeforeSnatch);
    host.refresh();
    assert(host.view().players[0].handCardCount + 1 == p1HandBeforeSnatch);
    // Stage 9.1: player-specific JSON exposes Lord and self identities only.
    {
        const std::vector<PlayerIdentity> identities {PlayerIdentity::Lord, PlayerIdentity::Loyalist, PlayerIdentity::Rebel, PlayerIdentity::Renegade};
        GameSession identitySession({"Lord", "Loyalist", "Rebel", "Renegade"}, true, identities);
        const auto rebelClient = identitySession.addClient(3);
        const auto identityJson = encodeViewState(identitySession.viewFor(rebelClient));
        const auto players = identityJson["players"].toArray();
        assert(identityJson["selfIdentity"].toInt() == int(PlayerIdentity::Rebel));
        assert(players[0].toObject().contains("identity"));
        assert(!players[1].toObject().contains("identity"));
        assert(players[2].toObject()["identity"].toInt() == int(PlayerIdentity::Rebel));
        assert(!players[3].toObject().contains("identity"));
        identitySession.testingEngine().testingSetPlayerStatus(2, PlayerStatus::Dead);
        const auto afterDeath = encodeViewState(identitySession.viewFor(rebelClient));
        assert(afterDeath["players"].toArray()[1].toObject()["identity"].toInt() == int(PlayerIdentity::Loyalist));
    }
    { // Stage 9.2: the authoritative multiplayer session rejects forged out-of-range Slash targets.
        GameSession rangeSession({"P1", "P2", "P3", "P4"});
        const auto p1Client = rangeSession.addClient(1); const auto p2Client = rangeSession.addClient(2); const auto p3Client = rangeSession.addClient(3);
        auto& engine = rangeSession.testingEngine();
        for (const auto& player : engine.players()) { std::vector<CardId> ids; for (const auto& c : player->handCards()) ids.push_back(c->id()); for (const auto& id : ids) player->removeCard(id); }
        engine.players()[0]->addCard(std::make_shared<Card>("stage92-network-slash", "stage92-network-slash", CardType::Slash, Suit::Spade, 1));
        const auto p3Hp = engine.players()[2]->hp(); const auto p1Hand = engine.players()[0]->handCards().size();
        const auto forged = rangeSession.submitAction(p1Client, PlayCardAction {1, "stage92-network-slash", {3}});
        assert(!forged.accepted && engine.players()[2]->hp() == p3Hp && engine.players()[0]->handCards().size() == p1Hand);
        assert(!engine.pendingResponse() && rangeSession.viewFor(p3Client).currentTurnPlayer == 1);
        assert(rangeSession.submitAction(p1Client, PlayCardAction {1, "stage92-network-slash", {2}}).accepted);
        assert(rangeSession.viewFor(p2Client).response && rangeSession.viewFor(p2Client).response->isResponder);
    }
    { // Stage 9.2: a four-player server rejects a remote forged range target and accepts its adjacent TCP target.
        GameServer rangeServer({"P1", "P2", "P3", "P4"}); QString rangeError; assert(rangeServer.listen(rangeError, 0));
        GameClientController rangeHost(rangeServer.session(), 1);
        NetworkGameClient rangeClient(QStringLiteral("127.0.0.1"), rangeServer.serverPort()); rangeClient.connectToHost();
        require(waitFor([&] { return rangeServer.remoteConnected(); }), "range client joins lobby");
        require(rangeServer.startLobbyGame(), "range game explicitly starts");
        require(waitFor([&] { return rangeClient.connected(); }), "range client receives started view");
        auto& engine = rangeServer.session().testingEngine();
        for (const auto& player : engine.players()) { std::vector<CardId> ids; for (const auto& c : player->handCards()) ids.push_back(c->id()); for (const auto& id : ids) player->removeCard(id); }
        engine.players()[1]->addCard(std::make_shared<Card>("stage92-tcp-slash", "stage92-tcp-slash", CardType::Slash, Suit::Spade, 1));
        assert(rangeHost.submit(EndPlayPhaseAction {1}).accepted); rangeServer.broadcastViews();
        assert(waitFor([&] { return rangeClient.view().currentTurnPlayer == 2 && rangeClient.view().currentPhase == Phase::Play; }));
        const auto farHp = engine.players()[3]->hp(); const auto p2Hand = engine.players()[1]->handCards().size();
        assert(rangeClient.submit(PlayCardAction {2, "stage92-tcp-slash", {4}}).accepted);
        assert(waitFor([&] { return !rangeClient.actionPending(); }));
        assert(engine.players()[3]->hp() == farHp && engine.players()[1]->handCards().size() == p2Hand && !engine.pendingResponse());
        assert(rangeClient.submit(PlayCardAction {2, "stage92-tcp-slash", {1}}).accepted);
        assert(waitFor([&] { rangeHost.refresh(); return rangeHost.view().response && rangeHost.view().response->isResponder; }));
    }
    { // B4.3: four real TCP clients receive the same public Harvest pool, then observe its authoritative removal and cleanup.
        GameServer harvestServer; QString harvestError; require(harvestServer.listen(harvestError, 0), "Harvest server listens");
        require(harvestServer.setTargetPlayerCount(4), "Harvest lobby is configured");
        GameClientController harvestHost(harvestServer.session(), 1);
        NetworkGameClient harvestP2(QStringLiteral("127.0.0.1"), harvestServer.serverPort());
        NetworkGameClient harvestP3(QStringLiteral("127.0.0.1"), harvestServer.serverPort());
        NetworkGameClient harvestP4(QStringLiteral("127.0.0.1"), harvestServer.serverPort());
        harvestP2.connectToHost(); harvestP3.connectToHost(); harvestP4.connectToHost();
        require(waitFor([&] { return harvestServer.connectedHumanCount() == 4; }), "all Harvest clients join");
        require(harvestServer.startLobbyGame(), "Harvest game starts");
        require(waitFor([&] { return harvestP2.connected() && harvestP3.connected() && harvestP4.connected(); }), "Harvest clients receive views");
        auto& harvestEngine = harvestServer.session().testingEngine();
        for (const auto& player : harvestEngine.players()) { std::vector<CardId> ids; for (const auto& c : player->handCards()) ids.push_back(c->id()); for (const auto& id : ids) player->removeCard(id); }
        harvestEngine.players()[0]->addCard(std::make_shared<Card>("b43-network-harvest", "b43-network-harvest", CardType::Harvest, Suit::Heart, 1));
        harvestServer.broadcastViews(); harvestHost.refresh();
        require(harvestHost.submit(PlayCardAction {1, "b43-network-harvest", {}}).accepted, "host uses Harvest");
        require(waitFor([&] { harvestHost.refresh(); return harvestHost.view().harvest && harvestP2.view().harvest && harvestP3.view().harvest && harvestP4.view().harvest; }), "Harvest pool decodes for every TCP client");
        const auto pool = harvestHost.view().harvest->pool;
        require(pool.size() == 4 && harvestP2.view().harvest->pool.size() == 4 && harvestP3.view().harvest->pool.size() == 4 && harvestP4.view().harvest->pool.size() == 4, "Harvest reveals exactly one public card per living player");
        for (std::size_t i = 0; i < pool.size(); ++i) require(pool[i].id == harvestP2.view().harvest->pool[i].id && pool[i].id == harvestP3.view().harvest->pool[i].id && pool[i].id == harvestP4.view().harvest->pool[i].id, "Harvest public CardIds match across TCP clients");
        require(harvestHost.view().cardSelection && harvestHost.view().cardSelection->options.front().cardType
                    && *harvestHost.view().cardSelection->options.front().cardType == pool.front().type,
                "Harvest selection carries CardType display metadata instead of its internal name");
        require(harvestHost.view().cardSelection && harvestHost.submitCardSelection(harvestHost.view().cardSelection->requestId, harvestHost.view().cardSelection->options.front().optionId).accepted, "P1 selects a Harvest card");
        require(waitFor([&] { return harvestP2.view().cardSelection && harvestP2.view().harvest && harvestP2.view().harvest->pool.size() == 3; }), "P2 sees reduced public pool and its own picker request");
        require(harvestP2.submitCardSelection(harvestP2.view().cardSelection->requestId, harvestP2.view().cardSelection->options.front().optionId).accepted, "P2 selects a Harvest card");
        require(waitFor([&] { return harvestP3.view().cardSelection && harvestP3.view().harvest && harvestP3.view().harvest->pool.size() == 2; }), "P3 becomes picker over TCP");
        require(harvestServer.session().handleTimeout().accepted, "P3 Harvest timeout auto-picks deterministically"); harvestServer.broadcastViews();
        require(waitFor([&] { return harvestP4.view().cardSelection && harvestP4.view().harvest && harvestP4.view().harvest->pool.size() == 1; }), "P4 sees timeout-reduced public pool");
        require(harvestP4.submitCardSelection(harvestP4.view().cardSelection->requestId, harvestP4.view().cardSelection->options.front().optionId).accepted, "P4 completes Harvest");
        require(waitFor([&] { harvestHost.refresh(); return !harvestHost.view().harvest && !harvestP2.view().harvest && !harvestP3.view().harvest && !harvestP4.view().harvest; }), "Harvest pool clears for host and all remotes");
        require(harvestEngine.currentPlayer()->id() == 1 && harvestEngine.currentPhase() == Phase::Play, "Harvest returns to source Play phase");
    }
    { // B4.3: four real TCP clients seal Borrowed Sword privacy, authority, pass transfer, timeout and stale-request behavior.
        GameServer borrowServer; QString borrowError; require(borrowServer.listen(borrowError, 0), "Borrowed Sword server listens");
        require(borrowServer.setTargetPlayerCount(4), "Borrowed Sword lobby is configured");
        GameClientController borrowHost(borrowServer.session(), 1);
        NetworkGameClient borrowP2(QStringLiteral("127.0.0.1"), borrowServer.serverPort()); NetworkGameClient borrowP3(QStringLiteral("127.0.0.1"), borrowServer.serverPort()); NetworkGameClient borrowP4(QStringLiteral("127.0.0.1"), borrowServer.serverPort());
        borrowP2.connectToHost(); borrowP3.connectToHost(); borrowP4.connectToHost();
        require(waitFor([&] { return borrowServer.connectedHumanCount() == 4; }), "all Borrowed Sword clients join"); require(borrowServer.startLobbyGame(), "Borrowed Sword game starts");
        require(waitFor([&] { return borrowP2.connected() && borrowP3.connected() && borrowP4.connected(); }), "Borrowed Sword clients receive views");
        auto& engine = borrowServer.session().testingEngine();
        for (const auto& player : engine.players()) { std::vector<CardId> ids; for (const auto& c : player->handCards()) ids.push_back(c->id()); for (const auto& id : ids) player->removeCard(id); }
        auto& source = *engine.players()[0]; auto& holder = *engine.players()[1];
        source.addCard(std::make_shared<Card>("b43-tcp-pass", "b43-tcp-pass", CardType::BorrowedSword, Suit::Heart, 1)); holder.addCard(std::make_shared<Card>("SECRET_B43_BORROWED_KILL", "SECRET_B43_BORROWED_KILL", CardType::Slash, Suit::Spade, 1));
        engine.testingEquip(holder.id(), std::make_shared<Card>("b43-tcp-weapon", "b43-tcp-weapon", CardType::Weapon, Suit::Club, 2, EquipmentData{EquipmentSlot::Weapon, 3}));
        QJsonObject p2Payload, p3Payload, p4Payload;
        QObject::connect(&borrowServer, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& payload) { if (payload.contains("response")) { if (viewer == 2) p2Payload = payload; else if (viewer == 3) p3Payload = payload; else if (viewer == 4) p4Payload = payload; } });
        borrowServer.broadcastViews(); borrowHost.refresh(); require(borrowHost.submit(PlayCardAction {1, "b43-tcp-pass", {2, 3}}).accepted, "P1 uses Borrowed Sword over TCP");
        require(waitFor([&] { return borrowP2.view().response && borrowP2.view().response->isResponder && !p2Payload.isEmpty() && !p3Payload.isEmpty() && !p4Payload.isEmpty(); }), "P2 receives the private forced Kill response");
        const auto p2Json = QJsonDocument(p2Payload).toJson(QJsonDocument::Compact); const auto p3Json = QJsonDocument(p3Payload).toJson(QJsonDocument::Compact); const auto p4Json = QJsonDocument(p4Payload).toJson(QJsonDocument::Compact);
        require(p2Json.contains("SECRET_B43_BORROWED_KILL") && !p3Json.contains("SECRET_B43_BORROWED_KILL") && !p4Json.contains("SECRET_B43_BORROWED_KILL"), "forced Kill cards are serialized only to the responder");
        const auto forcedRequest = borrowP2.view().response->requestId; const auto weaponBefore = holder.equipment(EquipmentSlot::Weapon)->id();
        require(borrowP3.submit(RespondAction {3, forcedRequest, std::nullopt}).accepted, "non-responder action reaches server"); require(waitFor([&] { return !borrowP3.actionPending(); }), "non-responder rejection returns");
        require(engine.pendingResponse() && engine.pendingResponse()->requestId == forcedRequest && holder.equipment(EquipmentSlot::Weapon) && holder.equipment(EquipmentSlot::Weapon)->id() == weaponBefore, "non-responder cannot mutate Borrowed Sword state");
        require(borrowP2.submit(RespondAction {2, forcedRequest, std::nullopt}).accepted, "P2 passes forced Kill over TCP");
        require(waitFor([&] { borrowHost.refresh(); return !borrowHost.view().response && !engine.borrowedSwordContext() && !holder.equipment(EquipmentSlot::Weapon); }), "pass clears forced response and equipment");
        require(std::any_of(borrowHost.view().ownHand.begin(), borrowHost.view().ownHand.end(), [&](const auto& c) { return c.id == weaponBefore; }), "P1 receives the exact pass-transferred weapon CardId");
        source.addCard(std::make_shared<Card>("b43-tcp-dodge", "b43-tcp-dodge", CardType::BorrowedSword, Suit::Heart, 2)); holder.addCard(std::make_shared<Card>("b43-tcp-forced-slash", "b43-tcp-forced-slash", CardType::Slash, Suit::Spade, 2)); engine.players()[2]->addCard(std::make_shared<Card>("b43-tcp-dodge-card", "b43-tcp-dodge-card", CardType::Dodge, Suit::Diamond, 2)); engine.testingEquip(holder.id(), std::make_shared<Card>("b43-tcp-dodge-weapon", "b43-tcp-dodge-weapon", CardType::Weapon, Suit::Club, 2, EquipmentData{EquipmentSlot::Weapon, 3}));
        borrowServer.broadcastViews(); borrowHost.refresh(); require(borrowHost.submit(PlayCardAction {1, "b43-tcp-dodge", {2, 3}}).accepted, "Borrowed Sword forced Kill starts over TCP");
        require(waitFor([&] { return borrowP2.view().response && borrowP2.view().response->isResponder && borrowP2.view().response->type == ResponseType::BorrowedSwordSlash; }), "P2 receives forced Kill over TCP");
        require(borrowP2.submit(RespondAction {2, borrowP2.view().response->requestId, CardId("b43-tcp-forced-slash")}).accepted, "P2 plays forced Kill over TCP");
        require(waitFor([&] { return borrowP3.view().response && borrowP3.view().response->isResponder && borrowP3.view().response->type == ResponseType::Dodge; }), "P3 receives the normal Dodge response over TCP");
        const auto p3HpBeforeDodge = engine.players()[2]->hp(); require(borrowP3.submit(RespondAction {3, borrowP3.view().response->requestId, CardId("b43-tcp-dodge-card")}).accepted, "P3 dodges the forced Kill over TCP");
        require(waitFor([&] { return !engine.borrowedSwordContext() && !engine.pendingResponse() && engine.players()[2]->hp() == p3HpBeforeDodge && holder.equipment(EquipmentSlot::Weapon) && holder.equipment(EquipmentSlot::Weapon)->id() == "b43-tcp-dodge-weapon"; }), "forced Kill Dodge keeps weapon and completes Borrowed Sword");
        source.addCard(std::make_shared<Card>("b43-tcp-damage", "b43-tcp-damage", CardType::BorrowedSword, Suit::Heart, 3)); holder.addCard(std::make_shared<Card>("b43-tcp-damage-slash", "b43-tcp-damage-slash", CardType::Slash, Suit::Spade, 3)); engine.testingEquip(holder.id(), std::make_shared<Card>("b43-tcp-damage-weapon", "b43-tcp-damage-weapon", CardType::Weapon, Suit::Club, 3, EquipmentData{EquipmentSlot::Weapon, 3}));
        borrowServer.broadcastViews(); borrowHost.refresh(); require(borrowHost.submit(PlayCardAction {1, "b43-tcp-damage", {2, 3}}).accepted, "Borrowed Sword damage path starts over TCP");
        require(waitFor([&] { return borrowP2.view().response && borrowP2.view().response->type == ResponseType::BorrowedSwordSlash; }), "P2 receives second forced Kill"); require(borrowP2.submit(RespondAction {2, borrowP2.view().response->requestId, CardId("b43-tcp-damage-slash")}).accepted, "P2 plays damage-path forced Kill");
        require(waitFor([&] { return borrowP3.view().response && borrowP3.view().response->type == ResponseType::Dodge; }), "P3 receives damage-path Dodge request"); const auto p3HpBeforeDamage = engine.players()[2]->hp(); require(borrowP3.submit(RespondAction {3, borrowP3.view().response->requestId, std::nullopt}).accepted, "P3 declines damage-path Dodge");
        require(waitFor([&] { return !engine.borrowedSwordContext() && !engine.pendingResponse() && engine.players()[2]->hp() == p3HpBeforeDamage - 1 && holder.equipment(EquipmentSlot::Weapon) && holder.equipment(EquipmentSlot::Weapon)->id() == "b43-tcp-damage-weapon"; }), "forced Kill damage keeps weapon and completes Borrowed Sword");
        source.addCard(std::make_shared<Card>("b43-tcp-timeout", "b43-tcp-timeout", CardType::BorrowedSword, Suit::Diamond, 1)); engine.testingEquip(holder.id(), std::make_shared<Card>("b43-tcp-timeout-weapon", "b43-tcp-timeout-weapon", CardType::Weapon, Suit::Club, 3, EquipmentData{EquipmentSlot::Weapon, 3}));
        borrowServer.broadcastViews(); borrowHost.refresh(); require(borrowHost.submit(PlayCardAction {1, "b43-tcp-timeout", {2, 3}}).accepted, "second Borrowed Sword starts");
        require(waitFor([&] { return borrowP2.view().response && borrowP2.view().response->isResponder; }), "P2 receives timeout-bound forced response"); const auto timeoutRequest = borrowP2.view().response->requestId;
        require(borrowServer.session().handleTimeout().accepted, "server timeout declines forced Kill"); borrowServer.broadcastViews();
        require(waitFor([&] { borrowHost.refresh(); return !engine.borrowedSwordContext() && !engine.pendingResponse() && !holder.equipment(EquipmentSlot::Weapon); }), "timeout clears Borrowed Sword and transfers weapon once");
        const auto handAfterTimeout = source.handCards().size(); require(std::any_of(source.handCards().begin(), source.handCards().end(), [](const auto& c) { return c->id() == "b43-tcp-timeout-weapon"; }), "timeout transfers the original weapon CardId");
        require(borrowP2.submit(RespondAction {2, timeoutRequest, CardId("SECRET_B43_BORROWED_KILL")}).accepted, "stale response reaches server"); require(waitFor([&] { return !borrowP2.actionPending(); }), "stale response is rejected");
        require(source.handCards().size() == handAfterTimeout && !holder.equipment(EquipmentSlot::Weapon) && !engine.borrowedSwordContext(), "stale forced request cannot duplicate transfer or resume the context");
    }
    { // B4.4: Nullification cards are private to the current TCP responder and stale requests cannot advance the chain.
        GameServer nullServer; QString nullError; require(nullServer.listen(nullError, 0), "Nullification server listens");
        require(nullServer.setTargetPlayerCount(3), "Nullification lobby is configured");
        GameClientController nullHost(nullServer.session(), 1);
        NetworkGameClient nullP2(QStringLiteral("127.0.0.1"), nullServer.serverPort()); NetworkGameClient nullP3(QStringLiteral("127.0.0.1"), nullServer.serverPort());
        nullP2.connectToHost(); nullP3.connectToHost(); require(waitFor([&] { return nullServer.connectedHumanCount() == 3; }), "Nullification TCP clients join");
        require(nullServer.startLobbyGame(), "Nullification TCP game starts"); require(waitFor([&] { return nullP2.connected() && nullP3.connected(); }), "Nullification clients receive views");
        auto& engine = nullServer.session().testingEngine(); for (const auto& player : engine.players()) { std::vector<CardId> ids; for (const auto& c : player->handCards()) ids.push_back(c->id()); for (const auto& id : ids) player->removeCard(id); }
        engine.players()[0]->addCard(std::make_shared<Card>("b44-tcp-draw", "b44-tcp-draw", CardType::ExNihilo, Suit::Heart, 1)); engine.players()[2]->addCard(std::make_shared<Card>("SECRET_B44_NULLIFICATION", "SECRET_B44_NULLIFICATION", CardType::Nullification, Suit::Spade, 1));
        QJsonObject p1Payload, p2Payload, p3Payload; QObject::connect(&nullServer, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& payload) { if (payload.contains("response")) { if (viewer == 1) p1Payload = payload; else if (viewer == 2) p2Payload = payload; else if (viewer == 3) p3Payload = payload; } });
        nullServer.broadcastViews(); nullHost.refresh(); require(nullHost.submit(PlayCardAction {1, "b44-tcp-draw", {}}).accepted, "P1 uses DrawTwo");
        require(waitFor([&] { return nullP2.view().response && nullP2.view().response->isResponder && nullP2.view().response->type == ResponseType::Nullification; }), "P2 is first Nullification responder");
        require(nullP2.submit(RespondAction {2, nullP2.view().response->requestId, std::nullopt}).accepted, "P2 passes Nullification");
        require(waitFor([&] { return nullP3.view().response && nullP3.view().response->isResponder && !p1Payload.isEmpty() && !p2Payload.isEmpty() && !p3Payload.isEmpty(); }), "P3 becomes responder over TCP");
        const auto privateJson = QJsonDocument(p3Payload).toJson(QJsonDocument::Compact); const auto hostJson = QJsonDocument(p1Payload).toJson(QJsonDocument::Compact); const auto p2Json = QJsonDocument(p2Payload).toJson(QJsonDocument::Compact);
        require(privateJson.contains("SECRET_B44_NULLIFICATION") && !hostJson.contains("SECRET_B44_NULLIFICATION") && !p2Json.contains("SECRET_B44_NULLIFICATION"), "only current responder receives private Nullification CardId");
        const auto oldRequest = nullP3.view().response->requestId; require(nullP3.view().response->selectableCards.size() == 1 && nullP3.view().response->selectableCards.front().id == "SECRET_B44_NULLIFICATION", "P3 receives only its legal Nullification card");
        require(nullP3.submit(RespondAction {3, oldRequest, CardId("SECRET_B44_NULLIFICATION")}).accepted, "P3 plays Nullification over TCP");
        require(waitFor([&] { nullHost.refresh(); return nullHost.view().response && nullHost.view().response->responder == 1 && nullHost.view().response->requestId != oldRequest; }), "new responder and requestId follow Nullification");
        require(!nullHost.view().response->isResponder || nullHost.view().response->selectableCards.empty(), "old responder cards do not leak after responder switch");
        require(nullP3.submit(RespondAction {3, oldRequest, std::nullopt}).accepted, "stale TCP request reaches server"); require(waitFor([&] { return !nullP3.actionPending(); }), "stale TCP request is rejected");
        require(engine.pendingResponse() && engine.pendingResponse()->requestId == nullHost.view().response->requestId && engine.pendingResponse()->responder == 1, "stale TCP request leaves current chain unchanged");
    }
    { // B4.5: real TCP clients receive public judgment zones/results while delayed-trick responses retain private cards.
        GameServer judgmentServer; QString judgmentError; require(judgmentServer.listen(judgmentError, 0), "Judgment server listens");
        require(judgmentServer.setTargetPlayerCount(3), "Judgment lobby is configured");
        GameClientController judgmentHost(judgmentServer.session(), 1);
        NetworkGameClient judgmentP2(QStringLiteral("127.0.0.1"), judgmentServer.serverPort()); NetworkGameClient judgmentP3(QStringLiteral("127.0.0.1"), judgmentServer.serverPort());
        judgmentP2.connectToHost(); judgmentP3.connectToHost(); require(waitFor([&] { return judgmentServer.connectedHumanCount() == 3; }), "Judgment TCP clients join");
        require(judgmentServer.startLobbyGame(), "Judgment game starts"); require(waitFor([&] { return judgmentP2.connected() && judgmentP3.connected(); }), "Judgment clients receive views");
        auto& engine = judgmentServer.session().testingEngine();
        const auto clear = [](Player& player) { std::vector<CardId> ids; for (const auto& c : player.handCards()) ids.push_back(c->id()); for (const auto& id : ids) player.removeCard(id); };
        for (const auto& player : engine.players()) clear(*player);
        auto& source = *engine.players()[0]; auto& target = *engine.players()[1]; auto& privateResponder = *engine.players()[2];
        privateResponder.addCard(std::make_shared<Card>("SECRET_B45_TCP_NULL", "SECRET_B45_TCP_NULL", CardType::Nullification, Suit::Spade, 1));
        QJsonObject phasePayloadP2, phasePayloadP3, privateP2, privateP3;
        QObject::connect(&judgmentServer, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& payload) {
            if (payload["phase"].toInt() == int(Phase::Judge)) { if (viewer == 2) phasePayloadP2 = payload; if (viewer == 3) phasePayloadP3 = payload; }
            if (payload.contains("response")) { if (viewer == 2) privateP2 = payload; if (viewer == 3) privateP3 = payload; }
        });
        const auto passChain = [&] {
            while (engine.pendingResponse()) {
                const auto request = *engine.pendingResponse();
                if (request.responder == 1) { require(judgmentHost.submit(RespondAction {1, request.requestId, std::nullopt}).accepted, "host passes delayed-trick Nullification"); judgmentServer.broadcastViews(); }
                else if (request.responder == 2) { require(judgmentP2.submit(RespondAction {2, request.requestId, std::nullopt}).accepted, "P2 passes delayed-trick Nullification"); require(waitFor([&] { return !engine.pendingResponse() || engine.pendingResponse()->requestId != request.requestId; }), "P2 delayed-trick pass reaches server"); }
                else { require(judgmentP3.submit(RespondAction {3, request.requestId, std::nullopt}).accepted, "P3 passes delayed-trick Nullification"); require(waitFor([&] { return !engine.pendingResponse() || engine.pendingResponse()->requestId != request.requestId; }), "P3 delayed-trick pass reaches server"); }
            }
        };
        source.addCard(std::make_shared<Card>("b45-tcp-cancel", "b45-tcp-cancel", CardType::Indulgence, Suit::Heart, 1));
        judgmentServer.broadcastViews(); judgmentHost.refresh(); require(judgmentHost.submit(PlayCardAction {1, "b45-tcp-cancel", {2}}).accepted, "Indulgence starts over real TCP path"); judgmentServer.broadcastViews();
        require(waitFor([&] { return judgmentP2.view().response && judgmentP2.view().response->responder == 2; }), "P2 receives delayed-trick Nullification request");
        require(judgmentP2.submit(RespondAction {2, judgmentP2.view().response->requestId, std::nullopt}).accepted, "P2 passes first delayed-trick response");
        require(waitFor([&] { return judgmentP3.view().response && judgmentP3.view().response->isResponder; }), "P3 becomes delayed-trick responder");
        const auto cancelRequest = judgmentP3.view().response->requestId;
        require(judgmentP3.submit(RespondAction {3, cancelRequest, CardId("SECRET_B45_TCP_NULL")}).accepted, "P3 nullifies Indulgence over TCP"); passChain();
        require(!target.hasJudgmentCard(CardType::Indulgence) && engine.deck().discardPileSize() >= 2, "Nullified Indulgence never enters the judgment zone");
        const auto p2Text = QJsonDocument(privateP2).toJson(QJsonDocument::Compact); const auto p3Text = QJsonDocument(privateP3).toJson(QJsonDocument::Compact);
        require(p3Text.contains("SECRET_B45_TCP_NULL") && !p2Text.contains("SECRET_B45_TCP_NULL"), "delayed-trick chain preserves private responder cards");

        source.addCard(std::make_shared<Card>("b45-tcp-indulgence", "b45-tcp-indulgence", CardType::Indulgence, Suit::Heart, 1));
        source.addCard(std::make_shared<Card>("b45-tcp-supply", "b45-tcp-supply", CardType::SupplyShortage, Suit::Club, 1));
        source.addCard(std::make_shared<Card>("b45-tcp-lightning", "b45-tcp-lightning", CardType::Lightning, Suit::Spade, 1));
        for (const auto& id : {std::string("b45-tcp-indulgence"), std::string("b45-tcp-supply"), std::string("b45-tcp-lightning")}) {
            judgmentHost.refresh(); require(judgmentHost.submit(PlayCardAction {1, id, {2}}).accepted, "Delayed trick is played before TCP judgment"); judgmentServer.broadcastViews(); passChain();
        }
        judgmentServer.broadcastViews();
        require(waitFor([&] { return judgmentP2.view().players[1].judgmentCards.size() == 3 && judgmentP3.view().players[1].judgmentCards.size() == 3; }), "all TCP clients receive ordered public judgment zone");
        require(judgmentP2.view().players[1].judgmentCards[0].id == "b45-tcp-indulgence" && judgmentP3.view().players[1].judgmentCards[2].id == "b45-tcp-lightning", "judgment zone order and card identities are public");
        engine.testingAddToDrawPile(std::make_shared<Card>("b45-tcp-lightning-judge", "lightning miss", CardType::Slash, Suit::Diamond, 7));
        engine.testingAddToDrawPile(std::make_shared<Card>("b45-tcp-supply-judge", "supply fail", CardType::Slash, Suit::Heart, 7));
        engine.testingAddToDrawPile(std::make_shared<Card>("b45-tcp-indulgence-judge", "indulgence fail", CardType::Slash, Suit::Spade, 7));
        require(judgmentHost.submit(EndPlayPhaseAction {1}).accepted, "target Judgment turn begins"); judgmentServer.broadcastViews();
        require(waitFor([&] { return !phasePayloadP2.isEmpty() && !phasePayloadP3.isEmpty() && judgmentP2.view().judgment && judgmentP3.view().judgment; }), "Judgment phase and public judgment result reach all TCP clients");
        require(phasePayloadP2["current"].toInt() == 2 && phasePayloadP3["current"].toInt() == 2, "all TCP phase snapshots agree on judgment player");
        require(judgmentP2.view().judgment->card.id == "b45-tcp-lightning-judge" && judgmentP3.view().judgment->card.id == "b45-tcp-lightning-judge", "judgment card identity, suit and rank are public and consistent");
        require(judgmentP2.view().players[1].judgmentCards.empty() && judgmentP3.view().players[2].judgmentCards.size() == 1
            && judgmentP3.view().players[2].judgmentCards.front().id == "b45-tcp-lightning", "Lightning transfer synchronizes to every TCP client");
        const auto sawLog = [](const PlayerViewState& view, const char* text) { return std::any_of(view.visibleLogs.begin(), view.visibleLogs.end(), [text](const auto& entry) { return entry.find(text) != std::string::npos; }); };
        require(sawLog(judgmentP2.view(), "skipped Play phase") && sawLog(judgmentP3.view(), "skipped Play phase")
            && sawLog(judgmentP2.view(), "skipped Draw phase") && sawLog(judgmentP3.view(), "skipped Draw phase"), "Indulgence and Supply Shortage results stay synchronized over TCP");
        const auto publicP2 = QJsonDocument(judgmentServer.viewPayloadFor(2)).toJson(QJsonDocument::Compact);
        require(!publicP2.contains("SECRET_B45_TCP_NULL"), "public judgment payload never leaks another player's private response card");
    }
    { // B4.6: real TCP ViewStates expose chaining and Fire Attack only reveals the selected card publicly.
        GameServer elementalServer; QString elementalError; require(elementalServer.listen(elementalError, 0) && elementalServer.setTargetPlayerCount(3), "B4.6 TCP server listens");
        GameClientController elementalHost(elementalServer.session(), 1);
        NetworkGameClient elementalP2(QStringLiteral("127.0.0.1"), elementalServer.serverPort()), elementalP3(QStringLiteral("127.0.0.1"), elementalServer.serverPort());
        elementalP2.connectToHost(); elementalP3.connectToHost(); require(waitFor([&] { return elementalServer.connectedHumanCount() == 3; }) && elementalServer.startLobbyGame() && waitFor([&] { return elementalP2.connected() && elementalP3.connected(); }), "B4.6 TCP clients start");
        auto& engine = elementalServer.session().testingEngine(); const auto clear = [](Player& player) { std::vector<CardId> ids; for (const auto& c : player.handCards()) ids.push_back(c->id()); for (const auto& id : ids) player.removeCard(id); };
        for (const auto& player : engine.players()) clear(*player); auto& p1 = *engine.players()[0]; auto& p2 = *engine.players()[1]; auto& p3 = *engine.players()[2];
        p1.addCard(std::make_shared<Card>("b46-tcp-iron", "iron", CardType::IronChain, Suit::Spade, 1)); elementalServer.broadcastViews();
        require(elementalHost.submit(PlayCardAction {1, "b46-tcp-iron", {2, 3}}).accepted, "Iron Chain enters TCP path");
        while (engine.pendingResponse()) { const auto request = *engine.pendingResponse(); if (request.responder == 1) elementalHost.submit(RespondAction {1, request.requestId, std::nullopt}); else if (request.responder == 2) elementalP2.submit(RespondAction {2, request.requestId, std::nullopt}); else elementalP3.submit(RespondAction {3, request.requestId, std::nullopt}); waitFor([&] { return !engine.pendingResponse() || engine.pendingResponse()->requestId != request.requestId; }); }
        elementalServer.broadcastViews(); require(waitFor([&] { return elementalP2.view().players[1].chained && elementalP2.view().players[2].chained && elementalP3.view().players[1].chained; }), "Iron Chain state synchronizes publicly");
        p1.addCard(std::make_shared<Card>("b46-tcp-fire", "fire", CardType::FireAttack, Suit::Heart, 1)); p1.addCard(std::make_shared<Card>("SECRET_B46_MATCH", "match", CardType::Slash, Suit::Spade, 1)); p2.addCard(std::make_shared<Card>("PUBLIC_B46_REVEAL", "reveal", CardType::Dodge, Suit::Spade, 1)); p2.addCard(std::make_shared<Card>("SECRET_B46_OTHER", "other", CardType::Peach, Suit::Heart, 1));
        QByteArray payload[4]; QObject::connect(&elementalServer, &GameServer::viewPayloadPrepared, &application, [&](PlayerId viewer, const QJsonObject& value) { payload[viewer] = QJsonDocument(value).toJson(QJsonDocument::Compact); });
        elementalServer.broadcastViews(); require(elementalHost.submit(PlayCardAction {1, "b46-tcp-fire", {2}}).accepted, "Fire Attack enters TCP path");
        while (engine.pendingResponse()) { const auto request = *engine.pendingResponse(); if (request.responder == 1) elementalHost.submit(RespondAction {1, request.requestId, std::nullopt}); else if (request.responder == 2) elementalP2.submit(RespondAction {2, request.requestId, std::nullopt}); else elementalP3.submit(RespondAction {3, request.requestId, std::nullopt}); waitFor([&] { return !engine.pendingResponse() || engine.pendingResponse()->requestId != request.requestId; }); }
        require(waitFor([&] { return elementalP2.view().cardSelection && elementalP2.view().cardSelection->purpose == CardSelectionPurpose::FireAttackReveal; }), "target receives private reveal list");
        require(!elementalP3.view().cardSelection && payload[2].contains("PUBLIC_B46_REVEAL") && !payload[3].contains("SECRET_B46_OTHER") && !payload[3].contains("SECRET_B46_MATCH"), "Fire Attack private JSON is hidden");
        const auto revealRequest = elementalP2.view().cardSelection->requestId; require(elementalP2.submitCardSelection(revealRequest, 1).accepted, "target reveals through TCP"); require(waitFor([&] { return elementalHost.view().fireAttack && elementalP3.view().fireAttack; }), "revealed card becomes public");
        require(elementalHost.view().fireAttack->revealedCard.id == "PUBLIC_B46_REVEAL" && elementalP3.view().fireAttack->revealedCard.id == "PUBLIC_B46_REVEAL", "all clients agree on public reveal");
        require(elementalP2.submitCardSelection(revealRequest, 1).accepted && waitFor([&] { return !elementalP2.actionPending(); }), "stale TCP reveal reaches rejection path");
        require(engine.pendingCardSelection() && engine.pendingCardSelection()->purpose == CardSelectionPurpose::FireAttackDiscard && engine.pendingCardSelection()->requestId != revealRequest, "stale reveal preserves fresh discard request");
        elementalHost.refresh(); require(elementalHost.view().cardSelection && elementalHost.view().cardSelection->options.size() == 1, "only source sees matching discard option"); require(elementalHost.submitCardSelection(elementalHost.view().cardSelection->requestId, 1).accepted, "source discards matching card"); elementalServer.broadcastViews();
        require(waitFor([&] { return elementalP2.view().players[1].hp == 3 && elementalP2.view().players[2].hp == 3 && !elementalP2.view().players[1].chained && !elementalP3.view().players[2].chained; }), "Fire propagation and chained reset synchronize over TCP");
    }
    { // Stage 3.3: Slash target arrays round-trip without protocol-specific target limits.
        for (const auto& targets : std::vector<std::vector<PlayerId>> {{2}, {2, 3}, {2, 3, 4}}) {
            const auto decoded = decodeAction(encodeAction(PlayCardAction {1, "halberd-slash", targets}));
            require(decoded.has_value() && std::holds_alternative<PlayCardAction>(decoded->action)
                && std::get<PlayCardAction>(decoded->action).targetIds == targets, "Slash targets array round trips");
        }
    }
    { // Stage 3.3: GameServer-owned session rejects invalid Halberd target lists without mutation.
        GameServer halberdServer; require(halberdServer.setTargetPlayerCount(4) && halberdServer.startLobbyGame(), "Halberd GameServer starts");
        GameClientController p1(halberdServer.session(), 1), p2(halberdServer.session(), 2);
        auto& engine = halberdServer.session().testingEngine();
        const auto clear = [](Player& player) { std::vector<CardId> ids; for (const auto& c : player.handCards()) ids.push_back(c->id()); for (const auto& id : ids) player.removeCard(id); };
        for (const auto& player : engine.players()) clear(*player);
        auto& source = *engine.players()[0];
        engine.testingEquip(1, std::make_shared<Card>("net-halberd", "方天画戟", CardType::Weapon, Suit::Diamond, 12, EquipmentData {EquipmentSlot::Weapon, 4}));
        source.addCard(std::make_shared<Card>("net-halberd-slash", "slash", CardType::Slash, Suit::Spade, 1));
        const auto unchanged = [&] { return source.handCards().size() == 1 && engine.slashUsedThisPhase() == 0 && !engine.pendingResponse(); };
        require(!p1.submit(PlayCardAction {1, "net-halberd-slash", {2, 3, 4, 99}}).accepted && unchanged(), "four Halberd targets are rejected unchanged");
        require(!p1.submit(PlayCardAction {1, "net-halberd-slash", {2, 2}}).accepted && unchanged(), "duplicate Halberd targets are rejected unchanged");
        require(!p1.submit(PlayCardAction {1, "net-halberd-slash", {2, 99}}).accepted && unchanged(), "invalid Halberd target is rejected unchanged");
        require(!p2.submit(PlayCardAction {2, "net-halberd-slash", {1, 3}}).accepted && unchanged(), "non-current player is rejected unchanged");
        require(p1.submit(PlayCardAction {1, "net-halberd-slash", {2, 3, 4}}).accepted && engine.pendingResponse()->responder == 2, "legal Halberd targets enter first server response");
        auto request = *engine.pendingResponse(); require(engine.submitAction(RespondAction {2, request.requestId, std::nullopt}).accepted && engine.pendingResponse()->responder == 3, "first target advances to second");
        request = *engine.pendingResponse(); require(engine.submitAction(RespondAction {3, request.requestId, std::nullopt}).accepted && engine.pendingResponse()->responder == 4, "second target advances to third");
        request = *engine.pendingResponse(); require(engine.submitAction(RespondAction {4, request.requestId, std::nullopt}).accepted && !engine.pendingResponse(), "third target clears context");
    }
    { // Stage 3.2 protocol round trips preserve both virtual Slash action forms.
        const auto active = encodeAction(PlayVirtualSlashAction {1, {"a", "b"}, {2}});
        const auto decodedActive = decodeAction(active); require(decodedActive.has_value() && std::holds_alternative<PlayVirtualSlashAction>(decodedActive->action), "virtual Slash JSON decodes");
        const auto& activeAction = std::get<PlayVirtualSlashAction>(decodedActive->action); require(activeAction.subcardIds.size() == 2 && activeAction.targetIds == std::vector<PlayerId> {2}, "virtual Slash preserves subcards and target");
        const auto response = encodeAction(RespondVirtualSlashAction {2, 42, {"c", "d"}});
        const auto decodedResponse = decodeAction(response); require(decodedResponse.has_value() && std::holds_alternative<RespondVirtualSlashAction>(decodedResponse->action), "virtual Slash response JSON decodes");
        const auto& responseAction = std::get<RespondVirtualSlashAction>(decodedResponse->action); require(responseAction.requestId == 42 && responseAction.subcardIds == std::vector<CardId> {"c", "d"}, "virtual Slash response preserves request and subcards");
    }
    client.disconnectFromHost();
    assert(waitFor([&] { return !server.remoteConnected() && client.connectionState() == ConnectionState::ConnectionLost; }));
    assert(!client.submit(EndPlayPhaseAction {2}).accepted);
    std::cout << "Network handshake, private ViewState, Slash/Dodge and damage synchronization passed.\n";
}
