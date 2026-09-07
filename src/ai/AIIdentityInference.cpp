#include "ai/AIIdentityInference.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace sanguosha::ai {
namespace {
constexpr int ScoreLimit = 100;
constexpr int ClassificationThreshold = 30;
constexpr int ClassificationMargin = 12;

struct EvidenceWeight final { int loyalist; int rebel; int renegade; };

constexpr EvidenceWeight weightFor(AIBehaviorEvidenceType type) noexcept
{
    switch (type) {
    case AIBehaviorEvidenceType::AttackLord: return {-28, 38, 10};
    case AIBehaviorEvidenceType::HarmLord: return {-10, 14, 4};
    case AIBehaviorEvidenceType::HealLord: return {45, -32, 10};
    case AIBehaviorEvidenceType::NullifyHarmToLord: return {38, -25, 8};
    case AIBehaviorEvidenceType::CounterProtectionOfLord: return {-24, 34, 10};
    case AIBehaviorEvidenceType::RemoveLordResource: return {-22, 30, 8};
    case AIBehaviorEvidenceType::RemoveBadDelayedTrickFromLord: return {34, -22, 7};
    case AIBehaviorEvidenceType::ApplyBadDelayedTrickToLord: return {-32, 42, 10};
    case AIBehaviorEvidenceType::AttackSuspectedRebel: return {12, -5, 5};
    case AIBehaviorEvidenceType::SupportSuspectedRebel: return {-5, 10, 6};
    case AIBehaviorEvidenceType::LowInformationAoe: return {-3, 5, 3};
    }
    return {};
}

bool isDamageAction(const std::string& detail) noexcept
{
    return detail == "Slash" || detail == "Fire Slash" || detail == "Thunder Slash"
        || detail == "Duel" || detail == "FireAttack" || detail == "Fire Attack";
}

bool isBadDelayedTrick(const std::string& detail) noexcept
{
    return detail == "Indulgence" || detail == "Supply Shortage";
}

bool isAoe(const std::string& detail) noexcept
{
    return detail == "Savage Assault" || detail == "Archery Attack";
}

int clampScore(int value) noexcept { return std::clamp(value, -ScoreLimit, ScoreLimit); }

AIInferredIdentity inferredFromKnown(PlayerIdentity identity) noexcept
{
    switch (identity) {
    case PlayerIdentity::Lord: return AIInferredIdentity::Lord;
    case PlayerIdentity::Loyalist: return AIInferredIdentity::Loyalist;
    case PlayerIdentity::Rebel: return AIInferredIdentity::Rebel;
    case PlayerIdentity::Renegade: return AIInferredIdentity::Renegade;
    case PlayerIdentity::None: return AIInferredIdentity::Unknown;
    }
    return AIInferredIdentity::Unknown;
}
}

void AIIdentityInference::reset() noexcept
{
    enabled_ = false;
    beliefs_.clear();
    evidence_.clear();
}

const AIIdentityBelief* AIIdentityInference::beliefFor(PlayerId playerId) const noexcept
{
    const auto found = std::find_if(beliefs_.begin(), beliefs_.end(), [&](const auto& belief) { return belief.playerId == playerId; });
    return found == beliefs_.end() ? nullptr : &*found;
}

