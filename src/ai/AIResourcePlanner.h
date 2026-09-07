#pragma once

#include <optional>
#include <vector>

#include "ai/AIThreatEvaluator.h"

namespace sanguosha::ai {

struct AICardResourceValue final {
    int immediateUse {0};
    int keep {0};
    int emergency {0};
    int reserve {0};
};

class AIResourcePlanner final {
public:
    static constexpr int LowHpThreshold = 2;
    static constexpr int EmergencyResourceThreshold = 120;
    static constexpr int ReserveNullificationThreshold = 2;

    [[nodiscard]] static AICardResourceValue evaluate(const PlayerViewState& view, const CardView& card,
                                                      const PublicPlayerView* target = nullptr) noexcept;
    [[nodiscard]] static int keepValue(const PlayerViewState& view, const CardView& card) noexcept;
    [[nodiscard]] static int immediateUseValue(const PlayerViewState& view, const CardView& card,
                                               const PublicPlayerView* target = nullptr) noexcept;
    [[nodiscard]] static int emergencyValue(const PlayerViewState& view, const CardView& card) noexcept;
    [[nodiscard]] static int responseCost(const PlayerViewState& view, const CardView& card) noexcept;
    [[nodiscard]] static int equipmentReplacementValue(const PlayerViewState& view, const CardView& card) noexcept;
    [[nodiscard]] static bool shouldUseNullification(const PlayerViewState& view) noexcept;
    [[nodiscard]] static bool shouldUseWine(const PlayerViewState& view, int bestKillOpportunity) noexcept;
    [[nodiscard]] static std::vector<CardId> serpentSpearMaterials(const PlayerViewState& view,
                                                                  bool emergencyResponse = false);
    [[nodiscard]] static std::vector<SelectionOptionId> chooseSelection(const PlayerViewState& view,
                                                                       const CardSelectionView& selection);
};

} // namespace sanguosha::ai
