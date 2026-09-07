#include "ai/AITargetEvaluator.h"

#include <algorithm>
#include <array>
#include <limits>

#include "ai/AIIdentityObjective.h"
#include "ai/AIIdentityStrategy.h"
#include "ai/AIResourcePlanner.h"
#include "ai/AIThreatEvaluator.h"

namespace sanguosha::ai {
namespace {
const CardView* equipped(const PublicPlayerView& player, EquipmentSlot slot) noexcept
{
    const auto& card = player.equipment.cardsBySlot[static_cast<std::size_t>(slot)];
    return card ? &*card : nullptr;
}

bool armorNamed(const PublicPlayerView& player, const char* name) noexcept
{
    const auto* armor = equipped(player, EquipmentSlot::Armor);
    return armor && armor->displayName == name;
}

bool ownsElementalDamage(const PlayerViewState& view) noexcept
{
    return std::any_of(view.ownHand.begin(), view.ownHand.end(), [](const auto& card) {
        return card.type == CardType::FireSlash || card.type == CardType::ThunderSlash || card.type == CardType::FireAttack;
    });
}

int defenseAgainst(const CardView& card, const PublicPlayerView& target) noexcept
{
    int defense = std::min(int(target.handCardCount), 6) * 3;
    if (armorNamed(target, "八卦阵") && (isSlashCard(card.type) || card.type == CardType::ArrowBarrage)) defense += 28;
    if (armorNamed(target, "仁王盾") && card.type == CardType::Slash
        && (card.suit == Suit::Spade || card.suit == Suit::Club)) defense += 75;
    if (armorNamed(target, "藤甲")) {
        if (card.type == CardType::Slash || card.type == CardType::BarbarianInvasion || card.type == CardType::ArrowBarrage)
            defense += 95;
        else if (card.type == CardType::FireSlash || card.type == CardType::FireAttack) defense -= 42;
    }
    if (armorNamed(target, "白银狮子")) defense += 12;
    if (equipped(target, EquipmentSlot::DefensiveHorse) && isSlashCard(card.type)) defense += 8;
    return defense;
}

int chainPropagationValue(const PlayerViewState& view, const PublicPlayerView& primary) noexcept
{
    if (!primary.chained) return 0;
    const AIIdentityObjective objective(view);
    int value = 0;
    for (const auto& player : view.players) {
        if (!player.alive || !player.chained || player.id == primary.id) continue;
        if (player.id == view.selfPlayerId) {
            value -= 105 + std::max(0, 3 - player.hp) * 30;
        } else if (objective.avoidsHostileTarget(player.id)) {
            value -= 100 + std::max(0, 3 - player.hp) * 25;
        } else {
            value += 28 + std::max(0, 4 - player.hp) * 18
                + AIThreatEvaluator::threatScore(view, player) / 3
                - AIThreatEvaluator::defenseScore(player) / 3;
        }
    }
    return value;
}

int minimumFireAttackCost(const PlayerViewState& view) noexcept
{
    int result = std::numeric_limits<int>::max();
    for (const auto& card : view.ownHand) {
        if (card.type == CardType::FireAttack) continue;
        result = std::min(result, AIResourcePlanner::responseCost(view, card));
    }
    return result;
}

int publicOptionValue(const CardSelectionOptionView& option) noexcept
{
    if (option.hidden) return 24;
    if (option.equipmentSlot == EquipmentSlot::Weapon) {
        if (option.displayName == "诸葛连弩" || option.displayName == "贯石斧" || option.displayName == "青龙偃月刀") return 95;
        if (option.displayName == "丈八蛇矛" || option.displayName == "方天画戟") return 82;
        return 68;
    }
    if (option.equipmentSlot == EquipmentSlot::Armor) return option.displayName == "八卦阵" ? 88 : 76;
    if (option.equipmentSlot == EquipmentSlot::DefensiveHorse) return 62;
    if (option.equipmentSlot == EquipmentSlot::OffensiveHorse) return 56;
    if (option.displayName == "乐不思蜀" || option.displayName == "兵粮寸断") return -75;
    if (option.displayName == "闪电") return 18;
    return 30;
}

bool setTieBefore(const PlayerViewState& view, const std::vector<PlayerId>& left, const std::vector<PlayerId>& right)
{
    const auto key = [&](const std::vector<PlayerId>& ids) {
        std::vector<std::pair<Seat, PlayerId>> result;
        for (const auto id : ids) {
            const auto player = std::find_if(view.players.begin(), view.players.end(), [&](const auto& item) { return item.id == id; });
            result.push_back(player == view.players.end() ? std::pair<Seat, PlayerId> {std::numeric_limits<Seat>::max(), id}
                                                          : std::pair<Seat, PlayerId> {player->seat, id});
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    return key(left) < key(right);
}
}

int AIAdvancedTargetEvaluation::finalScore() const noexcept
{
    return tacticalValue + threatValue + killValue + resourceDenialValue + defenseBreakValue
        + elementalValue + chainValue + knownIdentityModifier + inferenceModifier
        + identityStrategyModifier - riskPenalty;
}

int AITargetSetEvaluation::finalScore() const noexcept
{
    return individualValue + synergyValue - friendlyRisk - resourceRisk;
}

AIAdvancedTargetEvaluation AITargetEvaluator::evaluate(const PlayerViewState& view, const CardView& card,
                                                       const PublicPlayerView& target, bool legal,
                                                       int slashFamilyCount) noexcept
{
    AIAdvancedTargetEvaluation result;
    if (!legal || !target.alive || (target.id == view.selfPlayerId && card.type != CardType::IronChain)) {
        result.riskPenalty = 10000;
        return result;
    }
    const AIIdentityObjective objective(view);
    result.knownIdentityModifier = objective.hostileTargetBias(target.id);
    const auto inferred = std::find_if(view.inferenceModifiers.begin(), view.inferenceModifiers.end(),
        [&](const auto& modifier) { return modifier.playerId == target.id; });
    if (inferred != view.inferenceModifiers.end()) result.inferenceModifier = inferred->hostile - inferred->friendly;
    const auto strategy = AIIdentityStrategy::evaluate(view);
    result.identityStrategyModifier = AIIdentityStrategy::targetModifier(strategy, target.id, card.type);
    const bool renegadeStrategyOverride = view.selfIdentity == PlayerIdentity::Renegade
        && result.identityStrategyModifier > 0;
    if (objective.avoidsHostileTarget(target.id) && !renegadeStrategyOverride && card.type != CardType::IronChain) {
        result.riskPenalty = 10000;
        return result;
    }
    const auto threat = AIThreatEvaluator::evaluate(view, target, true, slashFamilyCount);
    result.tacticalValue = 18 + std::max(0, 4 - target.hp) * 8;
    result.threatValue = threat.threat;

    if (isSlashCard(card.type)) {
        result.killValue = threat.killOpportunity * 2;
        result.defenseBreakValue = -defenseAgainst(card, target);
        if (target.hp == 1) result.killValue += std::max(0, 75 - threat.defense * 2 - int(target.handCardCount) * 7);
        if (card.type == CardType::FireSlash) {
            if (armorNamed(target, "藤甲")) result.elementalValue += 85;
            if (target.chained) result.chainValue += chainPropagationValue(view, target);
        } else if (card.type == CardType::ThunderSlash) {
            if (target.chained) result.chainValue += chainPropagationValue(view, target) + 18;
        } else if (target.chained) {
            result.elementalValue -= 4; // no fake propagation bonus for ordinary Slash
        }
        if (target.handCardCount == 0) {
            const auto self = std::find_if(view.players.begin(), view.players.end(), [&](const auto& player) { return player.id == view.selfPlayerId; });
            if (self != view.players.end()) {
                const auto* weapon = equipped(*self, EquipmentSlot::Weapon);
                if (weapon && weapon->displayName == "古锭刀") result.tacticalValue += 55;
            }
        }
    } else if (card.type == CardType::Duel) {
        result.killValue = threat.killOpportunity * 2;
        result.threatValue /= 2;
        result.resourceDenialValue = slashFamilyCount * 18 - int(target.handCardCount) * 15;
        if (target.handCardCount == 0) result.killValue += 45;
    } else if (card.type == CardType::Dismantlement || card.type == CardType::Snatch) {
        const int publicResources = AIThreatEvaluator::resourceValue(target);
        result.killValue = target.hp <= 1 ? threat.killOpportunity / 2 : 0;
        result.resourceDenialValue = (card.type == CardType::Snatch ? publicResources * 2 : publicResources)
            + threat.equipmentThreat * 3 + int(target.handCardCount) * (card.type == CardType::Snatch ? 7 : 5);
        result.defenseBreakValue = equipped(target, EquipmentSlot::Armor) ? 38 : 0;
        for (const auto& judgment : target.judgmentCards) {
            if (judgment.type == CardType::Indulgence || judgment.type == CardType::SupplyShortage)
                result.riskPenalty += 70;
        }
        if (target.handCardCount == 0 && !equipped(target, EquipmentSlot::Weapon)
            && !equipped(target, EquipmentSlot::Armor) && !equipped(target, EquipmentSlot::OffensiveHorse)
            && !equipped(target, EquipmentSlot::DefensiveHorse) && target.judgmentCards.empty()) result.riskPenalty += 10000;
    } else if (card.type == CardType::Indulgence || card.type == CardType::SupplyShortage) {
        result.threatValue *= 2;
        result.resourceDenialValue = card.type == CardType::Indulgence
            ? int(target.handCardCount) * 10 + threat.equipmentThreat
            : std::max(0, 5 - int(target.handCardCount)) * 12 + threat.threat / 2;
    } else if (card.type == CardType::FireAttack) {
        if (target.handCardCount == 0) result.riskPenalty += 10000;
        result.killValue = threat.killOpportunity * 2;
        result.elementalValue = armorNamed(target, "藤甲") ? 80 : 18;
        result.chainValue = chainPropagationValue(view, target);
        const int cost = minimumFireAttackCost(view);
        result.riskPenalty += cost == std::numeric_limits<int>::max() ? 120 : cost / 2;
    } else if (card.type == CardType::IronChain) {
        const bool elementalReady = ownsElementalDamage(view);
        result.threatValue = target.id == view.selfPlayerId ? 0 : threat.threat / 2;
        if (target.chained) {
            if (target.id == view.selfPlayerId) result.chainValue = elementalReady ? 125 : 45;
            else if (objective.avoidsHostileTarget(target.id) && !renegadeStrategyOverride) result.chainValue = 105;
            else result.chainValue = elementalReady ? -65 : 10;
        } else {
            if (target.id == view.selfPlayerId || (objective.avoidsHostileTarget(target.id) && !renegadeStrategyOverride)) result.riskPenalty += 120;
            else result.chainValue = elementalReady ? 65 + std::max(0, 4 - target.hp) * 15 : 8;
        }
    }
    return result;
}

AITargetSetEvaluation AITargetEvaluator::bestTargetSet(const PlayerViewState& view, const CardView& card,
                                                       const TargetLegality& legalTarget)
{
    const AIIdentityObjective objective(view);
    const auto strategy = AIIdentityStrategy::evaluate(view);
    std::vector<const PublicPlayerView*> legal;
    for (const auto& player : view.players) {
        const bool renegadeStrategyOverride = view.selfIdentity == PlayerIdentity::Renegade
            && AIIdentityStrategy::targetModifier(strategy, player.id, card.type) > 0;
        if (legalTarget(card.id, player.id)
            && (card.type == CardType::IronChain || !objective.avoidsHostileTarget(player.id)
                || renegadeStrategyOverride)) legal.push_back(&player);
    }
    std::stable_sort(legal.begin(), legal.end(), [](const auto* left, const auto* right) {
        return left->seat != right->seat ? left->seat < right->seat : left->id < right->id;
    });
    const int slashCount = int(std::count_if(view.ownHand.begin(), view.ownHand.end(), [](const auto& item) { return isSlashCard(item.type); }));
    AITargetSetEvaluation best;
    best.individualValue = -10000;
    std::vector<PlayerId> current;
    // The public minTargets helper historically reports zero for several
    // engine branches that still require one target at submit time (Slash,
    // Duel, Dismantlement, Snatch). Iron Chain is the sole targeted card whose
    // empty set is a legal recast.
    const int minimum = card.type == CardType::IronChain ? 0 : std::max(1, card.minTargets);
    const int maximum = std::min({card.maxTargets, int(legal.size()), 3});
    const auto consider = [&] (const std::vector<PlayerId>& targets, AITargetSetEvaluation& output) {
        AITargetSetEvaluation candidate; candidate.targets = targets;
        for (const auto id : targets) {
            const auto target = std::find_if(view.players.begin(), view.players.end(), [&](const auto& player) { return player.id == id; });
            candidate.individualValue += evaluate(view, card, *target, true, slashCount).finalScore();
        }
        if (card.type == CardType::IronChain && targets.size() == 2 && ownsElementalDamage(view)) candidate.synergyValue += 30;
        if (candidate.finalScore() > output.finalScore()
            || (candidate.finalScore() == output.finalScore() && setTieBefore(view, candidate.targets, output.targets))) output = std::move(candidate);
    };
    std::function<void(std::size_t)> generate = [&](std::size_t start) {
        if (int(current.size()) >= minimum) consider(current, best);
        if (int(current.size()) == maximum) return;
        for (std::size_t index = start; index < legal.size(); ++index) {
            current.push_back(legal[index]->id); generate(index + 1); current.pop_back();
        }
    };
    generate(0);
    if (best.individualValue == -10000 || (minimum == 0 && best.finalScore() <= 0)) return {};
    return best;
}

int AITargetEvaluator::aoeValue(const PlayerViewState& view, CardType type, int slashFamilyCount) noexcept
{
    const AIIdentityObjective objective(view);
    const auto strategy = AIIdentityStrategy::evaluate(view);
    const CardView card {"aoe-evaluation", type, Suit::Spade, 1, "AOE", {}, 0, 0};
    int total = 0;
    for (const auto& target : view.players) {
        if (!target.alive || target.id == view.selfPlayerId) continue;
        const bool renegadeStrategyOverride = view.selfIdentity == PlayerIdentity::Renegade
            && AIIdentityStrategy::targetModifier(strategy, target.id, type) > 0;
        if (objective.avoidsHostileTarget(target.id) && !renegadeStrategyOverride) {
            total -= 180 + std::max(0, 3 - target.hp) * 35; continue;
        }
        const auto threat = AIThreatEvaluator::evaluate(view, target, false, slashFamilyCount);
        int value = 25 + threat.threat / 2 + threat.killOpportunity
            + std::max(0, 3 - int(target.handCardCount)) * 14 - defenseAgainst(card, target);
        if (target.hp == 1) value += 55;
        total += value + objective.hostileTargetBias(target.id);
    }
    return total + AIIdentityStrategy::cardUseModifier(view, strategy, type);
}

int AITargetEvaluator::globalCardValue(const PlayerViewState& view, CardType type) noexcept
{
    const AIIdentityObjective objective(view);
    const auto strategy = AIIdentityStrategy::evaluate(view);
    const auto self = std::find_if(view.players.begin(), view.players.end(), [&](const auto& player) { return player.id == view.selfPlayerId; });
    if (self == view.players.end()) return -10000;
    if (type == CardType::PeachGarden) {
        int value = (self->maxHp - self->hp) * 90;
        for (const auto& player : view.players) {
            if (!player.alive || player.id == view.selfPlayerId || player.hp >= player.maxHp) continue;
            value += objective.protectsTargetFromHostileAction(player.id) ? 185 : -45;
        }
        return value + AIIdentityStrategy::cardUseModifier(view, strategy, type);
    }
    if (type == CardType::Harvest) {
        int value = std::max(0, 5 - int(view.ownHand.size())) * 28;
        for (const auto& player : view.players) {
            if (!player.alive || player.id == view.selfPlayerId) continue;
            value -= AIThreatEvaluator::threatScore(view, player) / 8;
        }
        return value + AIIdentityStrategy::cardUseModifier(view, strategy, type);
    }
    return 0;
}

int AITargetEvaluator::borrowedSwordPairScore(const PlayerViewState& view, const PublicPlayerView& attacker,
                                              const PublicPlayerView& victim, int slashFamilyCount) noexcept
{
    const AIIdentityObjective objective(view);
    const auto strategy = AIIdentityStrategy::evaluate(view);
    const auto blocked = [&](PlayerId id, CardType type) {
        return objective.avoidsHostileTarget(id)
            && !(view.selfIdentity == PlayerIdentity::Renegade
                 && AIIdentityStrategy::targetModifier(strategy, id, type) > 0);
    };
    if (blocked(attacker.id, CardType::BorrowedSword) || blocked(victim.id, CardType::Slash)) return -10000;
    const CardView slash {"borrowed-slash", CardType::Slash, Suit::Spade, 1, "Slash", {}, 1, 1};
    const int victimValue = evaluate(view, slash, victim, true, slashFamilyCount).finalScore();
    const auto* weapon = equipped(attacker, EquipmentSlot::Weapon);
    const int weaponGain = weapon ? 55 + AIThreatEvaluator::equipmentThreat(view, attacker) * 2 : 0;
    const int attackLikelihood = std::min(int(attacker.handCardCount), 5) * 8;
    return victimValue + weaponGain + attackLikelihood + objective.hostileTargetBias(victim.id);
}

std::vector<SelectionOptionId> AITargetEvaluator::chooseTargetResourceSelection(
    const PlayerViewState&, const CardSelectionView& selection)
{
    auto options = selection.options;
    std::stable_sort(options.begin(), options.end(), [](const auto& left, const auto& right) {
        const int leftValue = publicOptionValue(left), rightValue = publicOptionValue(right);
        return leftValue != rightValue ? leftValue > rightValue : left.optionId < right.optionId;
    });
    const int count = std::min(selection.minCount, int(options.size()));
    std::vector<SelectionOptionId> result;
    for (int index = 0; index < count; ++index) result.push_back(options[static_cast<std::size_t>(index)].optionId);
    return result;
}

} // namespace sanguosha::ai
