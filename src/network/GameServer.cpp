#include "network/GameServer.h"

#include "ai/AIController.h"

#include <algorithm>
#include <QHostAddress>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QUuid>

namespace sanguosha::network {

GameServer::GameServer(const std::vector<std::string>& playerNames, QObject* parent)
    : QObject(parent), targetPlayerCount_(std::clamp(int(playerNames.size()), 2, 8)), session_(playerNames, false)
{
    connect(&server_, &QTcpServer::newConnection, this, &GameServer::onConnection);
    timeoutTimer_.setInterval(20); connect(&timeoutTimer_, &QTimer::timeout, this, &GameServer::onTimeoutTick); timeoutTimer_.start();
    session_.subscribeToGameEvents([this](const GameEvent& event) {
        Q_UNUSED(event);
        if (gameStarted_) { sendViews(); scheduleAIActions(); }
    });
    session_.subscribeToActions([this] { scheduleAIActions(); });
}

GameServer::~GameServer()
{
    // Close sockets while session_ is still alive. QTcpServer destroys its child
    // sockets after later-declared members (including session_) are gone.
    close();
}

bool GameServer::listen(QString& error, quint16 port)
{
    if (server_.isListening()) {
        error = QStringLiteral("主机已经启动，正在监听端口 %1。").arg(server_.serverPort());
        return false;
    }
    if (server_.listen(QHostAddress::AnyIPv4, port)) return true;
    if (server_.serverError() == QAbstractSocket::AddressInUseError) {
        error = QStringLiteral("无法启动主机：端口 %1 可能已被占用。%2").arg(port).arg(server_.errorString());
    } else {
        error = QStringLiteral("无法启动主机：无法监听端口 %1。%2").arg(port).arg(server_.errorString());
    }
    return false;
}

void GameServer::close()
{
    clearAIControllers();
    timeoutTimer_.stop();
    server_.close();
    std::vector<QTcpSocket*> sockets;
    sockets.reserve(connections_.size());
    for (const auto& entry : connections_) if (entry.second.socket) sockets.push_back(entry.second.socket);
    for (auto* socket : sockets) {
        // QTcpServer owns incoming sockets. Destroy them now, while this
        // server and its session are still valid, rather than leaving their
        // disconnected() notifications for member destruction.
        QObject::disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->setParent(nullptr);
        delete socket;
    }
    connections_.clear();
    reconnectRecords_.clear();
}

void GameServer::setHostDisplayName(QString name)
{
    hostDisplayName_ = name.trimmed();
    if (hostDisplayName_.isEmpty()) hostDisplayName_ = QStringLiteral("玩家1");
}

GameSession& GameServer::session() noexcept { return session_; }
int GameServer::targetPlayerCount() const noexcept { return targetPlayerCount_; }
int GameServer::connectedHumanCount() const noexcept
{
    return 1 + int(std::count_if(connections_.begin(), connections_.end(), [](const auto& entry) { return entry.second.joined; }));
}
int GameServer::aiPlayerCount() const noexcept { return std::max(0, targetPlayerCount_ - connectedHumanCount()); }
bool GameServer::setTargetPlayerCount(int count)
{
    if (gameStarted_ || count < 2 || count > 8 || count < connectedHumanCount()) return false;
    targetPlayerCount_ = count;
    std::vector<std::string> lobbyNames;
    lobbyNames.reserve(static_cast<std::size_t>(targetPlayerCount_));
    for (int player = 1; player <= targetPlayerCount_; ++player) lobbyNames.push_back("Player " + std::to_string(player));
    session_.configureGame(lobbyNames, {}, {}, gameModeForPlayerCount(targetPlayerCount_));
    emit stateChanged();
    return true;
}
bool GameServer::addAI() { return setTargetPlayerCount(targetPlayerCount_ + 1); }
bool GameServer::removeAI() { return aiPlayerCount() > 0 && setTargetPlayerCount(targetPlayerCount_ - 1); }
bool GameServer::gameStarted() const noexcept { return gameStarted_; }
LobbyState GameServer::lobbyState() const
{
    LobbyState state {targetPlayerCount_, connectedHumanCount(), aiPlayerCount(), gameModeForPlayerCount(targetPlayerCount_), gameStarted_, {}};
    state.players.push_back({1, 0, hostDisplayName_, true, true, false});
    for (const auto& record : reconnectRecords_) state.players.push_back({record.second.playerId, record.second.playerId - 1, record.second.displayName, record.second.connected, false, false});
    for (PlayerId player = 2; player <= targetPlayerCount_; ++player) {
        const bool occupied = std::any_of(state.players.begin(), state.players.end(), [player](const auto& entry) { return entry.playerId == player; });
        if (!occupied) state.players.push_back({player, player - 1, QStringLiteral("AI-%1").arg(player - 1), true, false, true});
    }
    std::sort(state.players.begin(), state.players.end(), [](const auto& left, const auto& right) { return left.seat < right.seat; }); return state;
}
bool GameServer::remoteConnected() const noexcept { return std::any_of(connections_.begin(), connections_.end(), [](const auto& entry) { return entry.second.joined; }); }
quint16 GameServer::serverPort() const noexcept { return server_.serverPort(); }
QHostAddress GameServer::serverAddress() const { return server_.serverAddress(); }
QStringList GameServer::discoverLanIpv4Addresses()
{
    QStringList privateAddresses;
    QStringList otherAddresses;
    const auto isPrivate = [](quint32 value) {
        return (value & 0xff000000U) == 0x0a000000U
            || (value & 0xfff00000U) == 0xac100000U
            || (value & 0xffff0000U) == 0xc0a80000U;
    };
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || !(interface.flags() & QNetworkInterface::IsRunning)
            || (interface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const auto& entry : interface.addressEntries()) {
            const auto address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback() || address.isNull()) continue;
            const auto text = address.toString();
            auto& destination = isPrivate(address.toIPv4Address()) ? privateAddresses : otherAddresses;
            if (!destination.contains(text)) destination.push_back(text);
        }
    }
    privateAddresses.append(otherAddresses);
    return privateAddresses;
}
void GameServer::broadcastViews() { sendViews(); }
void GameServer::setInteractionTimeoutForTesting(int milliseconds) { interactionTimeoutMs_ = milliseconds; timeoutContext_.clear(); refreshTimeoutContext(); }
void GameServer::setReconnectGracePeriodForTesting(int milliseconds) { reconnectGracePeriodMs_ = std::max(0, milliseconds); }
#ifdef SANGUOSHA_TESTING
void GameServer::testingSetIdentityAssignments(const std::vector<PlayerIdentity>& identities) { testingIdentities_ = identities; }
int GameServer::testingReconnectReservationCount() const noexcept
{
    return int(std::count_if(reconnectRecords_.begin(), reconnectRecords_.end(), [](const auto& record) { return !record.second.connected; }));
}
#endif
QJsonObject GameServer::viewPayloadFor(SessionClientId clientId) const
{
    const auto view = session_.viewFor(clientId);
    auto payload = encodeViewState(view);
    payload["timeoutKind"] = int(view.timeoutKind);
    payload["timeoutPlayer"] = view.timeoutPlayerId;
    payload["timeoutRemainingMs"] = view.timeoutRemainingMs;
    return payload;
}

