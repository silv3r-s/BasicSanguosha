#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>

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

void lobbyAllocatesSmallestFreeSeatsWithoutConnectionOrderAssumption()
{
    GameServer server; QString error;
    expect(server.listen(error, 0) && server.setTargetPlayerCount(4) && server.lobbyState().gameMode == GameMode::FreeForAll, "lobby server starts with four FreeForAll seats");
    server.setReconnectGracePeriodForTesting(50);
    NetworkGameClient first("127.0.0.1", server.serverPort());
    NetworkGameClient second("127.0.0.1", server.serverPort());
    first.connectToHost(); second.connectToHost();
    expect(waitFor([&] { return server.connectedHumanCount() == 3; }), "both remote clients join");
    expect(waitFor([&] { return first.lobbyState().gameMode == GameMode::FreeForAll && second.lobbyState().gameMode == GameMode::FreeForAll; }), "remote lobby receives the authoritative FreeForAll mode");
    expect(waitFor([&] {
        const std::array<PlayerId, 2> seats {first.selfPlayerId(), second.selfPlayerId()};
        return (seats[0] == 2 && seats[1] == 3) || (seats[0] == 3 && seats[1] == 2);
    }), "the two smallest available seats are allocated exactly once");
    const auto releasedSeat = second.selfPlayerId();
    second.disconnectFromHost();
    expect(waitFor([&] { return server.connectedHumanCount() == 2 && server.testingReconnectReservationCount() == 1; }),
           "disconnect reserves its lobby seat during reconnect grace");
    NetworkGameClient graceJoiner("127.0.0.1", server.serverPort());
    graceJoiner.connectToHost();
    expect(waitFor([&] { return server.connectedHumanCount() == 3 && graceJoiner.selfPlayerId() == 4; }),
           "a new client cannot take the reserved seat during grace");
    graceJoiner.disconnectFromHost();
    expect(waitFor([&] { return server.testingReconnectReservationCount() == 0; }),
           "reservation cleanup releases expired lobby seats");
    NetworkGameClient replacement("127.0.0.1", server.serverPort());
    replacement.connectToHost();
    expect(waitFor([&] { return server.connectedHumanCount() == 3 && replacement.selfPlayerId() == releasedSeat; }),
           "the released seat is reused after reconnect grace expires");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    lobbyAllocatesSmallestFreeSeatsWithoutConnectionOrderAssumption();
    std::cout << "BasicSanguoshaLobbySeatAllocationTests PASS\n";
}
