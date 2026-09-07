#include "client/GameClientController.h"

namespace sanguosha {

GameClientController::GameClientController(GameSession& session, PlayerId playerId)
    : session_(session), clientId_(session_.addClient(playerId)), view_(session_.viewFor(clientId_)) {}

const PlayerViewState& GameClientController::view() const noexcept { return view_; }
PlayerId GameClientController::selfPlayerId() const noexcept { return view_.selfPlayerId; }
ActionResult GameClientController::submit(const GameAction& action) { const auto result = session_.submitAction(clientId_, action); refresh(); return result; }
ActionResult GameClientController::submitCardSelection(std::uint64_t requestId, SelectionOptionId optionId) { const auto result = session_.submitCardSelection(clientId_, requestId, optionId); refresh(); return result; }
ActionResult GameClientController::submitCardSelection(std::uint64_t requestId, const std::vector<SelectionOptionId>& optionIds) { const auto result = session_.submitCardSelection(clientId_, requestId, optionIds); refresh(); return result; }
bool GameClientController::canPlayCard(const CardId& cardId) const { return session_.canPlayCard(clientId_, cardId); }
bool GameClientController::canPlayCardOnTarget(const CardId& cardId, PlayerId targetId) const { return session_.canPlayCardOnTarget(clientId_, cardId, targetId); }
int GameClientController::requiredDiscardCount() const { return session_.requiredDiscardCount(clientId_); }
bool GameClientController::connected() const noexcept { return connected_; }
std::string GameClientController::connectionStatus() const { return connectionStatus_; }
void GameClientController::setConnectionStatus(std::string status, bool connected) { connectionStatus_ = std::move(status); connected_ = connected; }
void GameClientController::restart() { session_.restart(clientId_); refresh(); }
void GameClientController::refresh() { view_ = session_.viewFor(clientId_); }

} // namespace sanguosha
