#pragma once

#include "client/IGameClient.h"
#include "session/GameSession.h"

namespace sanguosha {

class GameClientController final : public IGameClient {
public:
    GameClientController(GameSession& session, PlayerId playerId);
    [[nodiscard]] const PlayerViewState& view() const noexcept override;
    [[nodiscard]] PlayerId selfPlayerId() const noexcept override;
    ActionResult submit(const GameAction& action) override;
    ActionResult submitCardSelection(std::uint64_t requestId, SelectionOptionId optionId) override;
    ActionResult submitCardSelection(std::uint64_t requestId, const std::vector<SelectionOptionId>& optionIds) override;
    [[nodiscard]] bool canPlayCard(const CardId& cardId) const override;
    [[nodiscard]] bool canPlayCardOnTarget(const CardId& cardId, PlayerId targetId) const override;
    [[nodiscard]] int requiredDiscardCount() const override;
    [[nodiscard]] bool connected() const noexcept override;
    [[nodiscard]] bool actionPending() const noexcept override { return false; }
    [[nodiscard]] std::string connectionStatus() const override;
    void setConnectionStatus(std::string status, bool connected);
    void restart() override;
    void refresh() override;

private:
    GameSession& session_;
    SessionClientId clientId_;
    PlayerViewState view_;
    std::string connectionStatus_ {"本地开发模式"};
    bool connected_ {true};
};

} // namespace sanguosha