bool GameServer::startLobbyGame()
{
    if (gameStarted_) return false;
    std::vector<std::string> names;
    std::vector<PlayerControlType> controls(static_cast<std::size_t>(targetPlayerCount_), PlayerControlType::AI);
    names.reserve(static_cast<std::size_t>(targetPlayerCount_));
    controls.front() = PlayerControlType::Human;
    for (int player = 1; player <= targetPlayerCount_; ++player) names.push_back(player == 1 ? "Player 1" : "AI-" + std::to_string(player - 1));
    names.front() = hostDisplayName_.toStdString();
    for (const auto& entry : connections_) if (entry.second.joined && entry.second.playerId && !entry.second.displayName.isEmpty())
        names.at(static_cast<std::size_t>(*entry.second.playerId - 1)) = entry.second.displayName.toStdString();
    for (const auto& entry : connections_) if (entry.second.joined && entry.second.playerId) controls.at(static_cast<std::size_t>(*entry.second.playerId - 1)) = PlayerControlType::Human;
    const auto mode = gameModeForPlayerCount(targetPlayerCount_);
#ifdef SANGUOSHA_TESTING
    const std::vector<PlayerIdentity> identities = mode == GameMode::Identity ? testingIdentities_ : std::vector<PlayerIdentity> {};
#else
    const std::vector<PlayerIdentity> identities;
#endif
    session_.configureGame(names, identities, controls, mode);
    clearAIControllers();
    for (auto& entry : connections_) if (entry.second.joined && entry.second.playerId && !entry.second.sessionClientId) entry.second.sessionClientId = session_.addClient(*entry.second.playerId);
    session_.startGame();
    gameStarted_ = true;
    createAIControllers();
    refreshTimeoutContext();
    broadcastLobbyState();
    sendViews();
    emit stateChanged();
    scheduleAIActions();
    return true;
}

