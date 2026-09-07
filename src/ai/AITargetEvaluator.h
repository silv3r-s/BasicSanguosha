#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "session/PlayerViewState.h"

namespace sanguosha::ai {

struct AIAdvancedTargetEvaluation final {
    int tacticalValue {0};
    int threatValue {0};
    int killValue {0};
    int resourceDenialValue {0};
    int defenseBreakValue {0};
    int elementalValue {0};
    int chainValue {0};
    int knownIdentityModifier {0};
    int inferenceModifier {0};
    int identityStrategyModifier {0};
    int riskPenalty {0};

    [[nodiscard]] int finalScore() const noexcept;
};

struct AITargetSetEvaluation final {
    std::vector<PlayerId> targets;
    int individualValue {0};
    int synergyValue {0};
    int friendlyRisk {0};
    int resourceRisk {0};

    [[nodiscard]] int finalScore() const noexcept;
};

class AITargetEvaluator final {
public:
    using TargetLegality = std::function<bool(const CardId&, PlayerId)>;

    [[nodiscard]] static AIAdvancedTargetEvaluation evaluate(const PlayerViewState& view, const CardView& card,
                                                             const PublicPlayerView& target, bool legal,
                                                             int slashFamilyCount) noexcept;
    [[nodiscard]] static AITargetSetEvaluation bestTargetSet(const PlayerViewState& view, const CardView& card,
                                                             const TargetLegality& legalTarget);
    [[nodiscard]] static int aoeValue(const PlayerViewState& view, CardType type, int slashFamilyCount) noexcept;
    [[nodiscard]] static int globalCardValue(const PlayerViewState& view, CardType type) noexcept;
    [[nodiscard]] static int borrowedSwordPairScore(const PlayerViewState& view, const PublicPlayerView& attacker,
                                                    const PublicPlayerView& victim, int slashFamilyCount) noexcept;
    [[nodiscard]] static std::vector<SelectionOptionId> chooseTargetResourceSelection(
        const PlayerViewState& view, const CardSelectionView& selection);
};

} // namespace sanguosha::ai
