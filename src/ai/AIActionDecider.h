#pragma once

#include <optional>
#include <functional>
#include <vector>

#include "core/GameAction.h"
#include "session/PlayerViewState.h"

namespace sanguosha::ai {

class IAIActionDecider {
public:
    virtual ~IAIActionDecider() = default;
    using TargetLegality = std::function<bool(const CardId&, PlayerId)>;
    using PlayLegality = std::function<bool(const CardId&)>;
    // The session supplies complete Borrowed Sword pairs.  This keeps range and
    // weapon legality in the authoritative engine rather than reconstructing it
    // from the public AI view.
    using BorrowedSwordPairs = std::function<std::vector<std::pair<PlayerId, PlayerId>>(const CardId&)>;
    [[nodiscard]] virtual std::optional<GameAction> decideAction(const PlayerViewState& view, int requiredDiscardCount,
                                                                   const TargetLegality& legalTarget,
                                                                   const BorrowedSwordPairs& borrowedSwordPairs = {},
                                                                   const PlayLegality& legalPlay = {}) const = 0;
    [[nodiscard]] virtual std::vector<SelectionOptionId> decideSelection(const PlayerViewState& view, const CardSelectionView& selection) const = 0;
};

// Stage 10's deterministic policy: first legal response/selection, discard the
// first required cards, and end Play without attempting strategic card use.
class BasicAIActionDecider final : public IAIActionDecider {
public:
    [[nodiscard]] std::optional<GameAction> decideAction(const PlayerViewState& view, int requiredDiscardCount,
                                                          const TargetLegality& legalTarget,
                                                          const BorrowedSwordPairs& borrowedSwordPairs = {},
                                                          const PlayLegality& legalPlay = {}) const override;
    [[nodiscard]] std::vector<SelectionOptionId> decideSelection(const PlayerViewState& view, const CardSelectionView& selection) const override;
};

} // namespace sanguosha::ai
