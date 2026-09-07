#include "ai/AIThreatEvaluator.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

namespace sanguosha::ai {
namespace {
const CardView* equipped(const PublicPlayerView& player, EquipmentSlot slot) noexcept
{
    const auto& card = player.equipment.cardsBySlot[static_cast<std::size_t>(slot)];
    return card ? &*card : nullptr;
}

int baseWeaponThreat(const CardView& weapon, int handCount) noexcept
{
    if (weapon.displayName == "诸葛连弩") return 8 + std::min(handCount, 6) * 2;
    if (weapon.displayName == "贯石斧") return 14;
    if (weapon.displayName == "青龙偃月刀") return 13;
    if (weapon.displayName == "方天画戟") return 12;
    if (weapon.displayName == "古锭刀") return 11;
    if (weapon.displayName == "朱雀羽扇") return 11;
    if (weapon.displayName == "麒麟弓") return 10;
    if (weapon.displayName == "丈八蛇矛") return 10;
    if (weapon.displayName == "银月枪") return 7; // Equipment body only; triggered skill is N/A.
    return 8 + (weapon.attackRange ? std::max(0, *weapon.attackRange - 1) : 0);
}

bool hasArmor(const PublicPlayerView& player, const char* name) noexcept
{
    const auto* armor = equipped(player, EquipmentSlot::Armor);
    return armor && armor->displayName == name;
}
}

int AIThreatEvaluator::publicDistance(const PlayerViewState& view, PlayerId targetId) noexcept
{
    std::vector<const PublicPlayerView*> alive;
    for (const auto& player : view.players) if (player.alive) alive.push_back(&player);
    std::stable_sort(alive.begin(), alive.end(), [](const auto* left, const auto* right) {
        return left->seat != right->seat ? left->seat < right->seat : left->id < right->id;
    });
    const auto source = std::find_if(alive.begin(), alive.end(), [&](const auto* player) { return player->id == view.selfPlayerId; });
    const auto target = std::find_if(alive.begin(), alive.end(), [&](const auto* player) { return player->id == targetId; });
    if (source == alive.end() || target == alive.end() || source == target) return std::numeric_limits<int>::max();
    const int direct = std::abs(int(std::distance(alive.begin(), source)) - int(std::distance(alive.begin(), target)));
    int distance = std::min(direct, int(alive.size()) - direct);
    if (equipped(**source, EquipmentSlot::OffensiveHorse)) --distance;
    if (equipped(**target, EquipmentSlot::DefensiveHorse)) ++distance;
    return std::max(1, distance);
}

int AIThreatEvaluator::equipmentThreat(const PlayerViewState& view, const PublicPlayerView& target) noexcept
{
    if (!target.alive) return 0;
    int score = 0;
    if (const auto* weapon = equipped(target, EquipmentSlot::Weapon)) {
        score += baseWeaponThreat(*weapon, int(target.handCardCount));
        if (weapon->displayName == "古锭刀") {
            if (std::any_of(view.players.begin(), view.players.end(), [&](const auto& player) {
                    return player.alive && player.id != target.id && player.handCardCount == 0;
                })) score += 6;
        } else if (weapon->displayName == "朱雀羽扇") {
            if (std::any_of(view.players.begin(), view.players.end(), [&](const auto& player) {
                    return player.alive && player.id != target.id && (player.chained || hasArmor(player, "藤甲"));
                })) score += 7;
        } else if (weapon->displayName == "麒麟弓") {
            if (std::any_of(view.players.begin(), view.players.end(), [&](const auto& player) {
                    return player.alive && player.id != target.id
                        && (equipped(player, EquipmentSlot::OffensiveHorse) || equipped(player, EquipmentSlot::DefensiveHorse));
                })) score += 5;
        }
    }
    if (equipped(target, EquipmentSlot::OffensiveHorse)) score += 5;
    if (equipped(target, EquipmentSlot::DefensiveHorse)) score += 3;
    return score;
}

int AIThreatEvaluator::defenseScore(const PublicPlayerView& target) noexcept
{
    if (!target.alive) return 0;
    int score = 0;
    if (const auto* armor = equipped(target, EquipmentSlot::Armor)) {
        if (armor->displayName == "八卦阵") score += 16;
        else if (armor->displayName == "仁王盾") score += 14;
        else if (armor->displayName == "藤甲") score += target.chained ? 7 : 13;
        else if (armor->displayName == "白银狮子") score += 15;
        else score += 9;
    }
    if (equipped(target, EquipmentSlot::DefensiveHorse)) score += 7;
    score += std::min(int(target.handCardCount), 6) * 2;
    return score;
}

int AIThreatEvaluator::resourceValue(const PublicPlayerView& target) noexcept
{
    if (!target.alive) return 0;
    int equipmentCount = 0;
    for (const auto& card : target.equipment.cardsBySlot) if (card) ++equipmentCount;
    return std::min(int(target.handCardCount), 8) * 6 + equipmentCount * 10 + int(target.judgmentCards.size()) * 3;
}

AIThreatBreakdown AIThreatEvaluator::evaluateThreat(const PlayerViewState& view, const PublicPlayerView& target) noexcept
{
    AIThreatBreakdown result;
    if (!target.alive) return result;
    result.health = std::min(std::max(target.hp, 0), 5) * 6 + std::min(std::max(target.maxHp, 0), 6) * 2;
    const int handCount = int(target.handCardCount);
    result.hand = std::min(handCount, 6) * 5 + std::min(std::max(handCount - 6, 0), 4) * 2;
    result.equipment = equipmentThreat(view, target) + defenseScore(target) / 3;
    if (const auto* weapon = equipped(target, EquipmentSlot::Weapon)) {
        result.reach = weapon->attackRange ? std::max(0, *weapon->attackRange - 1) * 4 : 0;
        if (weapon->displayName == "诸葛连弩" && handCount >= 4) result.burst += 10;
        if ((weapon->displayName == "贯石斧" || weapon->displayName == "青龙偃月刀") && handCount >= 3) result.burst += 6;
    }
    if (target.chained) result.restriction -= 3;
    for (const auto& judgment : target.judgmentCards) {
        if (judgment.type == CardType::Indulgence) result.restriction -= 12;
        else if (judgment.type == CardType::SupplyShortage) result.restriction -= 8;
        else if (judgment.type == CardType::Lightning) result.restriction -= 3;
    }
    return result;
}

int AIThreatEvaluator::threatScore(const PlayerViewState& view, const PublicPlayerView& target) noexcept
{
    return evaluateThreat(view, target).total();
}

int AIThreatEvaluator::threatScore(const PublicPlayerView& target) noexcept
{
    PlayerViewState context {target.id, 0, target.id, Phase::Play};
    context.players = {target};
    return threatScore(context, target);
}

int AIThreatEvaluator::killOpportunityScore(const PlayerViewState& view, const PublicPlayerView& target,
                                            bool hasLegalAttack, int slashFamilyCount) noexcept
{
    if (!target.alive) return -10000;
    const int hp = std::clamp(target.hp, 0, 5);
    const int hand = std::min(int(target.handCardCount), 8);
    int score = (5 - hp) * 18 + (hp == 1 ? 32 : 0) + (target.dying ? 50 : 0);
    score += (4 - std::min(hand, 4)) * 8 + (hand == 0 ? 18 : 0);
    if (!equipped(target, EquipmentSlot::Armor)) score += 10;
    score -= defenseScore(target);
    const int distance = publicDistance(view, target.id);
    if (hasLegalAttack) score += 18;
    else if (distance != std::numeric_limits<int>::max()) score -= std::min(distance, 5) * 3;
    score += std::min(std::max(slashFamilyCount - 1, 0), 3) * 5;
    return score;
}

AITargetEvaluation AIThreatEvaluator::evaluate(const PlayerViewState& view, const PublicPlayerView& target,
                                               bool hasLegalAttack, int slashFamilyCount) noexcept
{
    return {threatScore(view, target), killOpportunityScore(view, target, hasLegalAttack, slashFamilyCount),
            defenseScore(target), equipmentThreat(view, target), resourceValue(target)};
}

int AIThreatEvaluator::pressurePriority(const PlayerViewState& view, const PublicPlayerView& target,
                                        AIPressureIntent intent, int tacticalScore,
                                        bool hasLegalAttack, int slashFamilyCount) noexcept
{
    const auto value = evaluate(view, target, hasLegalAttack, slashFamilyCount);
    switch (intent) {
    case AIPressureIntent::Attack:
        return tacticalScore * 3 + value.killOpportunity * 2 + value.threat / 2 - value.defense;
    case AIPressureIntent::Duel:
        return tacticalScore * 2 + value.killOpportunity * 2 + value.threat / 3 - int(target.handCardCount) * 8;
    case AIPressureIntent::Dismantlement:
        return tacticalScore * 2 + value.threat + value.equipmentThreat * 2 + value.defense + value.resourceValue / 2;
    case AIPressureIntent::Snatch:
        return tacticalScore * 2 + value.threat / 2 + value.equipmentThreat + value.resourceValue * 2;
    case AIPressureIntent::DelayedTrick:
        return tacticalScore * 2 + value.threat * 2 + value.resourceValue;
    }
    return tacticalScore;
}

bool AIThreatEvaluator::isHigherThreat(const PlayerViewState& view, const PublicPlayerView& left,
                                       const PublicPlayerView& right) noexcept
{
    const int leftScore = threatScore(view, left), rightScore = threatScore(view, right);
    if (leftScore != rightScore) return leftScore > rightScore;
    return left.seat != right.seat ? left.seat < right.seat : left.id < right.id;
}

bool AIThreatEvaluator::isHigherThreat(const PublicPlayerView& left, const PublicPlayerView& right) noexcept
{
    PlayerViewState view {left.id, 0, left.id, Phase::Play};
    view.players = {left, right};
    return isHigherThreat(view, left, right);
}

} // namespace sanguosha::ai
