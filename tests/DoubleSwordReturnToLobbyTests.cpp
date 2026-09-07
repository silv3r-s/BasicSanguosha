#include <QCoreApplication>

#include <cstdlib>
#include <iostream>
#include <memory>

#include "network/GameServer.h"

using namespace sanguosha;
using namespace sanguosha::network;

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

void clearHand(Player& player)
{
    std::vector<CardId> ids;
    for (const auto& card : player.handCards()) ids.push_back(card->id());
    for (const auto& id : ids) player.removeCard(id);
}

void doubleSwordClearsOnReturnToLobby()
{
    GameServer server;
    expect(server.startLobbyGame(), "server starts a real game");
    auto& session = server.session();
    const auto attackerClient = session.addClient(1);
    const auto targetClient = session.addClient(2);
    auto& engine = session.testingEngine();
    clearHand(*engine.players()[0]);
    clearHand(*engine.players()[1]);
    engine.testingSetGender(1, Gender::Male);
    engine.testingSetGender(2, Gender::Female);
    engine.testingEquip(1, std::make_shared<Card>("double-return-sword", "雌雄双股剑", CardType::Weapon, Suit::Spade, 2,
                                                   EquipmentData {EquipmentSlot::Weapon, 2}));
    engine.players()[1]->addCard(std::make_shared<Card>("double-return-target", "target", CardType::Peach, Suit::Heart, 1));
    engine.testingStartSlash(1, {2});
    const auto selection = engine.pendingCardSelection();
    expect(selection && selection->purpose == CardSelectionPurpose::DoubleSwordDiscard && selection->requester == 2,
           "Double Sword creates a pending target selection");
    const auto oldRequestId = selection->requestId;
    expect(session.viewFor(targetClient).cardSelection.has_value(), "target receives the selection before lobby return");

    expect(server.returnToLobby() && !server.gameStarted(), "formal returnToLobby exits the game");
    expect(!engine.pendingCardSelection() && !engine.doubleSwordContext(), "returnToLobby clears Double Sword context and selection");
    expect(session.timeoutContextKey() != "selection:" + std::to_string(oldRequestId), "returnToLobby clears the old selection timeout");
    const auto lobbyView = session.viewFor(attackerClient);
    expect(!lobbyView.cardSelection && !lobbyView.response, "lobby ViewState has no game interaction");
    expect(!session.submitCardSelection(targetClient, oldRequestId, 1).accepted, "old selection is rejected after lobby return");
    expect(!server.gameStarted() && !engine.pendingCardSelection() && !engine.doubleSwordContext(),
           "stale selection cannot restore the interaction");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    doubleSwordClearsOnReturnToLobby();
    std::cout << "BasicSanguoshaDoubleSwordReturnToLobbyTests PASS\n";
}
