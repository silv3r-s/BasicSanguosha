#include "network/NetworkGameClient.h"

#include <QHostAddress>
#include <QJsonArray>

#include <algorithm>

namespace sanguosha::network {

NetworkGameClient::NetworkGameClient(QString host, quint16 port, QString displayName, QObject* parent)
    : QObject(parent), displayName_(std::move(displayName)), host_(std::move(host)), port_(port)
{
    connect(&socket_, &QTcpSocket::connected, this, [this] {
        state_ = ConnectionState::Connected;
        status_ = "连接成功，正在验证协议……";
        QJsonObject hello{{"type", "hello"}, {"protocolVersion", ProtocolVersion}, {"displayName", displayName_}};
        if (reconnecting_ && !reconnectToken_.isEmpty()) hello["reconnectToken"] = reconnectToken_;
        send(hello);
        emit statusChanged();
    });
    connect(&socket_, &QTcpSocket::readyRead, this, &NetworkGameClient::onReadyRead);
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this] {
        if (state_ != ConnectionState::ConnectionLost) state_ = ConnectionState::Error;
        switch (socket_.error()) {
        case QAbstractSocket::ConnectionRefusedError:
            status_ = "连接失败：主机拒绝连接，请检查局域网地址、端口和房主状态。"; break;
        case QAbstractSocket::HostNotFoundError:
            status_ = "连接失败：找不到主机，请检查局域网 IPv4 地址。"; break;
        case QAbstractSocket::SocketTimeoutError:
            status_ = "连接超时：请确认两台电脑位于同一局域网，且房主允许专用网络访问。"; break;
        case QAbstractSocket::NetworkError:
            status_ = "连接失败：网络不可达，请检查 Wi-Fi 或网线连接。"; break;
        default:
            status_ = "连接失败：网络套接字发生错误。"; break;
        }
        emit statusChanged();
    });
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        pendingRequest_.reset();
        if (welcomed_) {
            state_ = ConnectionState::ConnectionLost;
            view_.response.reset();
            view_.cardSelection.reset();
            view_.harvest.reset();
            view_.judgment.reset();
            view_.fireAttack.reset();
            view_.timeoutKind = TimeoutKind::None;
            view_.timeoutPlayerId = 0;
            view_.timeoutRemainingMs = 0;
            lobbyState_.gameStarted = false;
            timeoutReceipt_.invalidate();
            status_ = "与房主的连接已断开。";
        } else if (state_ != ConnectionState::Error) {
            state_ = ConnectionState::Disconnected;
        }
        emit viewChanged();
        emit lobbyChanged();
        emit statusChanged();
    });
}

NetworkGameClient::~NetworkGameClient()
{
    // socket_ is destroyed after several ViewState members.  Disconnect its
    // callbacks first so a final Qt disconnection notification cannot enter a
    // lambda that captures this while those members are already gone.
    QObject::disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
}

void NetworkGameClient::connectToHost()
{
    codec_.reset();
    pendingRequest_.reset();
    state_ = ConnectionState::Connecting;
    status_ = "正在连接 " + host_.toStdString() + ":" + std::to_string(port_) + "……";
    const QHostAddress address(host_.trimmed());
    if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isNull()) {
        state_ = ConnectionState::Error;
        status_ = "连接失败：请输入有效的 IPv4 地址。";
        emit statusChanged();
        return;
    }
    socket_.connectToHost(address, port_);
    emit statusChanged();
}

void NetworkGameClient::reconnectToHost() { reconnecting_ = true; connectToHost(); }

void NetworkGameClient::disconnectForReconnect()
{
    pendingRequest_.reset(); welcomed_ = false; timeoutReceipt_.invalidate(); socket_.disconnectFromHost();
}

void NetworkGameClient::disconnectFromHost()
{
    pendingRequest_.reset();
    lobbyState_ = {};
    view_ = PlayerViewState {view_.selfPlayerId, 0, 0, Phase::Start};
    nextRequest_ = 1;
    welcomed_ = false;
    reconnecting_ = false;
    reconnectToken_.clear();
    timeoutReceipt_.invalidate();
    socket_.disconnectFromHost();
    emit lobbyChanged();
    emit viewChanged();
}

