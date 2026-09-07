#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

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
    QEventLoop loop; QTimer timeout; QTimer poll;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
    timeout.start(milliseconds); poll.start(10); loop.exec(); return condition();
}

void serverListensOnAnyIPv4()
{
    GameServer server; QString error;
    expect(server.listen(error, 0), "LAN server starts");
    expect(server.serverAddress().protocol() == QAbstractSocket::IPv4Protocol && !server.serverAddress().isLoopback()
               && server.serverAddress() == QHostAddress::AnyIPv4,
           "server listens on all IPv4 interfaces rather than LocalHost only");
}

void localhostStillConnectsToLanServer()
{
    GameServer server; QString error;
    expect(server.listen(error, 0), "LAN server starts for localhost compatibility");
    NetworkGameClient client(QStringLiteral("127.0.0.1"), server.serverPort());
    client.connectToHost();
    expect(waitFor([&] { return server.remoteConnected() && client.selfPlayerId() == 2; }),
           "localhost still connects to an AnyIPv4 listener");
}

void duplicateHostListenIsRejectedSafely()
{
    GameServer first; QString firstError;
    expect(first.listen(firstError, 0), "first LAN server starts");
    GameServer second; QString secondError;
    expect(!second.listen(secondError, first.serverPort()) && !secondError.isEmpty(),
           "second server on an occupied port is rejected with an error");
    expect(first.serverPort() != 0 && first.serverAddress() == QHostAddress::AnyIPv4,
           "the first listener remains healthy after duplicate listen rejection");
    QString repeatError;
    expect(!first.listen(repeatError, first.serverPort()) && repeatError.contains(QStringLiteral("已经启动")),
           "re-listening through the same server is rejected safely");
}

void lanAddressDiscoveryExcludesLoopback()
{
    for (const auto& address : GameServer::discoverLanIpv4Addresses()) {
        const QHostAddress value(address);
        expect(value.protocol() == QAbstractSocket::IPv4Protocol && !value.isLoopback() && value.toString() != QStringLiteral("127.0.0.1"),
               "LAN discovery contains only non-loopback IPv4 addresses");
    }
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    serverListensOnAnyIPv4();
    localhostStillConnectsToLanServer();
    duplicateHostListenIsRejectedSafely();
    lanAddressDiscoveryExcludesLoopback();
    std::cout << "BasicSanguoshaLanNetworkTests PASS\n";
}
