#pragma once

#include <optional>

#include "session/PlayerViewState.h"

namespace sanguosha::ai {

enum class AIIdentityGoal { Neutral, PreserveLord, EliminateLord, Independent };

// This is deliberately constructed only from a filtered PlayerViewState.  It
// cannot inspect GameEngine players or infer identities that the viewer cannot
// already see.
class AIIdentityObjective final {
public:
    explicit AIIdentityObjective(const PlayerViewState& view) : mode_(view.gameMode), self_(view.selfIdentity)
    {
        if (mode_ != GameMode::Identity) return;
        for (const auto& player : view.players) {
            if (player.identity && *player.identity == PlayerIdentity::Lord) {
                lordPlayerId_ = player.id;
                break;
            }
        }
        if (self_ == PlayerIdentity::Renegade && lordPlayerId_) {
            for (const auto& player : view.players)
                if (player.alive && player.id != view.selfPlayerId && player.id != *lordPlayerId_)
                    renegadeMustKeepLordAlive_ = true;
        }
    }

    [[nodiscard]] bool active() const noexcept { return mode_ == GameMode::Identity && self_ != PlayerIdentity::None; }
    [[nodiscard]] PlayerIdentity selfIdentity() const noexcept { return active() ? self_ : PlayerIdentity::None; }
    [[nodiscard]] std::optional<PlayerId> lordPlayerId() const noexcept { return lordPlayerId_; }
    [[nodiscard]] AIIdentityGoal goal() const noexcept
    {
        if (!active()) return AIIdentityGoal::Neutral;
        if (self_ == PlayerIdentity::Lord || self_ == PlayerIdentity::Loyalist) return AIIdentityGoal::PreserveLord;
        if (self_ == PlayerIdentity::Rebel) return AIIdentityGoal::EliminateLord;
        return AIIdentityGoal::Independent;
    }

    [[nodiscard]] const char* victoryCondition() const noexcept
    {
        if (!active()) return "Be the last survivor.";
        if (self_ == PlayerIdentity::Lord || self_ == PlayerIdentity::Loyalist)
            return "Keep the Lord alive and eliminate every Rebel and Renegade.";
        if (self_ == PlayerIdentity::Rebel) return "Eliminate the Lord.";
        return "Be the sole survivor after eliminating the Lord.";
    }

    [[nodiscard]] bool protectsTargetFromHostileAction(PlayerId target) const noexcept
    {
        return active() && self_ == PlayerIdentity::Loyalist && lordPlayerId_ && target == *lordPlayerId_;
    }

    [[nodiscard]] bool avoidsHostileTarget(PlayerId target) const noexcept
    {
        // A rule guard, not faction balancing: killing Lord before the final
        // duel cannot win for Renegade. Only publicly alive seats are inspected.
        return protectsTargetFromHostileAction(target)
            || (renegadeMustKeepLordAlive_ && lordPlayerId_ && target == *lordPlayerId_);
    }

    [[nodiscard]] int hostileTargetBias(PlayerId target) const noexcept
    {
        if (!active()) return 0;
        if (self_ == PlayerIdentity::Loyalist && lordPlayerId_ && target == *lordPlayerId_) return -10000;
        if (self_ == PlayerIdentity::Rebel && lordPlayerId_ && target == *lordPlayerId_) return 80;
        return 0;
    }

    [[nodiscard]] int beneficialTargetBias(PlayerId target) const noexcept
    {
        return protectsTargetFromHostileAction(target) ? 60 : 0;
    }

    [[nodiscard]] bool shouldRescue(PlayerId target) const noexcept
    {
        return active() && self_ == PlayerIdentity::Loyalist && lordPlayerId_ && target == *lordPlayerId_;
    }

    [[nodiscard]] bool shouldDeclineRescue(PlayerId target) const noexcept
    {
        // Unknown living identities never become presumed allies. Self rescue
        // is handled by the caller before this conservative external policy.
        return active() && !shouldRescue(target);
    }

    [[nodiscard]] bool shouldNullify(const PlayerViewState& view) const noexcept
    {
        if (!view.nullification || !view.nullification->targetId) return false;
        const auto& effect = *view.nullification;
        const auto target = *effect.targetId;
        if (target != view.selfPlayerId && !protectsTargetFromHostileAction(target)) return false;
        const bool beneficial = effect.trickType == CardType::ExNihilo
            || effect.trickType == CardType::PeachGarden || effect.trickType == CardType::Harvest;
        // Odd rounds cancel the original effect; even rounds restore it.
        return beneficial ? effect.chainRound % 2 == 0 : effect.chainRound % 2 == 1;
    }

private:
    GameMode mode_ {GameMode::FreeForAll};
    PlayerIdentity self_ {PlayerIdentity::None};
    std::optional<PlayerId> lordPlayerId_;
    bool renegadeMustKeepLordAlive_ {false};
};

} // namespace sanguosha::ai
