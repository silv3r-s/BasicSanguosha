#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
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
    QEventLoop loop; QTimer timeout, poll; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    timeout.start(milliseconds); poll.start(10); loop.exec(); return condition();
}

struct Fixture {
    GameServer server;
    GameClientController host;
    std::unique_ptr<NetworkGameClient> attacker, target, observer;

    Fixture(bool targetHasHand = true) : host(server.session(), 1)
    {
        QString error;
        expect(server.listen(error, 0) && server.setTargetPlayerCount(4), "Double Sword TCP server starts");
        attacker = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort()); attacker->connectToHost();
        expect(waitFor([&] { return server.connectedHumanCount() == 2 && attacker->selfPlayerId() == 2; }), "attacker receives seat P2");
        target = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort()); target->connectToHost();
        expect(waitFor([&] { return server.connectedHumanCount() == 3 && target->selfPlayerId() == 3; }), "target receives seat P3");
        observer = std::make_unique<NetworkGameClient>("127.0.0.1", server.serverPort()); observer->connectToHost();
        expect(waitFor([&] { return server.connectedHumanCount() == 4 && observer->selfPlayerId() == 4; }), "observer receives seat P4");
        expect(server.startLobbyGame(), "server starts the TCP game");
        expect(waitFor([&] { return attacker->connected() && target->connected() && observer->connected(); }), "TCP clients enter the game");
        auto& engine = server.session().testingEngine();
        for (const auto& player : engine.players()) {
            std::vector<CardId> ids;
            for (const auto& card : player->handCards()) ids.push_back(card->id());
            for (const auto& id : ids) player->removeCard(id);
        }
        engine.testingSetGender(2, Gender::Male);
        engine.testingSetGender(3, Gender::Female);
        engine.testingSetGender(4, Gender::Male);
        engine.testingEquip(2, std::make_shared<Card>("double-network-sword", "雌雄双股剑", CardType::Weapon, Suit::Spade, 2,
                                                       EquipmentData {EquipmentSlot::Weapon, 2}));
        engine.players()[1]->addCard(std::make_shared<Card>("double-network-slash", "network slash", CardType::Slash, Suit::Club, 7));
        if (targetHasHand) {
            engine.players()[2]->addCard(std::make_shared<Card>("DOUBLE_SECRET_ID", "DOUBLE SECRET NAME", CardType::Peach, Suit::Spade, 9));
            engine.players()[2]->addCard(std::make_shared<Card>("DOUBLE_SECRET_ID_2", "DOUBLE SECRET NAME 2", CardType::Dodge, Suit::Heart, 12));
        }
        expect(host.submit(EndPlayPhaseAction {1}).accepted, "advance to attacker Play phase");
        server.broadcastViews();
        expect(waitFor([&] { return attacker->view().currentTurnPlayer == 2 && attacker->view().currentPhase == Phase::Play; }), "attacker has the live Play phase");
    }

    void begin()
    {
        expect(attacker->submit(PlayCardAction {2, "double-network-slash", {3}}).accepted, "attacker uses a real TCP Slash");
        expect(waitFor([&] { return server.session().testingEngine().pendingCardSelection().has_value() && target->view().cardSelection.has_value(); }), "target receives Double Sword selection over TCP");
    }
    const CardSelectionView& selection() const { return *target->view().cardSelection; }
};

void doubleSwordNetworkTargetReceivesRequest()
{
    Fixture f; f.begin(); const auto& s = f.selection();
    expect(s.purpose == CardSelectionPurpose::DoubleSwordDiscard && s.targetId == 3 && s.minCount == 0 && s.maxCount == 1
               && s.prompt.find("雌雄双股剑") != std::string::npos && s.options.size() == 2
               && s.options[0].displayName == "DOUBLE SECRET NAME" && !s.options[0].hidden,
           "target receives the authoritative Double Sword request, named own-hand candidates, and pass choice");
    const auto& context = f.server.session().testingEngine().doubleSwordContext();
    expect(context && context->source == 2 && context->target == 3 && context->requestId == s.requestId,
           "server context retains the authoritative attacker, target, and request ID");
}

void doubleSwordNetworkAttackerCannotChooseForTarget()
{
    Fixture f; f.begin(); const auto id = f.selection().requestId; const auto sourceBefore = f.server.session().testingEngine().player(2)->handCards().size();
    f.attacker->submitCardSelection(id, 1);
    expect(waitFor([&] { return !f.attacker->actionPending(); }) && f.server.session().testingEngine().pendingCardSelection()
               && f.server.session().testingEngine().player(2)->handCards().size() == sourceBefore
               && f.server.session().testingEngine().player(3)->handCards().size() == 2,
           "attacker TCP action cannot choose for the target");
}

void doubleSwordNetworkObserverCannotInteract()
{
    Fixture f; f.begin(); const auto id = f.selection().requestId;
    f.observer->submitCardSelection(id, 1);
    expect(waitFor([&] { return !f.observer->actionPending(); }) && f.server.session().testingEngine().pendingCardSelection()
               && f.server.session().testingEngine().pendingCardSelection()->requester == 3,
           "observer TCP action cannot operate Double Sword");
}