bool GameServer::returnToLobby()
{
    if (!gameStarted_) return false;
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(targetPlayerCount_));
    clearAIControllers();
    for (int player = 1; player <= targetPlayerCount_; ++player) names.push_back(player == 1 ? "Player 1" : "AI-" + std::to_string(player - 1));
    // createGame resets deck, hands, HP, equipment, identities, interactions, request IDs and logs.
    // The client-to-seat map remains intact, so connected humans do not need to reconnect.
    session_.configureGame(names, {}, {}, gameModeForPlayerCount(targetPlayerCount_));
    gameStarted_ = false;
    timeoutContext_.clear();
    timeoutElapsed_.invalidate();
    session_.setTimeoutView(TimeoutKind::None, 0, 0);
    broadcastLobbyState();
    emit connectionStatusChanged(QStringLiteral("当前对局已结束，已返回大厅。"), true);
    emit stateChanged();
    return true;
}

std::optional<PlayerId> GameServer::allocateSeat() const
{
    for (PlayerId candidate = 2; candidate <= targetPlayerCount_; ++candidate) {
    const bool occupied = std::any_of(connections_.begin(), connections_.end(), [candidate](const auto& entry) {
            return entry.second.joined && entry.second.playerId && *entry.second.playerId == candidate;
        });
        const bool reserved = std::any_of(reconnectRecords_.begin(), reconnectRecords_.end(), [candidate](const auto& entry) { return !entry.second.connected && entry.second.playerId == candidate; });
        if (!occupied && !reserved) return candidate;
    }
    return std::nullopt;
}

void GameServer::onConnection()
{
    while (server_.hasPendingConnections()) {
        auto* incoming = server_.nextPendingConnection();
        connections_.emplace(incoming, ConnectionContext {incoming});
        connect(incoming, &QTcpSocket::readyRead, this, [this, incoming] { onReadyRead(incoming); });
        connect(incoming, &QTcpSocket::disconnected, this, [this, incoming] { onDisconnected(incoming); });
    }
}

void GameServer::onDisconnected(QTcpSocket* socket)
{
    const auto found = connections_.find(socket);
    if (found == connections_.end()) return;
    const auto player = found->second.playerId.value_or(0);
    const bool wasLobbyMember = found->second.joined && !gameStarted_;
    const auto sessionClientId = found->second.sessionClientId;
    if (found->second.joined && !found->second.reconnectToken.isEmpty()) {
        auto record = reconnectRecords_.find(found->second.reconnectToken.toStdString());
        if (record != reconnectRecords_.end()) { record->second.connected = false; record->second.disconnectedAt.restart(); }
    }
    if (gameStarted_ && player) session_.handlePlayerDisconnect(player, false);
    if (gameStarted_ && found->second.playerId) session_.appendPublicLog(found->second.displayName.toStdString() + " disconnected; waiting to reconnect.");
    if (gameStarted_ && sessionClientId) session_.disconnectClient(*sessionClientId);
    connections_.erase(found);
    if (wasLobbyMember) broadcastLobbyState();
    else if (gameStarted_) sendViews();
    emit connectionStatusChanged(player ? QStringLiteral("玩家 %1 已断开连接。").arg(player) : QStringLiteral("连接已断开。"), false);
    emit stateChanged();
}

