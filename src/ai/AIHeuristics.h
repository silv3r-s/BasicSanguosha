#pragma once

#include <algorithm>

#include "session/PlayerViewState.h"

namespace sanguosha::ai {

inline int keepValue(const CardView& card, const PlayerViewState& view)
{
    int value = 20;
    switch (card.type) {
    case CardType::Peach: value = 90; break;
    case CardType::Dodge: value = 65; break;
    case CardType::Nullification: value = 70; break;
    case CardType::Wine: value = 42; break;
    case CardType::ExNihilo: value = 55; break;
    case CardType::FireSlash: case CardType::ThunderSlash: value = 38; break;
    case CardType::Slash: value = 32; break;
    case CardType::Weapon: value = 38; break;
    case CardType::Armor: value = 55; break;
    case CardType::OffensiveHorse: case CardType::DefensiveHorse: value = 35; break;
    default: break;
    }
    const auto self = std::find_if(view.players.begin(), view.players.end(), [&] (const auto& p) { return p.id == view.selfPlayerId; });
    if (self != view.players.end() && self->hp <= 2) {
        if (card.type == CardType::Peach) value += 60;
        if (card.type == CardType::Dodge) value += 35;
        if (card.type == CardType::Armor) value += 20;
    }
    return value;
}

inline int targetValue(const PublicPlayerView& target, const PlayerViewState& view)
{
    if (!target.alive || target.id == view.selfPlayerId) return -10000;
    int score = (5 - std::min(target.hp, 4)) * 20 + (target.handCardCount == 0 ? 18 : 0);
    if (target.handCardCount <= 2) score += 10;
    if (target.chained) score += 8;
    if (target.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Armor)]) score += 12;
    if (target.equipment.cardsBySlot[static_cast<std::size_t>(EquipmentSlot::Weapon)]) score += 8;
    return score;
}

inline int equipmentValue(CardType type) { return type == CardType::Armor ? 60 : type == CardType::Weapon ? 45 : 35; }

} // namespace sanguosha::ai
