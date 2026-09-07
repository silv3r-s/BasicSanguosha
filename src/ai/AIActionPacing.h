#pragma once

#include "session/PlayerViewState.h"

namespace sanguosha::ai {

struct AIActionPacing final {
    static constexpr int NormalActionDelayMs = 1200;
    static constexpr int CriticalActionDelayMs = 1600;
    static constexpr int PassDelayMs = 320;
    static constexpr int TurnTransitionDelayMs = 600;
    [[nodiscard]] static int delayFor(const PlayerViewState& view, bool turnTransition = false) noexcept;
};

} // namespace sanguosha::ai