const PlayerViewState& NetworkGameClient::view() const noexcept { return view_; }
PlayerId NetworkGameClient::selfPlayerId() const noexcept { return view_.selfPlayerId; }
void NetworkGameClient::setDeserializedViewObserverForTesting(std::function<void(const PlayerViewState&)> observer) { deserializedViewObserverForTesting_ = std::move(observer); }
void NetworkGameClient::setAppliedViewObserverForTesting(std::function<void(const PlayerViewState&, bool)> observer) { appliedViewObserverForTesting_ = std::move(observer); }

ActionResult NetworkGameClient::submit(const GameAction& action)
{
    if (!connected() || pendingRequest_) return {false, "", ActionResult::Error::InvalidResponse};
    const auto request = nextRequest_++;
    pendingRequest_ = request;
    send(QJsonObject{{"type", "action"}, {"requestId", qint64(request)}, {"payload", encodeAction(action)}});
    emit statusChanged();
    return {true, ""};
}

ActionResult NetworkGameClient::submitCardSelection(std::uint64_t requestId, SelectionOptionId optionId)
{
    return submitCardSelection(requestId, std::vector<SelectionOptionId> {optionId});
}

ActionResult NetworkGameClient::submitCardSelection(std::uint64_t requestId, const std::vector<SelectionOptionId>& optionIds)
{
    if (!connected() || pendingRequest_) return {false, "", ActionResult::Error::InvalidResponse};
    const auto request = nextRequest_++;
    pendingRequest_ = request;
    send(QJsonObject{{"type", "action"}, {"requestId", qint64(request)}, {"payload", encodeAction(SelectCardsAction{selfPlayerId(), requestId, {}}, optionIds)}});
    emit statusChanged();
    return {true, ""};
}

bool NetworkGameClient::canPlayCard(const CardId& id) const
{
    return connected() && !pendingRequest_ && view_.currentPhase == Phase::Play && view_.currentTurnPlayer == selfPlayerId()
        && std::any_of(view_.ownHand.begin(), view_.ownHand.end(), [&](const auto& c) { return c.id == id && c.type != CardType::Dodge; });
}
bool NetworkGameClient::canPlayCardOnTarget(const CardId& id, PlayerId target) const
{
    return canPlayCard(id) && target != selfPlayerId()
        && std::any_of(view_.players.begin(), view_.players.end(), [&](const auto& p) { return p.id == target && p.alive; });
}
int NetworkGameClient::requiredDiscardCount() const
{
    const auto it = std::find_if(view_.players.begin(), view_.players.end(), [&](const auto& p) { return p.id == selfPlayerId(); });
    return it == view_.players.end() ? 0 : std::max(0, int(view_.ownHand.size()) - it->hp);
}
bool NetworkGameClient::connected() const noexcept { return state_ == ConnectionState::InGame && socket_.state() == QAbstractSocket::ConnectedState; }
bool NetworkGameClient::actionPending() const noexcept { return pendingRequest_.has_value(); }
std::string NetworkGameClient::connectionStatus() const { return status_; }
void NetworkGameClient::refresh()
{
    if (view_.timeoutKind == TimeoutKind::None || !timeoutReceipt_.isValid()) return;
    view_.timeoutRemainingMs = std::max(0, timeoutReceiptRemainingMs_ - int(timeoutReceipt_.elapsed()));
}
void NetworkGameClient::send(const QJsonObject& message) { socket_.write(MessageCodec::frame(message)); }


