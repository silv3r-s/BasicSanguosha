#pragma once

#include <vector>

#include "session/PlayerViewState.h"

namespace sanguosha::ai {

enum class AIStrategyPhase { EarlyGame, MidGame, LateGame, Endgame };
enum class AIStrategicIntent {
    Neutral,
    ProtectLord,
    EliminateLikelyRebel,
    PressureLord,
    PreserveSelf,
    BalanceTable,
    FinishLord
};

struct AIIdentityTargetModifier final {
    PlayerId playerId {0};
    int general {0};
    int damage {0};
    int resourceDenial {0};
    int delayedTrick {0};
};

struct AIIdentityStrategyResult final {
    bool enabled {false};
    AIStrategyPhase phase {AIStrategyPhase::EarlyGame};
    AIStrategicIntent intent {AIStrategicIntent::Neutral};
    int tableBalanceScore {0}; // positive: inferred Rebel side is stronger
    int riskTolerance {0};
    int aggressionLevel {0};
    int selfPreservation {0};
    int rescueLordModifier {0};
    int nullificationLordModifier {0};
    int aoeModifier {0};
    int peachGardenModifier {0};
    int harvestModifier {0};
    std::vector<AIIdentityTargetModifier> targets;
};

// Stateless role strategy. Every result is rebuilt from the current filtered
// view and Stage 13.5D inference output, so reconnect and a second game cannot
// retain stale tactical scores.
class AIIdentityStrategy final {
public:
    [[nodiscard]] static AIIdentityStrategyResult evaluate(const PlayerViewState& view) noexcept;
    [[nodiscard]] static int targetModifier(const AIIdentityStrategyResult& result, PlayerId target,
                                            CardType cardType) noexcept;
    [[nodiscard]] static int cardUseModifier(const PlayerViewState& view,
                                             const AIIdentityStrategyResult& result,
                                             CardType cardType) noexcept;
    [[nodiscard]] static int cardKeepModifier(const PlayerViewState& view,
                                              const AIIdentityStrategyResult& result,
                                              CardType cardType) noexcept;
    [[nodiscard]] static bool shouldRescue(const PlayerViewState& view,
                                           const AIIdentityStrategyResult& result,
                                           PlayerId target) noexcept;
    [[nodiscard]] static bool shouldNullify(const PlayerViewState& view,
                                            const AIIdentityStrategyResult& result) noexcept;
};

} // namespace sanguosha::ai