void GameServer::onReadyRead(QTcpSocket* socket)
{
    const auto contextIt = connections_.find(socket);
    if (contextIt == connections_.end()) return;
    auto& context = contextIt->second;
    std::vector<QJsonObject> messages;
    QString error;
    if (!context.codec.append(socket->readAll(), messages, error)) {
        reject(socket, error);
        socket->disconnectFromHost();
        return;
    }
    for (const auto& message : messages) {
        const auto type = message["type"].toString();
        if (!context.helloReceived) {
            if (type != "hello" || !message.contains("protocolVersion") || message["protocolVersion"].toInt() != ProtocolVersion) {
                reject(socket, QStringLiteral("协议版本不匹配。"));
                socket->disconnectFromHost();
                return;
            }
            const auto requestedToken = message["reconnectToken"].toString();
            if (!requestedToken.isEmpty()) {
                const auto record = reconnectRecords_.find(requestedToken.toStdString());
                if (record == reconnectRecords_.end() || record->second.connected) { reject(socket, QStringLiteral("重连令牌无效。")); socket->disconnectFromHost(); return; }
                context.helloReceived = context.joined = true; context.playerId = record->second.playerId; context.displayName = record->second.displayName; context.reconnectToken = requestedToken;
                context.sessionClientId = session_.addClient(*context.playerId); record->second.connected = true;
                if (gameStarted_) session_.setPlayerConnected(*context.playerId, true);
                if (gameStarted_) session_.appendPublicLog(context.displayName.toStdString() + " reconnected.");
                sendTo(socket, QJsonObject{{"type", "welcome"}, {"protocolVersion", ProtocolVersion}, {"player", *context.playerId}, {"reconnectToken", requestedToken}});
                broadcastLobbyState();
                if (gameStarted_) { refreshTimeoutContext(); sendViews(); }
                emit stateChanged(); continue;
            }
            if (gameStarted_) { reject(socket, QStringLiteral("游戏已经开始，无法加入。")); socket->disconnectFromHost(); return; }
            const auto seat = allocateSeat();
            if (!seat) { reject(socket, QStringLiteral("大厅已满。")); socket->disconnectFromHost(); return; }
            context.helloReceived = true;
            context.joined = true;
            context.playerId = *seat;
            context.displayName = message["displayName"].toString().trimmed();
            if (context.displayName.isEmpty()) context.displayName = QStringLiteral("玩家 %1").arg(*seat);
            context.sessionClientId = session_.addClient(*seat);
            context.reconnectToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
            reconnectRecords_.emplace(context.reconnectToken.toStdString(), ReconnectRecord {*seat, context.displayName, true, {}});
            sendTo(socket, QJsonObject{{"type", "welcome"}, {"protocolVersion", ProtocolVersion}, {"player", *seat}, {"reconnectToken", context.reconnectToken}});
            emit connectionStatusChanged(QStringLiteral("玩家 %1 已进入大厅。").arg(*seat), true);
            broadcastLobbyState();
            emit stateChanged();
            continue;
        }
        if (type != "action" || !message.contains("requestId") || !message["requestId"].isDouble() || !message["payload"].isObject()) {
            reject(socket, QStringLiteral("未知或格式错误的消息。"));
            continue;
        }
        const auto request = std::uint64_t(message["requestId"].toInteger());
        if (!gameStarted_ || !context.sessionClientId) {
            sendTo(socket, QJsonObject{{"type", "action_result"}, {"payload", encodeActionResult(request, {false, "游戏尚未开始。", ActionResult::Error::InvalidResponse})}});
            continue;
        }
        const auto decoded = decodeAction(message["payload"].toObject());
        if (!decoded) {
            sendTo(socket, QJsonObject{{"type", "action_result"}, {"payload", encodeActionResult(request, {false, "", ActionResult::Error::InvalidCard})}});
            continue;
        }
        const ActionResult result = decoded->selectionOptionIds
            ? session_.submitCardSelection(*context.sessionClientId, std::get<SelectCardsAction>(decoded->action).requestId, *decoded->selectionOptionIds)
            : session_.submitAction(*context.sessionClientId, decoded->action);
        sendTo(socket, QJsonObject{{"type", "action_result"}, {"payload", encodeActionResult(request, result)}});
        if (result.accepted) {
            refreshTimeoutContext();
            sendViews();
            emit stateChanged();
        }
    }
}

void GameServer::onTimeoutTick()
{
    expireReconnectReservations();
    refreshTimeoutContext();
    if (timeoutContext_.empty() || !timeoutElapsed_.isValid()) return;
    if (timeoutElapsed_.elapsed() < interactionTimeoutMs_) {
        const int remaining = std::max(0, interactionTimeoutMs_ - int(timeoutElapsed_.elapsed()));
        session_.updateTimeoutView(remaining);
        const int bucket = remaining / 200;
        if (bucket != lastTimeoutBroadcastBucket_) { lastTimeoutBroadcastBucket_ = bucket; sendViews(); }
        return;
    }
    timeoutContext_.clear();
    const auto result = session_.handleTimeout();
    if (result.accepted) { refreshTimeoutContext(); sendViews(); emit stateChanged(); }
}