void doubleSwordNetworkDoesNotLeakTargetHand()
{
    Fixture f; f.begin();
    const auto attackerPayload = QJsonDocument(f.server.viewPayloadFor(2)).toJson(QJsonDocument::Compact);
    const auto observerPayload = QJsonDocument(f.server.viewPayloadFor(4)).toJson(QJsonDocument::Compact);
    const auto targetPayload = QJsonDocument(f.server.viewPayloadFor(3)).toJson(QJsonDocument::Compact);
    expect(targetPayload.contains("DOUBLE_SECRET_ID") && targetPayload.contains("DOUBLE SECRET NAME")
               && !attackerPayload.contains("DOUBLE_SECRET_ID") && !attackerPayload.contains("DOUBLE SECRET NAME")
               && !observerPayload.contains("DOUBLE_SECRET_ID") && !observerPayload.contains("DOUBLE SECRET NAME")
               && !f.attacker->view().cardSelection && !f.observer->view().cardSelection,
           "target-only TCP selection and hand identities are not leaked to attacker or observer");
}

void doubleSwordNetworkTargetCanDiscardHandCard()
{
    Fixture f; f.begin(); const auto s = f.selection(); const auto sourceBefore = f.server.session().testingEngine().player(2)->handCards().size();
    expect(f.target->submitCardSelection(s.requestId, s.options.front().optionId).accepted, "target submits a real TCP discard choice");
    expect(waitFor([&] { return f.server.session().testingEngine().player(3)->handCards().size() == 1; })
               && f.server.session().testingEngine().player(2)->handCards().size() == sourceBefore
               && !f.server.session().testingEngine().pendingCardSelection() && f.server.session().testingEngine().pendingResponse(),
           "target discard is resolved authoritatively and Slash continues");
}

void doubleSwordNetworkTargetCanLetAttackerDraw()
{
    Fixture f; f.begin(); const auto s = f.selection(); const auto sourceBefore = f.server.session().testingEngine().player(2)->handCards().size();
    expect(f.target->submitCardSelection(s.requestId, std::vector<SelectionOptionId> {}).accepted, "target submits the TCP pass choice");
    expect(waitFor([&] { return f.server.session().testingEngine().player(2)->handCards().size() == sourceBefore + 1; })
               && f.server.session().testingEngine().player(3)->handCards().size() == 2
               && !f.server.session().testingEngine().pendingCardSelection() && f.server.session().testingEngine().pendingResponse(),
           "target pass draws one for attacker and Slash continues");
}

void doubleSwordNetworkNoHandAutoDraws()
{
    Fixture f(false);
    const auto sourceBefore = f.server.session().testingEngine().player(2)->handCards().size();
    expect(f.attacker->submit(PlayCardAction {2, "double-network-slash", {3}}).accepted, "attacker sends real TCP Slash against handless target");
    expect(waitFor([&] { const auto& engine = f.server.session().testingEngine(); return engine.pendingResponse().has_value()
                              && !engine.pendingCardSelection() && engine.player(2)->handCards().size() == sourceBefore; })
               && !f.target->view().cardSelection,
           "handless target receives no meaningless selection and attacker draws");
}

void doubleSwordNetworkRejectsStaleRequest()
{
    Fixture f; f.begin(); const auto s = f.selection();
    expect(f.target->submitCardSelection(s.requestId, std::vector<SelectionOptionId> {}).accepted, "target completes the initial request");
    expect(waitFor([&] { return !f.server.session().testingEngine().pendingCardSelection(); }), "initial request is complete");
    const auto sourceAfter = f.server.session().testingEngine().player(2)->handCards().size();
    f.target->submitCardSelection(s.requestId, s.options.front().optionId);
    expect(waitFor([&] { return !f.target->actionPending(); }) && f.server.session().testingEngine().player(2)->handCards().size() == sourceAfter
               && !f.server.session().testingEngine().doubleSwordContext(),
           "stale TCP request cannot replay the draw, discard, or context");
}

void doubleSwordNetworkRejectsRequestAfterReturnToLobby()
{
    Fixture f; f.begin(); const auto s = f.selection(); const auto sourceBefore = f.server.session().testingEngine().player(2)->handCards().size();
    expect(f.server.returnToLobby() && !f.server.gameStarted(), "host returns to lobby during Double Sword request");
    expect(waitFor([&] { return !f.target->lobbyState().gameStarted && !f.target->view().cardSelection; }), "remote target receives lobby state without Double Sword interaction");
    expect(f.target->submitCardSelection(s.requestId, s.options.front().optionId).accepted, "target sends its stale TCP action for rejection");
    expect(waitFor([&] { return !f.target->actionPending(); }) && !f.server.gameStarted()
               && !f.server.session().testingEngine().pendingCardSelection() && !f.server.session().testingEngine().doubleSwordContext(),
           "stale lobby request is rejected without restoring Double Sword interaction");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    doubleSwordNetworkTargetReceivesRequest();
    doubleSwordNetworkAttackerCannotChooseForTarget();
    doubleSwordNetworkObserverCannotInteract();
    doubleSwordNetworkDoesNotLeakTargetHand();
    doubleSwordNetworkTargetCanDiscardHandCard();
    doubleSwordNetworkTargetCanLetAttackerDraw();
    doubleSwordNetworkNoHandAutoDraws();
    doubleSwordNetworkRejectsStaleRequest();
    doubleSwordNetworkRejectsRequestAfterReturnToLobby();
    std::cout << "BasicSanguoshaDoubleSwordNetworkTests PASS\n";
}
