#include "ai/AIIdentityStrategy.h"

#include <algorithm>
#include <cmath>

#include "ai/AIThreatEvaluator.h"

namespace sanguosha::ai {
namespace {
enum class EstimatedRole { Unknown, Lord, Loyalist, Rebel, Renegade };

const PublicPlayerView* playerFor(const PlayerViewState& view, PlayerId id) noexcept
{
    const auto found = std::find_if(view.players.begin(), view.players.end(),
        [&](const auto& player) { return player.id == id; });
    return found == view.players.end() ? nullptr : &*found;
}

const AIInferenceModifierView* inferenceFor(const PlayerViewState& view, PlayerId id) noexcept
{
    const auto found = std::find_if(view.inferenceModifiers.begin(), view.inferenceModifiers.end(),
        [&](const auto& item) { return item.playerId == id; });
    return found == view.inferenceModifiers.end() ? nullptr : &*found;
}

EstimatedRole knownRole(const PublicPlayerView& player) noexcept
{
    if (!player.identity) return EstimatedRole::Unknown;
    switch (*player.identity) {
    case PlayerIdentity::Lord: return EstimatedRole::Lord;
    case PlayerIdentity::Loyalist: return EstimatedRole::Loyalist;
    case PlayerIdentity::Rebel: return EstimatedRole::Rebel;
    case PlayerIdentity::Renegade: return EstimatedRole::Renegade;
    case PlayerIdentity::None: return EstimatedRole::Unknown;
    }
    return EstimatedRole::Unknown;
}

EstimatedRole estimatedRole(const PlayerViewState& view, const PublicPlayerView& player) noexcept
{
    const auto known = knownRole(player);
    if (known != EstimatedRole::Unknown) return known;
    const auto* belief = inferenceFor(view, player.id);
    if (!belief) return EstimatedRole::Unknown;
    switch (belief->inferred) {
    case AIInferredIdentityView::Lord: return EstimatedRole::Lord;
    case AIInferredIdentityView::Loyalist: return EstimatedRole::Loyalist;
    case AIInferredIdentityView::Rebel: return EstimatedRole::Rebel;
    case AIInferredIdentityView::Renegade: return EstimatedRole::Renegade;
    case AIInferredIdentityView::Unknown: return EstimatedRole::Unknown;
    }
    return EstimatedRole::Unknown;
}

int confidenceWeight(const PlayerViewState& view, PlayerId id) noexcept
{
    const auto* player = playerFor(view, id);
    if (player && player->identity) return 90;
    const auto* belief = inferenceFor(view, id);
    if (!belief) return 0;
    return belief->confidence >= 3 ? 52 : belief->confidence == 2 ? 32 : belief->confidence == 1 ? 14 : 0;
}

bool isDamageCard(CardType type) noexcept
{
    return isSlashCard(type) || type == CardType::Duel || type == CardType::FireAttack
        || type == CardType::BarbarianInvasion || type == CardType::ArrowBarrage;
}

bool isControlCard(CardType type) noexcept
{
    return type == CardType::Dismantlement || type == CardType::Snatch || type == CardType::BorrowedSword;
}

bool isDelayed(CardType type) noexcept
{
    return type == CardType::Indulgence || type == CardType::SupplyShortage;
}

bool isBeneficialTrick(CardType type) noexcept
{
    return type == CardType::ExNihilo || type == CardType::PeachGarden || type == CardType::Harvest;
}
}

AIIdentityStrategyResult AIIdentityStrategy::evaluate(const PlayerViewState& view) noexcept
{
    AIIdentityStrategyResult result;
    if (view.gameMode != GameMode::Identity || view.selfIdentity == PlayerIdentity::None) return result;
    const auto* self = playerFor(view, view.selfPlayerId);
    const auto lord = std::find_if(view.players.begin(), view.players.end(), [](const auto& player) {
        return player.identity && *player.identity == PlayerIdentity::Lord;
    });
    if (!self || lord == view.players.end()) return result;
    result.enabled = true;

    const int aliveCount = int(std::count_if(view.players.begin(), view.players.end(), [](const auto& p) { return p.alive; }));
    if (aliveCount <= 2) result.phase = AIStrategyPhase::Endgame;
    else if (aliveCount <= 3) result.phase = AIStrategyPhase::LateGame;
    else if (view.turnNumber <= aliveCount && lord->hp > 2 && self->hp > 2) result.phase = AIStrategyPhase::EarlyGame;
    else result.phase = AIStrategyPhase::MidGame;

    int rebelStrength = 0, lordStrength = std::max(0, lord->hp) * 14 + int(lord->handCardCount) * 4;
    int likelyRebels = 0, likelyLoyalists = 0;
    for (const auto& player : view.players) {
        if (!player.alive || player.id == lord->id) continue;
        const int publicStrength = std::max(0, player.hp) * 10 + int(player.handCardCount) * 3
            + AIThreatEvaluator::threatScore(view, player) / 4;
        const auto role = estimatedRole(view, player);
        if (role == EstimatedRole::Rebel) { rebelStrength += publicStrength; ++likelyRebels; }
        else if (role == EstimatedRole::Loyalist) { lordStrength += publicStrength; ++likelyLoyalists; }
    }
    result.tableBalanceScore = likelyRebels == 0 && likelyLoyalists == 0
        ? 0 : std::clamp(rebelStrength - lordStrength, -100, 100);

    const bool lowSelf = self->hp <= 2;
    const bool lowLord = lord->hp <= 2;
    switch (view.selfIdentity) {
    case PlayerIdentity::Lord:
        result.intent = lowSelf ? AIStrategicIntent::PreserveSelf : AIStrategicIntent::EliminateLikelyRebel;
        result.selfPreservation = lowSelf ? 90 : 35;
        result.riskTolerance = lowSelf ? -55 : -15;
        result.aggressionLevel = lowSelf ? -35 : 25;
        result.nullificationLordModifier = 90;
        break;
    case PlayerIdentity::Loyalist:
        result.intent = lowLord ? AIStrategicIntent::ProtectLord : AIStrategicIntent::EliminateLikelyRebel;
        result.selfPreservation = lowSelf ? 55 : 20;
        result.riskTolerance = lowSelf ? -30 : 0;
        result.aggressionLevel = 25;
        result.rescueLordModifier = 140;
        result.nullificationLordModifier = 120;
        break;
    case PlayerIdentity::Rebel:
        result.intent = lowLord ? AIStrategicIntent::FinishLord : AIStrategicIntent::PressureLord;
        result.selfPreservation = lowSelf ? 55 : 15;
        result.riskTolerance = lowLord ? 45 : 15;
        result.aggressionLevel = lowLord ? 85 : 55;
        result.rescueLordModifier = -200;
        result.nullificationLordModifier = -100;
        break;
    case PlayerIdentity::Renegade:
        result.selfPreservation = lowSelf ? 100 : 45;
        result.riskTolerance = lowSelf ? -60 : -10;
        if (result.phase == AIStrategyPhase::Endgame) {
            result.intent = AIStrategicIntent::FinishLord;
            result.aggressionLevel = 100;
            result.rescueLordModifier = -200;
            result.nullificationLordModifier = -100;
        } else if (result.tableBalanceScore >= 25 || (lowLord && likelyRebels > 0)) {
            result.intent = AIStrategicIntent::ProtectLord;
            result.aggressionLevel = 15;
            result.rescueLordModifier = 130;
            result.nullificationLordModifier = 90;
        } else if (result.tableBalanceScore <= -25) {
            result.intent = AIStrategicIntent::BalanceTable;
            result.aggressionLevel = 25;
            result.rescueLordModifier = 20;
            result.nullificationLordModifier = 0;
        } else {
            result.intent = lowSelf ? AIStrategicIntent::PreserveSelf : AIStrategicIntent::BalanceTable;
            result.aggressionLevel = result.phase == AIStrategyPhase::EarlyGame ? -15 : 5;
            result.rescueLordModifier = lowLord && likelyRebels > 0 ? 90 : 10;
        }
        break;
    case PlayerIdentity::None: break;
    }

    for (const auto& target : view.players) {
        if (!target.alive || target.id == view.selfPlayerId) continue;
        AIIdentityTargetModifier modifier; modifier.playerId = target.id;
        const auto role = estimatedRole(view, target);
        const int strength = confidenceWeight(view, target.id);
        const bool known = bool(target.identity);
        if (view.selfIdentity == PlayerIdentity::Lord || view.selfIdentity == PlayerIdentity::Loyalist) {
            if (role == EstimatedRole::Rebel) modifier.general += strength;
            else if (role == EstimatedRole::Loyalist || (view.selfIdentity == PlayerIdentity::Loyalist && role == EstimatedRole::Lord))
                modifier.general -= strength + (known ? 40 : 12);
            else if (role == EstimatedRole::Renegade) modifier.general += likelyRebels == 0 ? strength : strength / 4;
        } else if (view.selfIdentity == PlayerIdentity::Rebel) {
            if (role == EstimatedRole::Lord) modifier.general += 110 + (target.hp <= 1 ? 100 : 0);
            else if (role == EstimatedRole::Loyalist) modifier.general += strength / 2;
            else if (role == EstimatedRole::Rebel) modifier.general -= strength;
        } else if (view.selfIdentity == PlayerIdentity::Renegade) {
            if (result.phase == AIStrategyPhase::Endgame && role == EstimatedRole::Lord) modifier.general += 150;
            else if (result.tableBalanceScore >= 25) {
                if (role == EstimatedRole::Rebel) modifier.general += std::max(28, strength / 2);
                if (role == EstimatedRole::Lord) modifier.general -= 90;
            } else if (result.tableBalanceScore <= -25) {
                if (role == EstimatedRole::Lord || role == EstimatedRole::Loyalist) modifier.general += std::max(25, strength / 2);
                if (role == EstimatedRole::Rebel) modifier.general -= strength / 2;
            }
            else if (role == EstimatedRole::Lord) modifier.general -= 85;
            if (aliveCount == 3 && role == EstimatedRole::Rebel && lord->hp <= 2) modifier.general += 65;
        }
        if (lowSelf && AIThreatEvaluator::threatScore(view, target) >= 65) modifier.general += 24;
        modifier.damage = modifier.general;
        modifier.resourceDenial = modifier.general + ((role == EstimatedRole::Lord && view.selfIdentity == PlayerIdentity::Rebel) ? 35 : 0);
        modifier.delayedTrick = modifier.general + ((role == EstimatedRole::Lord && view.selfIdentity == PlayerIdentity::Rebel) ? 28 : 0);
        result.targets.push_back(modifier);
    }

    int friendlyAoeRisk = 0, hostileAoeValue = 0;
    for (const auto& target : view.players) {
        if (!target.alive || target.id == view.selfPlayerId) continue;
        const auto role = estimatedRole(view, target);
        const int value = 12 + std::max(0, 3 - target.hp) * 12;
        if ((view.selfIdentity == PlayerIdentity::Lord || view.selfIdentity == PlayerIdentity::Loyalist)
            && (role == EstimatedRole::Lord || role == EstimatedRole::Loyalist)) friendlyAoeRisk += value;
        else if (view.selfIdentity == PlayerIdentity::Rebel && role == EstimatedRole::Lord) hostileAoeValue += value * 2;
        else if (view.selfIdentity == PlayerIdentity::Rebel && role == EstimatedRole::Rebel) friendlyAoeRisk += value;
    }
    result.aoeModifier = hostileAoeValue - friendlyAoeRisk;
    if (view.selfIdentity == PlayerIdentity::Lord || view.selfIdentity == PlayerIdentity::Loyalist)
        result.peachGardenModifier = (lord->maxHp - lord->hp) * 45;
    else if (view.selfIdentity == PlayerIdentity::Rebel)
        result.peachGardenModifier = -(lord->maxHp - lord->hp) * 55;
    else result.peachGardenModifier = result.tableBalanceScore > 0 ? 30 : result.tableBalanceScore < 0 ? -20 : 0;
    result.harvestModifier = self->handCardCount <= 1 ? 25 : 0;
    return result;
}

int AIIdentityStrategy::targetModifier(const AIIdentityStrategyResult& result, PlayerId target,
                                       CardType cardType) noexcept
{
    if (!result.enabled) return 0;
    const auto found = std::find_if(result.targets.begin(), result.targets.end(),
        [&](const auto& item) { return item.playerId == target; });
    if (found == result.targets.end()) return 0;
    if (isDelayed(cardType)) return found->delayedTrick;
    if (isControlCard(cardType)) return found->resourceDenial;
    if (isDamageCard(cardType)) return found->damage;
    return found->general;
}

int AIIdentityStrategy::cardUseModifier(const PlayerViewState&, const AIIdentityStrategyResult& result,
                                        CardType cardType) noexcept
{
    if (!result.enabled) return 0;
    if (cardType == CardType::BarbarianInvasion || cardType == CardType::ArrowBarrage) return result.aoeModifier;
    if (cardType == CardType::PeachGarden) return result.peachGardenModifier;
    if (cardType == CardType::Harvest) return result.harvestModifier;
    if (isDamageCard(cardType) || isControlCard(cardType) || isDelayed(cardType)) return result.aggressionLevel / 3;
    return 0;
}

int AIIdentityStrategy::cardKeepModifier(const PlayerViewState&, const AIIdentityStrategyResult& result,
                                         CardType cardType) noexcept
{
    if (!result.enabled) return 0;
    if (cardType == CardType::Peach) return result.selfPreservation + std::max(0, result.rescueLordModifier / 3);
    if (cardType == CardType::Dodge || cardType == CardType::Armor || cardType == CardType::DefensiveHorse)
        return result.selfPreservation;
    if (cardType == CardType::Nullification) return result.selfPreservation / 2 + std::max(0, result.nullificationLordModifier / 3);
    return 0;
}

bool AIIdentityStrategy::shouldRescue(const PlayerViewState& view, const AIIdentityStrategyResult& result,
                                      PlayerId target) noexcept
{
    if (!result.enabled) return target == view.selfPlayerId;
    if (target == view.selfPlayerId) return true;
    const auto* player = playerFor(view, target);
    if (!player) return false;
    if (player->identity && *player->identity == PlayerIdentity::Lord)
        return result.rescueLordModifier >= 80;
    const auto role = estimatedRole(view, *player);
    if (view.selfIdentity == PlayerIdentity::Rebel) return role == EstimatedRole::Rebel && confidenceWeight(view, target) >= 32;
    if (view.selfIdentity == PlayerIdentity::Lord || view.selfIdentity == PlayerIdentity::Loyalist)
        return role == EstimatedRole::Loyalist && confidenceWeight(view, target) >= 32;
    return false;
}

bool AIIdentityStrategy::shouldNullify(const PlayerViewState& view,
                                       const AIIdentityStrategyResult& result) noexcept
{
    if (!result.enabled || !view.nullification || !view.nullification->targetId) return false;
    const auto target = *view.nullification->targetId;
    const auto* player = playerFor(view, target);
    if (!player) return false;
    bool protect = target == view.selfPlayerId;
    if (player->identity && *player->identity == PlayerIdentity::Lord)
        protect = view.selfIdentity == PlayerIdentity::Lord
            ? target == view.selfPlayerId : result.nullificationLordModifier >= 50;
    else {
        const auto role = estimatedRole(view, *player);
        if (view.selfIdentity == PlayerIdentity::Rebel) protect = role == EstimatedRole::Rebel;
        else if (view.selfIdentity == PlayerIdentity::Lord || view.selfIdentity == PlayerIdentity::Loyalist)
            protect = role == EstimatedRole::Loyalist;
    }
    const bool beneficial = isBeneficialTrick(view.nullification->trickType);
    const bool cancellationRound = view.nullification->chainRound % 2 == 1;
    // A responder protects a desired effect by declining.  Do not spend a
    // second Nullification merely to reverse an already settled protection;
    // this preserves the established conservative response boundary.
    return protect && (beneficial ? !cancellationRound : cancellationRound);
}

} // namespace sanguosha::ai
