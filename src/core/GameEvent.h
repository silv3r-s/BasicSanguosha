#pragma once

#include <optional>
#include <cstdint>
#include <string>

#include "core/Player.h"

namespace sanguosha {

enum class GameEventType {
    GameStarted, TurnStarted, PhaseStarted, PhaseEnded, CardUsed, CardResponded,
    TargetSpecified, DamageCaused, DamageReceived, RecoverStarted, HpRecovered, HpChanged, PlayerDying,
    PlayerDied, TurnEnded, GameEnded, InteractionRequested, PublicCardMoved, NullificationContext
};

struct PublicEventCard {
    std::string displayName;
    CardType type {CardType::Slash};
    Suit suit {Suit::Spade};
    int rank {1};
};

struct GameEvent {
    GameEventType type;
    std::optional<PlayerId> source;
    std::optional<PlayerId> target;
    std::string detail;
    std::uint64_t eventId {0};
    std::optional<PublicEventCard> card;
};

} // namespace sanguosha
