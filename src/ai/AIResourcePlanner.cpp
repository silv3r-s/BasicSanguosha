#include "ai/AIResourcePlanner.h"

#include <algorithm>
#include <numeric>

namespace sanguosha::ai {
namespace {
const PublicPlayerView* selfPlayer(const PlayerViewState& view) noexcept
{
    const auto found = std::find_if(view.players.begin(), view.players.end(), [&](const auto& player) {
        return player.id == view.selfPlayerId;
    });
    return found == view.players.end() ? nullptr : &*found;
}

int countType(const PlayerViewState& view, CardType type) noexcept
{
    return int(std::count_if(view.ownHand.begin(), view.ownHand.end(), [&](const auto& card) {
        return type == CardType::Slash ? isSlashCard(card.type) : card.type == type;
    }));
}

int highestPublicThreat(const PlayerViewState& view) noexcept
{
    int result = 0;
    for (const auto& player : view.players) {
        if (player.alive && player.id != view.selfPlayerId)
            result = std::max(result, AIThreatEvaluator::threatScore(view, player));
    }
    return result;
}

bool hasEquipped(const PublicPlayerView* player, EquipmentSlot slot, const char* name = nullptr) noexcept
{
    if (!player) return false;
    const auto& card = player->equipment.cardsBySlot[static_cast<std::size_t>(slot)];
    return card && (!name || card->displayName == name);
}

int equipmentCardValue(const CardView& card, int hp) noexcept
{
    if (card.type == CardType::Armor) {
        int value = card.displayName == "白银狮子" ? 75 : card.displayName == "八卦阵" ? 72
            : card.displayName == "仁王盾" ? 68 : card.displayName == "藤甲" ? 62 : 58;
        if (hp <= AIResourcePlanner::LowHpThreshold) value += 28;
        return value;
    }
    if (card.type == CardType::Weapon) {
        if (card.displayName == "诸葛连弩") return 72;
        if (card.displayName == "贯石斧" || card.displayName == "青龙偃月刀") return 68;
        if (card.displayName == "丈八蛇矛" || card.displayName == "麒麟弓") return 64;
        if (card.displayName == "银月枪") return 48;
        return 55 + (card.attackRange ? std::max(0, *card.attackRange - 1) * 3 : 0);
    }
    if (card.type == CardType::DefensiveHorse) return hp <= AIResourcePlanner::LowHpThreshold ? 62 : 47;
    if (card.type == CardType::OffensiveHorse) return 45;
    return 0;
}

std::optional<EquipmentSlot> equipmentSlot(CardType type) noexcept
{
    if (type == CardType::Weapon) return EquipmentSlot::Weapon;
    if (type == CardType::Armor) return EquipmentSlot::Armor;
    if (type == CardType::OffensiveHorse) return EquipmentSlot::OffensiveHorse;
    if (type == CardType::DefensiveHorse) return EquipmentSlot::DefensiveHorse;
    return std::nullopt;
}

CardView optionCard(const CardSelectionOptionView& option)
{
    return {std::to_string(option.optionId), option.cardType.value_or(CardType::Slash), Suit::Spade, 1,
            option.displayName, {}, 0, 0};
}
}

AICardResourceValue AIResourcePlanner::evaluate(const PlayerViewState& view, const CardView& card,
                                                const PublicPlayerView* target) noexcept
{
    AICardResourceValue value;
    const auto* self = selfPlayer(view);
    const int hp = self ? self->hp : 4;
    const int slashCount = countType(view, CardType::Slash);
    const int dodgeCount = countType(view, CardType::Dodge);
    const int nullificationCount = countType(view, CardType::Nullification);
    const int pressure = highestPublicThreat(view);
    const int survivalPressure = pressure >= 70 ? 22 : pressure >= 55 ? 10 : 0;
    switch (card.type) {
    case CardType::Peach:
        value.keep = 100; value.reserve = 45;
        value.emergency = (hp <= 1 ? 240 : hp == 2 ? 150 : 55) + survivalPressure;
        value.immediateUse = hp >= (self ? self->maxHp : 4) ? 0 : hp <= 1 ? 220 : hp == 2 ? 145
            : survivalPressure >= 22 ? 48 : 0;
        value.keep -= std::max(0, countType(view, CardType::Peach) - 2) * 12;
        break;
    case CardType::Dodge:
        value.keep = 78 + (hp <= 2 ? 65 : 0); value.reserve = dodgeCount == 1 ? 35 : 12;
        value.emergency = (hp <= 1 ? 170 : hp == 2 ? 110 : 45) + survivalPressure;
        value.keep -= std::max(0, dodgeCount - 2) * 15;
        if (hasEquipped(self, EquipmentSlot::Armor, "八卦阵")) value.keep -= 14;
        break;
    case CardType::Nullification:
        value.keep = 82 + (nullificationCount == 1 ? 42 : 0) - std::max(0, nullificationCount - 2) * 18;
        value.reserve = nullificationCount < ReserveNullificationThreshold ? 40 : 10;
        value.emergency = (hp <= 2 ? 95 : 55) + survivalPressure / 2;
        break;
    case CardType::Wine:
        value.keep = 46; value.reserve = 18; value.emergency = hp <= 1 ? 210 : 35;
        value.immediateUse = 0;
        break;
    case CardType::Slash:
    case CardType::FireSlash:
    case CardType::ThunderSlash: {
        const bool elemental = card.type != CardType::Slash;
        value.keep = elemental ? 48 : 38;
        value.keep -= std::max(0, slashCount - 1) * (hasEquipped(self, EquipmentSlot::Weapon, "诸葛连弩") ? 2 : 9);
        if (hasEquipped(self, EquipmentSlot::Weapon, "诸葛连弩")) value.keep += 24;
        value.reserve = slashCount == 1 ? 25 : 5;
        value.immediateUse = elemental ? 30 : 42;
        if (target && card.type == CardType::FireSlash) {
            const auto& armor = target->equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)];
            if (target->chained || (armor && armor->displayName == "藤甲")) value.immediateUse += 42;
        }
        if (target && card.type == CardType::ThunderSlash && target->chained) value.immediateUse += 38;
        break;
    }
    case CardType::ExNihilo: value.keep = 88; value.immediateUse = 145; value.reserve = 15; break;
    case CardType::Duel: value.keep = 58; value.immediateUse = target && target->hp <= 2 ? 82 : 48; break;
    case CardType::Dismantlement: value.keep = 66; value.immediateUse = 62; break;
    case CardType::Snatch: value.keep = 68; value.immediateUse = 65; break;
    case CardType::Indulgence: case CardType::SupplyShortage: value.keep = 64; value.immediateUse = 60; break;
    case CardType::BarbarianInvasion: case CardType::ArrowBarrage: value.keep = 52; value.immediateUse = 45; break;
    case CardType::Harvest: value.keep = 72; value.immediateUse = 80; break;
    case CardType::PeachGarden: value.keep = 58; value.immediateUse = hp < (self ? self->maxHp : 4) ? 72 : 38; break;
    case CardType::FireAttack: value.keep = 62; value.immediateUse = target && target->hp <= 2 ? 82 : 55; break;
    case CardType::IronChain: value.keep = 45; value.immediateUse = 42; break;
    case CardType::BorrowedSword: value.keep = 55; value.immediateUse = 58; break;
    case CardType::Lightning: value.keep = 42; value.immediateUse = 35; break;
    case CardType::Weapon: case CardType::Armor: case CardType::OffensiveHorse: case CardType::DefensiveHorse:
        value.keep = equipmentCardValue(card, hp);
        value.immediateUse = equipmentReplacementValue(view, card);
        value.reserve = value.immediateUse == 0 ? 12 : 0;
        break;
    }
    return value;
}

