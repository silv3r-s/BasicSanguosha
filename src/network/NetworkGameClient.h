#pragma once

#include <QTcpSocket>
#include <QElapsedTimer>
#include <functional>

#include <vector>

#include "client/IGameClient.h"
#include "network/NetworkProtocol.h"

namespace sanguosha::network {
enum class ConnectionState { Disconnected, Connecting, Connected, InGame, ConnectionLost, Error };
struct LobbyPlayerState { PlayerId playerId {0}; int seat {0}; QString nickname; bool connected {true}; bool host {false}; bool ai {false}; };
struct LobbyState { int targetPlayerCount {2}; int connectedHumanCount {1}; int aiPlayerCount {1}; GameMode gameMode {GameMode::FreeForAll}; bool gameStarted {false}; std::vector<LobbyPlayerState> players; };

class NetworkGameClient final : public QObject, public IGameClient {
    Q_OBJECT
public:
    explicit NetworkGameClient(QString host, quint16 port = DefaultPort, QString displayName = {}, QObject* parent = nullptr);
    ~NetworkGameClient() override;
    void connectToHost();
    void reconnectToHost();
    void disconnectForReconnect();
    void disconnectFromHost();
    [[nodiscard]] const PlayerViewState& view() const noexcept override; [[nodiscard]] PlayerId selfPlayerId() const noexcept override;
    ActionResult submit(const GameAction& action) override; ActionResult submitCardSelection(std::uint64_t requestId, SelectionOptionId optionId) override; ActionResult submitCardSelection(std::uint64_t requestId, const std::vector<SelectionOptionId>& optionIds) override;
    [[nodiscard]] bool canPlayCard(const CardId& cardId) const override; [[nodiscard]] bool canPlayCardOnTarget(const CardId& cardId, PlayerId targetId) const override; [[nodiscard]] int requiredDiscardCount() const override;
    [[nodiscard]] bool connected() const noexcept override; [[nodiscard]] bool actionPending() const noexcept override; [[nodiscard]] ConnectionState connectionState() const noexcept { return state_; } [[nodiscard]] std::string connectionStatus() const override; void restart() override {} void refresh() override;
    [[nodiscard]] const LobbyState& lobbyState() const noexcept { return lobbyState_; }
    [[nodiscard]] const QString& reconnectToken() const noexcept { return reconnectToken_; }
    void setDeserializedViewObserverForTesting(std::function<void(const PlayerViewState&)> observer);
    void setAppliedViewObserverForTesting(std::function<void(const PlayerViewState&, bool)> observer);
signals: void viewChanged(); void statusChanged(); void lobbyChanged();
private: void send(const QJsonObject& message); void onReadyRead();
    QElapsedTimer timeoutReceipt_; int timeoutReceiptRemainingMs_ {0};
    QString displayName_;
    std::function<void(const PlayerViewState&)> deserializedViewObserverForTesting_;
    std::function<void(const PlayerViewState&, bool)> appliedViewObserverForTesting_;
    QString host_; quint16 port_; QTcpSocket socket_; MessageCodec codec_; PlayerViewState view_ {0,0,0,Phase::Start}; LobbyState lobbyState_; std::string status_ {"正在连接……"}; ConnectionState state_ {ConnectionState::Disconnected}; bool welcomed_ {false}; bool reconnecting_ {false}; QString reconnectToken_; std::uint64_t nextRequest_ {1}; std::optional<std::uint64_t> pendingRequest_;
};
} // namespace sanguosha::network
