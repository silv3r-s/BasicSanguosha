#include "ai/AIActionPacing.h"

namespace sanguosha::ai {

int AIActionPacing::delayFor(const PlayerViewState& view, bool turnTransition) noexcept
{
    if (view.gameOver) return 0;
    int delay = NormalActionDelayMs;
    if (view.cardSelection) delay = CriticalActionDelayMs;
    else if (view.response && view.response->isResponder)
        delay = view.response->selectableCards.empty() ? PassDelayMs : CriticalActionDelayMs;
    else if (view.currentTurnPlayer != view.selfPlayerId) delay = PassDelayMs;
    else if (view.currentPhase == Phase::Play)
        delay = view.ownHand.empty() ? PassDelayMs : CriticalActionDelayMs;
    return delay + (turnTransition ? TurnTransitionDelayMs : 0);
}

} // namespace sanguosha::ai