int AIResourcePlanner::keepValue(const PlayerViewState& view, const CardView& card) noexcept
{
    const auto value = evaluate(view, card);
    return value.keep + value.reserve + value.emergency;
}

int AIResourcePlanner::immediateUseValue(const PlayerViewState& view, const CardView& card,
                                         const PublicPlayerView* target) noexcept
{
    return evaluate(view, card, target).immediateUse;
}

int AIResourcePlanner::emergencyValue(const PlayerViewState& view, const CardView& card) noexcept
{
    return evaluate(view, card).emergency;
}

int AIResourcePlanner::responseCost(const PlayerViewState& view, const CardView& card) noexcept
{
    return keepValue(view, card);
}

int AIResourcePlanner::equipmentReplacementValue(const PlayerViewState& view, const CardView& card) noexcept
{
    const auto slot = equipmentSlot(card.type);
    const auto* self = selfPlayer(view);
    if (!slot || !self) return 0;
    int incoming = equipmentCardValue(card, self->hp);
    const auto& current = self->equipment.cardsBySlot[static_cast<std::size_t>(*slot)];
    if (card.type == CardType::OffensiveHorse) {
        const int newlyReachable = int(std::count_if(view.players.begin(), view.players.end(), [&](const auto& player) {
            return player.alive && player.id != view.selfPlayerId
                && AIThreatEvaluator::publicDistance(view, player.id) == 2;
        }));
        incoming += std::min(newlyReachable, 2) * 8;
    } else if (card.type == CardType::DefensiveHorse) {
        int adjacentThreat = 0;
        for (const auto& player : view.players) {
            if (player.alive && player.id != view.selfPlayerId
                && AIThreatEvaluator::publicDistance(view, player.id) == 1)
                adjacentThreat = std::max(adjacentThreat, AIThreatEvaluator::threatScore(view, player));
        }
        incoming += std::min(adjacentThreat / 8, 12);
    }
    if (!current) return incoming + 18;
    // Standard mounts of the same slot have no intrinsic upgrade.  Keep the
    // spare as a future cost instead of replacing an equivalent active mount.
    if (card.type == CardType::OffensiveHorse || card.type == CardType::DefensiveHorse) return 0;
    const int currentValue = equipmentCardValue(*current, self->hp);
    const int lionExit = current->displayName == "白银狮子" && self->hp < self->maxHp ? 32 : 0;
    const int upgrade = incoming - currentValue + lionExit;
    return upgrade >= 12 ? 55 + upgrade : 0;
}