void GameServer::expireReconnectReservations()
{
    bool changed = false;
    for (auto it = reconnectRecords_.begin(); it != reconnectRecords_.end();) {
        if (!it->second.connected && it->second.disconnectedAt.isValid() && it->second.disconnectedAt.elapsed() >= reconnectGracePeriodMs_) {
            if (gameStarted_) session_.handlePlayerDisconnect(it->second.playerId);
            it = reconnectRecords_.erase(it); changed = true;
        }
        else ++it;
    }
    if (changed) {
        refreshTimeoutContext();
        if (gameStarted_) sendViews();
        else broadcastLobbyState();
    }
}

void GameServer::refreshTimeoutContext()
{
    const auto key = session_.timeoutContextKey();
    if (key == timeoutContext_) {
        if (!key.empty() && timeoutElapsed_.isValid())
            session_.updateTimeoutView(std::max(0, interactionTimeoutMs_ - int(timeoutElapsed_.elapsed())));
        return;
    }
    timeoutContext_ = key;
    lastTimeoutBroadcastBucket_ = -1;
    if (timeoutContext_.empty()) { timeoutElapsed_.invalidate(); session_.setTimeoutView(TimeoutKind::None, 0, 0); }
    else {
        timeoutElapsed_.restart();
        session_.updateTimeoutView(interactionTimeoutMs_);
    }
}

void GameServer::sendTo(QTcpSocket* target, const QJsonObject& message)
{
    if (target && target->state() == QAbstractSocket::ConnectedState) target->write(MessageCodec::frame(message));
}
void GameServer::sendViews()
{
    if (!gameStarted_) return;
    for (const auto& entry : connections_) {
        const auto& context = entry.second;
        if (!context.joined || !context.sessionClientId || !context.playerId) continue;
        const auto payload = viewPayloadFor(*context.sessionClientId);
        emit viewPayloadPrepared(*context.playerId, payload);
        sendTo(context.socket, QJsonObject{{"type", "view_state"}, {"payload", payload}});
    }
}
void GameServer::createAIControllers()
{
    for (const auto playerId : session_.aiPlayerIds())
        aiControllers_.push_back(std::make_unique<ai::AIController>(session_, playerId, [this] { onAIAction(); }, std::make_unique<ai::BasicAIActionDecider>(), this));
}
void GameServer::clearAIControllers()
{
    for (auto& controller : aiControllers_) controller->stop();
    aiControllers_.clear();
    aiDriveScheduled_ = false;
    aiDriveDispatching_ = false;
    aiDriveAgain_ = false;
}
void GameServer::scheduleAIActions()
{
    if (!gameStarted_) return;
    if (aiDriveDispatching_) { aiDriveAgain_ = true; return; }
    if (aiDriveScheduled_) return;
    aiDriveScheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        aiDriveScheduled_ = false;
        if (!gameStarted_) return;
        aiDriveDispatching_ = true;
        for (auto& controller : aiControllers_) controller->schedule();
        aiDriveDispatching_ = false;
        if (aiDriveAgain_) {
            aiDriveAgain_ = false;
            scheduleAIActions();
        }
    });
}
void GameServer::onAIAction()
{
    if (!gameStarted_) return;
    refreshTimeoutContext();
    sendViews();
    emit stateChanged();
    scheduleAIActions();
}
void GameServer::broadcastLobbyState()
{
    QJsonObject payload {{"type", "lobby_state"}, {"targetPlayerCount", targetPlayerCount_},
        {"connectedHumanCount", connectedHumanCount()}, {"aiPlayerCount", aiPlayerCount()},
        {"gameMode", int(gameModeForPlayerCount(targetPlayerCount_))}, {"gameStarted", gameStarted_}};
    QJsonArray players; for (const auto& player : lobbyState().players) players.append(QJsonObject{{"player",player.playerId},{"seat",player.seat},{"name",player.nickname},{"connected",player.connected},{"host",player.host},{"ai",player.ai}}); payload["players"] = players;
    for (const auto& entry : connections_) if (entry.second.joined) sendTo(entry.second.socket, payload);
}
void GameServer::reject(QTcpSocket* socket, const QString& message) { sendTo(socket, QJsonObject{{"type", "error"}, {"message", message}}); }

} // namespace sanguosha::network
