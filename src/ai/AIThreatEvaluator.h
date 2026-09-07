#pragma once

#include "session/PlayerViewState.h"

namespace sanguosha::ai {

enum class AIPressureIntent { Attack, Duel, Dismantlement, Snatch, DelayedTrick };

// Every field is derived from PlayerViewState. No engine pointer, private hand,
// hidden identity, future deck, or opaque option identity can enter this layer.
struct AIThreatBreakdown final {
    int health {0};
    int hand {0};
    int equipment {0};
    int reach {0};
    int burst {0};
    int restriction {0};

    [[nodiscard]] int total() const noexcept
    {
        return health + hand + equipment + reach + burst + restriction;
    }
};

struct AITargetEvaluation final {
    int threat {0};
    int killOpportunity {0};
    int defense {0};
    int equipmentThreat {0};
    int resourceValue {0};
};

class AIThreatEvaluator final {
public:
    [[nodiscard]] static AIThreatBreakdown evaluateThreat(const PlayerViewState& view,
                                                          const PublicPlayerView& target) noexcept;
    [[nodiscard]] static AITargetEvaluation evaluate(const PlayerViewState& view, const PublicPlayerView& target,
                                                     bool hasLegalAttack = false, int slashFamilyCount = 0) noexcept;
    [[nodiscard]] static int threatScore(const PlayerViewState& view, const PublicPlayerView& target) noexcept;
    [[nodiscard]] static int threatScore(const PublicPlayerView& target) noexcept;
    [[nodiscard]] static int equipmentThreat(const PlayerViewState& view, const PublicPlayerView& target) noexcept;
    [[nodiscard]] static int defenseScore(const PublicPlayerView& target) noexcept;
    [[nodiscard]] static int killOpportunityScore(const PlayerViewState& view, const PublicPlayerView& target,
                                                  bool hasLegalAttack, int slashFamilyCount) noexcept;
    [[nodiscard]] static int resourceValue(const PublicPlayerView& target) noexcept;
    [[nodiscard]] static int pressurePriority(const PlayerViewState& view, const PublicPlayerView& target,
                                              AIPressureIntent intent, int tacticalScore,
                                              bool hasLegalAttack = false, int slashFamilyCount = 0) noexcept;
    [[nodiscard]] static int publicDistance(const PlayerViewState& view, PlayerId targetId) noexcept;

    // Higher score wins; equal public states resolve by stable seat then ID.
    [[nodiscard]] static bool isHigherThreat(const PlayerViewState& view, const PublicPlayerView& left,
                                             const PublicPlayerView& right) noexcept;
    [[nodiscard]] static bool isHigherThreat(const PublicPlayerView& left, const PublicPlayerView& right) noexcept;
};

} // namespace sanguosha::ai