bool AIResourcePlanner::shouldUseNullification(const PlayerViewState& view) noexcept
{
    if (!view.nullification) return false;
    const int count = countType(view, CardType::Nullification);
    const auto* self = selfPlayer(view);
    const auto type = view.nullification->trickType;
    const bool severe = type == CardType::Indulgence || type == CardType::SupplyShortage
        || type == CardType::Duel || type == CardType::FireAttack
        || type == CardType::BarbarianInvasion || type == CardType::ArrowBarrage;
    if (severe || (self && self->hp <= LowHpThreshold)) return true;
    return count >= ReserveNullificationThreshold;
}

bool AIResourcePlanner::shouldUseWine(const PlayerViewState&, int bestKillOpportunity) noexcept
{
    return bestKillOpportunity >= 105;
}

std::vector<CardId> AIResourcePlanner::serpentSpearMaterials(const PlayerViewState& view, bool emergencyResponse)
{
    auto cards = view.ownHand;
    std::stable_sort(cards.begin(), cards.end(), [&](const auto& left, const auto& right) {
        const int leftValue = keepValue(view, left), rightValue = keepValue(view, right);
        return leftValue != rightValue ? leftValue < rightValue : left.id < right.id;
    });
    if (cards.size() < 2) return {};
    const auto critical = [] (CardType type) {
        return type == CardType::Peach || type == CardType::Nullification;
    };
    if (!emergencyResponse && critical(cards[0].type) && critical(cards[1].type)) return {};
    if (!emergencyResponse && emergencyValue(view, cards[0]) + emergencyValue(view, cards[1]) >= EmergencyResourceThreshold) return {};
    return {cards[0].id, cards[1].id};
}

std::vector<SelectionOptionId> AIResourcePlanner::chooseSelection(const PlayerViewState& view,
                                                                  const CardSelectionView& selection)
{
    auto options = selection.options;
    const auto optionValue = [&](const CardSelectionOptionView& option) {
        return option.cardType ? keepValue(view, optionCard(option)) : 25;
    };
    std::stable_sort(options.begin(), options.end(), [&](const auto& left, const auto& right) {
        const int leftValue = optionValue(left), rightValue = optionValue(right);
        const bool harvest = selection.purpose == CardSelectionPurpose::Harvest;
        if (leftValue != rightValue) return harvest ? leftValue > rightValue : leftValue < rightValue;
        return left.optionId < right.optionId;
    });
    // The engine may make normally optional prompts mandatory.  In that case
    // the authoritative minimum always wins over resource conservation.
    if (selection.minCount > 0) {
        const int count = std::min(selection.minCount, int(options.size()));
        std::vector<SelectionOptionId> result;
        for (int index = 0; index < count; ++index) result.push_back(options[static_cast<std::size_t>(index)].optionId);
        return result;
    }
    if (selection.purpose == CardSelectionPurpose::AxeDiscard) {
        if (options.size() < 2) return {};
        const auto target = std::find_if(view.players.begin(), view.players.end(), [&](const auto& player) { return player.id == selection.targetId; });
        const int benefit = target == view.players.end() ? 0
            : AIThreatEvaluator::killOpportunityScore(view, *target, true, countType(view, CardType::Slash)) + 55;
        if (benefit <= optionValue(options[0]) + optionValue(options[1])) return {};
        return {options[0].optionId, options[1].optionId};
    }
    if (selection.purpose == CardSelectionPurpose::FireAttackDiscard) {
        if (options.empty()) return {};
        const auto target = std::find_if(view.players.begin(), view.players.end(), [&](const auto& player) { return player.id == selection.targetId; });
        const int benefit = target != view.players.end() && target->hp <= 2 ? 120 : 70;
        return optionValue(options.front()) < benefit ? std::vector<SelectionOptionId> {options.front().optionId} : std::vector<SelectionOptionId> {};
    }
    if (selection.purpose == CardSelectionPurpose::IceSwordPrompt) {
        const auto target = std::find_if(view.players.begin(), view.players.end(), [&](const auto& player) { return player.id == selection.targetId; });
        return target != view.players.end() && target->hp > 1 && AIThreatEvaluator::resourceValue(*target) >= 24 && !options.empty()
            ? std::vector<SelectionOptionId> {options.front().optionId} : std::vector<SelectionOptionId> {};
    }
    if (selection.purpose == CardSelectionPurpose::DoubleSwordDiscard) {
        return !options.empty() && optionValue(options.front()) < 65
            ? std::vector<SelectionOptionId> {options.front().optionId} : std::vector<SelectionOptionId> {};
    }
    if ((selection.purpose == CardSelectionPurpose::KirinBowMount) && !options.empty()) return {options.front().optionId};
    return {};
}

} // namespace sanguosha::ai
