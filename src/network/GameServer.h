#pragma once

#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QTimer>

#include <optional>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>

#include "network/NetworkProtocol.h"
#include "network/NetworkGameClient.h"
#include "session/GameSession.h"

namespace sanguosha::ai { class AIController; }

namespace sanguosha::network {

class GameServer final : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 Port = DefaultPort;
    explicit GameServer(const std::vector<std::string>& playerNames = {"Player 1", "Player 2"}, QObject* parent = nullptr);
    ~GameServer() override;
    bool listen(QString& error, quint16 port = Port);
    void close();
    [[nodiscard]] QHostAddress serverAddress() const;
    [[nodiscard]] static QStringList discoverLanIpv4Addresses();
    void setHostDisplayName(QString name);
    [[nodiscard]] GameSession& session() noexcept;
    [[nodiscard]] bool remoteConnected() const noexcept;
    [[nodiscard]] quint16 serverPort() const noexcept;
    void broadcastViews();
    void setInteractionTimeoutForTesting(int milliseconds);
    void setReconnectGracePeriodForTesting(int milliseconds);
#ifdef SANGUOSHA_TESTING
    void testingSetIdentityAssignments(const std::vector<PlayerIdentity>& identities);
    [[nodiscard]] int testingReconnectReservationCount() const noexcept;
#endif
    [[nodiscard]] int targetPlayerCount() const noexcept;
    [[nodiscard]] int connectedHumanCount() const noexcept;
    [[nodiscard]] int aiPlayerCount() const noexcept;
    bool setTargetPlayerCount(int count);
    bool addAI();
    bool removeAI();
    [[nodiscard]] bool gameStarted() const noexcept;
    [[nodiscard]] LobbyState lobbyState() const;
    bool startLobbyGame();
    bool returnToLobby();
    [[nodiscard]] QJsonObject viewPayloadFor(SessionClientId clientId) const;
signals:
    void connectionStatusChanged(const QString& status, bool connected);
    void stateChanged();
    void viewPayloadPrepared(PlayerId viewer, const QJsonObject& payload);
private:
    struct ConnectionContext {
        QTcpSocket* socket {};
        MessageCodec codec;
        bool helloReceived {false};
        bool joined {false};
        std::optional<SessionClientId> sessionClientId;
        std::optional<PlayerId> playerId;
        QString displayName;
        QString reconnectToken;
    };
    struct ReconnectRecord { PlayerId playerId {}; QString displayName; bool connected {false}; QElapsedTimer disconnectedAt; };
    void onConnection(); void onReadyRead(QTcpSocket* socket); void onDisconnected(QTcpSocket* socket); void onTimeoutTick(); void expireReconnectReservations(); void refreshTimeoutContext(); void sendTo(QTcpSocket* target, const QJsonObject& message); void sendViews(); void reject(QTcpSocket* socket, const QString& message); void broadcastLobbyState(); void createAIControllers(); void clearAIControllers(); void scheduleAIActions(); void onAIAction();
    [[nodiscard]] std::optional<PlayerId> allocateSeat() const;
    // Destroy the server (and its child sockets) before the connection registry. Socket
    // destruction emits disconnected(), whose handler still needs the registry alive.
    std::map<QTcpSocket*, ConnectionContext> connections_;
    std::unordered_map<std::string, ReconnectRecord> reconnectRecords_;
    QTcpServer server_;
    int targetPlayerCount_ {2}; bool gameStarted_ {false};
    QString hostDisplayName_ {QStringLiteral("玩家1")};
    GameSession session_;
    static constexpr int kDefaultPhaseTimeoutMs = 30000;
    int interactionTimeoutMs_ {kDefaultPhaseTimeoutMs}; QTimer timeoutTimer_; QElapsedTimer timeoutElapsed_; std::string timeoutContext_; int lastTimeoutBroadcastBucket_ {-1};
    int reconnectGracePeriodMs_ {60000};
    std::vector<std::unique_ptr<ai::AIController>> aiControllers_;
    // Coalesces state-change wakeups without losing a wakeup raised while a
    // drive is being dispatched.  Controller actions themselves stay queued.
    bool aiDriveScheduled_ {false};
    bool aiDriveDispatching_ {false};
    bool aiDriveAgain_ {false};
#ifdef SANGUOSHA_TESTING
    std::vector<PlayerIdentity> testingIdentities_;
#endif
};

} // namespace sanguosha::network
