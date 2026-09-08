#pragma once

#include "session/PlayerViewState.h"

namespace sanguosha::ai {

enum class AISpeedPreset { Test, Fast, Standard, Slow };

struct AIPacingValues final {
    int normalActionMs;
    int criticalActionMs;
    int passMs;
    int turnTransitionMs;
};

struct AIActionPacing final {
    static constexpr int NormalActionDelayMs = 1200;
    static constexpr int CriticalActionDelayMs = 1600;
    static constexpr int PassDelayMs = 320;
    static constexpr int TurnTransitionDelayMs = 600;
    [[nodiscard]] static AIPacingValues values(AISpeedPreset preset) noexcept;
    static void setPreset(AISpeedPreset preset) noexcept;
    [[nodiscard]] static AISpeedPreset preset() noexcept;
    [[nodiscard]] static int delayFor(const PlayerViewState& view, bool turnTransition = false) noexcept;
};

} // namespace sanguosha::ai