void NetworkGameClient::onReadyRead()
{
    std::vector<QJsonObject> messages;
    QString error;
    if (!codec_.append(socket_.readAll(), messages, error)) {
        state_ = ConnectionState::Error;
        status_ = error.toStdString();
        socket_.disconnectFromHost();
        emit statusChanged();
        return;
    }
    for (const auto& message : messages) {
        const auto type = message["type"].toString();
        if (type == "welcome" && message["protocolVersion"].toInt() == ProtocolVersion) {
            welcomed_ = true;
            state_ = ConnectionState::Connected;
            view_.selfPlayerId = message["player"].toInt();
            reconnectToken_ = message["reconnectToken"].toString();
            reconnecting_ = false;
            status_ = "已分配为玩家 " + std::to_string(view_.selfPlayerId) + "。";
            emit statusChanged();
        } else if (type == "lobby_state") {
            const auto encodedPlayers = message.value("players");
            if (!encodedPlayers.isArray()) {
                status_ = "收到的大厅玩家列表无效。";
                emit statusChanged();
                continue;
            }
            std::vector<LobbyPlayerState> players;
            bool validPlayers = true;
            for (const auto& value : encodedPlayers.toArray()) {
                if (!value.isObject()) { validPlayers = false; break; }
                const auto player = value.toObject();
                if (!player.value("player").isDouble() || !player.value("seat").isDouble()
                    || !player.value("name").isString() || !player.value("connected").isBool()
                    || !player.value("host").isBool() || !player.value("ai").isBool()
                    || player.value("player").toInt() <= 0 || player.value("seat").toInt() < 0) { validPlayers = false; break; }
                players.push_back({player.value("player").toInt(), player.value("seat").toInt(), player.value("name").toString(),
                    player.value("connected").toBool(), player.value("host").toBool(), player.value("ai").toBool()});
            }
            if (!validPlayers) {
                status_ = "收到的大厅玩家信息无效。";
                emit statusChanged();
                continue;
            }
            const bool wasInGame = lobbyState_.gameStarted;
            lobbyState_.targetPlayerCount = message["targetPlayerCount"].toInt(2);
            lobbyState_.connectedHumanCount = message["connectedHumanCount"].toInt(1);
            lobbyState_.aiPlayerCount = message["aiPlayerCount"].toInt(1);
            const int mode = message["gameMode"].toInt(-1);
            if (mode != int(GameMode::FreeForAll) && mode != int(GameMode::Identity)) { status_ = "收到的游戏模式无效。"; emit statusChanged(); continue; }
            lobbyState_.gameMode = GameMode(mode);
            lobbyState_.gameStarted = message["gameStarted"].toBool();
            lobbyState_.players = std::move(players);
            if (!lobbyState_.gameStarted) status_ = "你的座位：玩家 " + std::to_string(view_.selfPlayerId)
                + "。等待房主开始游戏（真人 " + std::to_string(lobbyState_.connectedHumanCount)
                + " / 总人数 " + std::to_string(lobbyState_.targetPlayerCount) + "）。";
            if (wasInGame && !lobbyState_.gameStarted) {
                view_ = PlayerViewState {view_.selfPlayerId, 0, 0, Phase::Start};
                pendingRequest_.reset();
                timeoutReceipt_.invalidate();
                emit viewChanged();
            }
            emit lobbyChanged();
            emit statusChanged();
        } else if (type == "action_result") {
            std::uint64_t request = 0;
            ActionResult result;
            if (decodeActionResult(message["payload"].toObject(), request, result) && pendingRequest_ && request == *pendingRequest_) {
                pendingRequest_.reset();
                if (!result.accepted) status_ = "操作被服务器拒绝。";
                emit statusChanged();
            }
        } else if (type == "view_state") {
            const auto payload = message["payload"].toObject();
            if (auto view = decodeViewState(payload)) {
                if (deserializedViewObserverForTesting_) deserializedViewObserverForTesting_(*view);
                view->timeoutKind = TimeoutKind(payload["timeoutKind"].toInt(int(TimeoutKind::None)));
                view->timeoutPlayerId = payload["timeoutPlayer"].toInt();
                view->timeoutRemainingMs = payload["timeoutRemainingMs"].toInt();
                view_ = std::move(*view);
                if (appliedViewObserverForTesting_) appliedViewObserverForTesting_(view_, actionPending());
                timeoutReceiptRemainingMs_ = view_.timeoutRemainingMs;
                if (view_.timeoutKind == TimeoutKind::None) timeoutReceipt_.invalidate(); else timeoutReceipt_.restart();
                state_ = ConnectionState::InGame;
                emit viewChanged();
            }
        } else if (type == "error") {
            state_ = ConnectionState::Error;
            status_ = message["message"].toString().toStdString();
            emit statusChanged();
        }
    }
}

} // namespace sanguosha::network
