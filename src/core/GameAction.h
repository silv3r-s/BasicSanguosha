#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <variant>

#include "cards/Card.h"
#include "core/Player.h"

namespace sanguosha {

struct EndPlayPhaseAction {
    PlayerId playerId;
};

struct PlayCardAction {
    PlayerId playerId;
    CardId cardId;
    std::vector<PlayerId> targetIds;
};

struct PlayVirtualSlashAction {
    PlayerId playerId;
    std::vector<CardId> subcardIds;
    std::vector<PlayerId> targetIds;
};

struct RespondAction {
    PlayerId playerId;
    std::uint64_t requestId;
    std::optional<CardId> cardId;
};

struct RespondVirtualSlashAction {
    PlayerId playerId;
    std::uint64_t requestId;
    std::vector<CardId> subcardIds;
};

struct SelectCardsAction {
    PlayerId playerId;
    std::uint64_t requestId;
    std::vector<CardId> cardIds;
};

struct DiscardAction {
    PlayerId playerId;
    std::vector<CardId> cardIds;
};

struct ActionResult {
    bool accepted {false};
    std::string message;
    enum class Error {
        None,
        UnauthorizedPlayer,
        NotCurrentPlayer,
        WrongPhase,
        InvalidCard,
        InvalidTarget,
        InvalidResponse,
        RequestMismatch,
        GameOver
    } error {Error::None};
};

using GameAction = std::variant<EndPlayPhaseAction, DiscardAction, PlayCardAction,
                                RespondAction, SelectCardsAction, PlayVirtualSlashAction, RespondVirtualSlashAction>;

} // namespace sanguosha
