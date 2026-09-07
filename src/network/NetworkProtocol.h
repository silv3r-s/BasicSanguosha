#pragma once

#include <QByteArray>
#include <QJsonObject>

#include <optional>
#include <vector>

#include "core/GameAction.h"
#include "session/PlayerViewState.h"

namespace sanguosha::network {

constexpr int ProtocolVersion = 2;
constexpr quint16 DefaultPort = 28765;
constexpr quint32 MaxMessageSize = 1024 * 1024;

class MessageCodec {
public:
    static QByteArray frame(const QJsonObject& object);
    bool append(const QByteArray& data, std::vector<QJsonObject>& messages, QString& error);
    void reset() noexcept { buffer_.clear(); }
private:
    QByteArray buffer_;
};

struct DecodedAction {
    GameAction action;
    std::optional<std::vector<SelectionOptionId>> selectionOptionIds;
};

QJsonObject encodeViewState(const PlayerViewState& view);
std::optional<PlayerViewState> decodeViewState(const QJsonObject& object);
QJsonObject encodeAction(const GameAction& action, std::optional<std::vector<SelectionOptionId>> selectionOptionIds = {});
std::optional<DecodedAction> decodeAction(const QJsonObject& object);
QJsonObject encodeActionResult(std::uint64_t requestId, const ActionResult& result);
bool decodeActionResult(const QJsonObject& object, std::uint64_t& requestId, ActionResult& result);

} // namespace sanguosha::network