void AIIdentityInference::rebuild(const PlayerViewState& view)
{
    reset();
    if (view.gameMode != GameMode::Identity) return;
    enabled_ = true;
    beliefs_.reserve(view.players.size());
    PlayerId lordId = 0;
    for (const auto& player : view.players) {
        AIIdentityBelief belief; belief.playerId = player.id;
        if (player.identity) {
            belief.confirmedIdentity = player.identity;
            belief.inferred = inferredFromKnown(*player.identity);
            belief.confidence = AIInferenceConfidence::High;
            belief.unknownScore = 0;
            if (*player.identity == PlayerIdentity::Lord) lordId = player.id;
        }
        beliefs_.push_back(belief);
    }
    if (!lordId) return;

    struct Working { int loyalEvidence {}; int rebelEvidence {}; };
    std::vector<Working> working(beliefs_.size());
    const auto beliefIndex = [&](PlayerId id) {
        const auto found = std::find_if(beliefs_.begin(), beliefs_.end(), [&](const auto& belief) { return belief.playerId == id; });
        return found == beliefs_.end() ? beliefs_.size() : static_cast<std::size_t>(std::distance(beliefs_.begin(), found));
    };
    const auto apply = [&](PlayerId actor, PlayerId target, AIBehaviorEvidenceType type, int turn) {
        const auto index = beliefIndex(actor);
        if (index == beliefs_.size() || beliefs_[index].confirmedIdentity) return;
        auto weight = weightFor(type);
        auto& belief = beliefs_[index];
        auto& state = working[index];
        const bool loyalEvidence = weight.loyalist > weight.rebel && weight.loyalist > 0;
        const bool rebelEvidence = weight.rebel > weight.loyalist && weight.rebel > 0;
        if (loyalEvidence) ++state.loyalEvidence;
        if (rebelEvidence) ++state.rebelEvidence;
        if (state.loyalEvidence > 0 && state.rebelEvidence > 0) weight.renegade += 14;
        belief.loyalistScore = clampScore(belief.loyalistScore + weight.loyalist);
        belief.rebelScore = clampScore(belief.rebelScore + weight.rebel);
        belief.renegadeScore = clampScore(belief.renegadeScore + weight.renegade);
        belief.unknownScore = std::max(0, belief.unknownScore - 12);
        ++belief.evidenceCount;
        evidence_.push_back({actor, target, type, turn, std::max({std::abs(weight.loyalist), std::abs(weight.rebel), std::abs(weight.renegade)})});
    };

    struct ActiveTrick { PlayerId target {}; bool harmful {}; bool beneficial {}; int nullifications {}; } active;
    int turn = 0;
    for (const auto& event : view.publicEvents) {
        if (event.type == GameEventType::TurnStarted) ++turn;
        if (event.type == GameEventType::TurnEnded) {
            for (auto& belief : beliefs_) if (!belief.confirmedIdentity) {
                belief.loyalistScore = belief.loyalistScore * 97 / 100;
                belief.rebelScore = belief.rebelScore * 97 / 100;
                belief.renegadeScore = belief.renegadeScore * 97 / 100;
            }
        }
        if (event.type == GameEventType::NullificationContext && event.target && *event.target == lordId) {
            const bool beneficial = event.detail.rfind("Beneficial:", 0) == 0;
            const auto separator = event.detail.find(':');
            const int count = separator == std::string::npos ? 0 : std::atoi(event.detail.c_str() + separator + 1);
            active = {lordId, !beneficial, beneficial, count};
        } else if (event.type == GameEventType::CardUsed && event.source) {
            active = {};
            if (event.target && *event.target == lordId) {
                if (isDamageAction(event.detail)) apply(*event.source, lordId, AIBehaviorEvidenceType::AttackLord, turn);
                else if (isBadDelayedTrick(event.detail)) apply(*event.source, lordId, AIBehaviorEvidenceType::ApplyBadDelayedTrickToLord, turn);
                if (isDamageAction(event.detail) || isBadDelayedTrick(event.detail)
                    || event.detail == "Dismantlement" || event.detail == "Snatch") active = {lordId, true, false, 0};
                if (event.detail == "ExNihilo") active = {lordId, false, true, 0};
            } else if (isAoe(event.detail)) {
                apply(*event.source, lordId, AIBehaviorEvidenceType::LowInformationAoe, turn);
            }
            if (event.target && *event.target != lordId) {
                const auto targetIndex = beliefIndex(*event.target);
                if (targetIndex < beliefs_.size() && beliefs_[targetIndex].rebelScore >= 45 && isDamageAction(event.detail))
                    apply(*event.source, *event.target, AIBehaviorEvidenceType::AttackSuspectedRebel, turn);
            }
        } else if (event.type == GameEventType::CardResponded && event.source && event.detail == "Peach"
                   && event.target && *event.target == lordId) {
            apply(*event.source, lordId, AIBehaviorEvidenceType::HealLord, turn);
        } else if (event.type == GameEventType::CardResponded && event.source && event.detail == "Peach" && event.target) {
            const auto targetIndex = beliefIndex(*event.target);
            if (targetIndex < beliefs_.size() && beliefs_[targetIndex].rebelScore >= 45)
                apply(*event.source, *event.target, AIBehaviorEvidenceType::SupportSuspectedRebel, turn);
        } else if (event.type == GameEventType::CardResponded && event.source && event.detail == "Nullification"
                   && active.target == lordId) {
            ++active.nullifications;
            const bool protects = (active.harmful && active.nullifications % 2 == 1)
                || (active.beneficial && active.nullifications % 2 == 0);
            apply(*event.source, lordId, protects ? AIBehaviorEvidenceType::NullifyHarmToLord
                                                  : AIBehaviorEvidenceType::CounterProtectionOfLord, turn);
        } else if (event.type == GameEventType::DamageReceived && event.source && event.target && *event.target == lordId) {
            apply(*event.source, lordId, AIBehaviorEvidenceType::HarmLord, turn);
        } else if (event.type == GameEventType::PublicCardMoved && event.source && event.target && *event.target == lordId) {
            if (event.detail.find(":Judgment:Indulgence") != std::string::npos
                || event.detail.find(":Judgment:Supply Shortage") != std::string::npos)
                apply(*event.source, lordId, AIBehaviorEvidenceType::RemoveBadDelayedTrickFromLord, turn);
            else apply(*event.source, lordId, AIBehaviorEvidenceType::RemoveLordResource, turn);
        }
    }

    for (std::size_t index = 0; index < beliefs_.size(); ++index) {
        auto& belief = beliefs_[index];
        if (belief.confirmedIdentity) continue;
        const std::array<std::pair<int, AIInferredIdentity>, 3> values {{
            {belief.loyalistScore, AIInferredIdentity::Loyalist},
            {belief.rebelScore, AIInferredIdentity::Rebel},
            {belief.renegadeScore, AIInferredIdentity::Renegade}}};
        auto ordered = values;
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) { return left.first > right.first; });
        const int margin = ordered[0].first - ordered[1].first;
        if (ordered[0].first >= ClassificationThreshold && margin >= ClassificationMargin)
            belief.inferred = ordered[0].second;
        else belief.inferred = AIInferredIdentity::Unknown;
        if (belief.evidenceCount >= 3 && ordered[0].first >= 60 && margin >= 25) belief.confidence = AIInferenceConfidence::High;
        else if (belief.evidenceCount >= 2 && ordered[0].first >= 35 && margin >= 15) belief.confidence = AIInferenceConfidence::Medium;
        else belief.confidence = AIInferenceConfidence::Low;
        if (belief.inferred == AIInferredIdentity::Rebel) belief.relation = AIRelationEstimate::LikelyHostile;
        else if (belief.inferred == AIInferredIdentity::Loyalist) belief.relation = AIRelationEstimate::LikelyFriendly;
    }
}

AIInferenceModifierView AIIdentityInference::targetModifier(PlayerIdentity observerIdentity, PlayerId playerId) const noexcept
{
    AIInferenceModifierView result; result.playerId = playerId;
    const auto* belief = beliefFor(playerId);
    if (!belief || belief->confirmedIdentity || belief->inferred == AIInferredIdentity::Unknown) return result;
    const int strength = belief->confidence == AIInferenceConfidence::High ? 18
        : belief->confidence == AIInferenceConfidence::Medium ? 10 : 4;
    result.confidence = static_cast<int>(belief->confidence) + 1;
    if (observerIdentity == PlayerIdentity::Lord || observerIdentity == PlayerIdentity::Loyalist) {
        if (belief->inferred == AIInferredIdentity::Rebel) result.hostile = strength;
        else if (belief->inferred == AIInferredIdentity::Loyalist) result.friendly = strength;
        else result.hostile = strength / 3;
    } else if (observerIdentity == PlayerIdentity::Rebel) {
        if (belief->inferred == AIInferredIdentity::Loyalist) result.hostile = strength;
        else if (belief->inferred == AIInferredIdentity::Rebel) result.friendly = strength;
    }
    return result;
}

} // namespace sanguosha::ai
