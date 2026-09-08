#include "ai/AIActionPacing.h"

#include <atomic>

namespace sanguosha::ai {
namespace { std::atomic<AISpeedPreset> activePreset {AISpeedPreset::Standard}; }

AIPacingValues AIActionPacing::values(AISpeedPreset preset) noexcept
{
    switch (preset) {
    case AISpeedPreset::Test: return {0, 0, 0, 0};
    case AISpeedPreset::Fast: return {500, 750, 180, 250};
    case AISpeedPreset::Slow: return {1800, 2400, 450, 900};
    case AISpeedPreset::Standard: return {1200, 1600, 320, 600};
    }
    return {1200, 1600, 320, 600};
}

void AIActionPacing::setPreset(AISpeedPreset preset) noexcept { activePreset.store(preset); }
AISpeedPreset AIActionPacing::preset() noexcept { return activePreset.load(); }

int AIActionPacing::delayFor(const PlayerViewState& view, bool turnTransition) noexcept
{
    if (view.gameOver) return 0;
    const auto pacing = values(preset());
    int delay = pacing.normalActionMs;
    if (view.cardSelection) delay = pacing.criticalActionMs;
    else if (view.response && view.response->isResponder)
        delay = view.response->selectableCards.empty() ? pacing.passMs : pacing.criticalActionMs;
    else if (view.currentTurnPlayer != view.selfPlayerId) delay = pacing.passMs;
    else if (view.currentPhase == Phase::Play)
        delay = view.ownHand.empty() ? pacing.passMs : pacing.criticalActionMs;
    return delay + (turnTransition ? pacing.turnTransitionMs : 0);
}

} // namespace sanguosha::ai
