#pragma once

#include <optional>
#include <string>

#include "core/Player.h"

namespace sanguosha {

enum class GameEventType {
    GameStarted, TurnStarted, PhaseStarted, PhaseEnded, CardUsed, CardResponded,
    TargetSpecified, DamageCaused, DamageReceived, RecoverStarted, HpRecovered, HpChanged, PlayerDying,
    PlayerDied, TurnEnded, GameEnded, InteractionRequested, PublicCardMoved, NullificationContext
};

struct GameEvent {
    GameEventType type;
    std::optional<PlayerId> source;
    std::optional<PlayerId> target;
    std::string detail;
};

} // namespace sanguosha
