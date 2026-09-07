#pragma once

#include <string>
#include <vector>

#include "core/GameAction.h"
#include "session/PlayerViewState.h"

namespace sanguosha {

class IGameClient {
public:
    virtual ~IGameClient() = default;
    [[nodiscard]] virtual const PlayerViewState& view() const noexcept = 0;
    [[nodiscard]] virtual PlayerId selfPlayerId() const noexcept = 0;
    virtual ActionResult submit(const GameAction& action) = 0;
    virtual ActionResult submitCardSelection(std::uint64_t requestId, SelectionOptionId optionId) = 0;
    virtual ActionResult submitCardSelection(std::uint64_t requestId, const std::vector<SelectionOptionId>& optionIds) = 0;
    [[nodiscard]] virtual bool canPlayCard(const CardId& cardId) const = 0;
    [[nodiscard]] virtual bool canPlayCardOnTarget(const CardId& cardId, PlayerId targetId) const = 0;
    [[nodiscard]] virtual int requiredDiscardCount() const = 0;
    [[nodiscard]] virtual bool connected() const noexcept = 0;
    [[nodiscard]] virtual bool actionPending() const noexcept = 0;
    [[nodiscard]] virtual std::string connectionStatus() const = 0;
    virtual void restart() = 0;
    virtual void refresh() = 0;
};

} // namespace sanguosha
