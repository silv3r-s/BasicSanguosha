#pragma once

#include <optional>
#include <vector>

#include "session/PlayerViewState.h"

namespace sanguosha::ai {

using AIInferredIdentity = AIInferredIdentityView;
enum class AIInferenceConfidence { Low, Medium, High };
enum class AIRelationEstimate { Uncertain, LikelyFriendly, LikelyHostile };

enum class AIBehaviorEvidenceType {
    AttackLord,
    HarmLord,
    HealLord,
    NullifyHarmToLord,
    CounterProtectionOfLord,
    RemoveLordResource,
    RemoveBadDelayedTrickFromLord,
    ApplyBadDelayedTrickToLord,
    AttackSuspectedRebel,
    SupportSuspectedRebel,
    LowInformationAoe
};

struct AIBehaviorEvidence final {
    PlayerId actor {0};
    PlayerId target {0};
    AIBehaviorEvidenceType type {AIBehaviorEvidenceType::AttackLord};
    int turnIndex {0};
    int weight {0};
};

struct AIIdentityBelief final {
    PlayerId playerId {0};
    int loyalistScore {0};
    int rebelScore {0};
    int renegadeScore {0};
    int unknownScore {100};
    int evidenceCount {0};
    AIInferredIdentity inferred {AIInferredIdentity::Unknown};
    AIInferenceConfidence confidence {AIInferenceConfidence::Low};
    AIRelationEstimate relation {AIRelationEstimate::Uncertain};
    std::optional<PlayerIdentity> confirmedIdentity;
};

// Rebuilt from the observer's filtered PlayerViewState and structured public
// GameEvent history. No GameEngine or server identity table is accepted.
class AIIdentityInference final {
public:
    void rebuild(const PlayerViewState& view);
    void reset() noexcept;

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] const std::vector<AIIdentityBelief>& beliefs() const noexcept { return beliefs_; }
    [[nodiscard]] const std::vector<AIBehaviorEvidence>& evidence() const noexcept { return evidence_; }
    [[nodiscard]] const AIIdentityBelief* beliefFor(PlayerId playerId) const noexcept;
    [[nodiscard]] AIInferenceModifierView targetModifier(PlayerIdentity observerIdentity, PlayerId playerId) const noexcept;

private:
    bool enabled_ {false};
    std::vector<AIIdentityBelief> beliefs_;
    std::vector<AIBehaviorEvidence> evidence_;
};

} // namespace sanguosha::ai
