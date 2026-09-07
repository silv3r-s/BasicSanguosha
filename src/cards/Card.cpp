#include "cards/Card.h"

#include <utility>

namespace sanguosha {

Card::Card(CardId id, std::string name, CardType type, Suit suit, int rank, std::optional<EquipmentData> equipmentData,
    std::string variant, std::string horseName)
    : id_(std::move(id)), name_(std::move(name)), type_(type), suit_(suit), rank_(rank), equipmentData_(equipmentData),
      variant_(std::move(variant)), horseName_(std::move(horseName))
{
}

const CardId &Card::id() const noexcept { return id_; }
const std::string &Card::name() const noexcept { return name_; }
CardType Card::type() const noexcept { return type_; }
Suit Card::suit() const noexcept { return suit_; }
int Card::rank() const noexcept { return rank_; }
const std::optional<EquipmentData> &Card::equipmentData() const noexcept { return equipmentData_; }
const std::string &Card::variant() const noexcept { return variant_; }
const std::string &Card::horseName() const noexcept { return horseName_; }

std::optional<EquipmentSlot> equipmentSlotForCard(CardType type)
{
    switch (type) {
    case CardType::Weapon: return EquipmentSlot::Weapon;
    case CardType::Armor: return EquipmentSlot::Armor;
    case CardType::OffensiveHorse: return EquipmentSlot::OffensiveHorse;
    case CardType::DefensiveHorse: return EquipmentSlot::DefensiveHorse;
    default: return std::nullopt;
    }
}

std::string suitDisplay(Suit suit)
{
    switch (suit) {
    case Suit::Spade: return "Spade";
    case Suit::Heart: return "Heart";
    case Suit::Club: return "Club";
    case Suit::Diamond: return "Diamond";
    }
    return "Unknown";
}

std::string rankDisplay(int rank)
{
    if (rank == 1) return "A";
    if (rank == 11) return "J";
    if (rank == 12) return "Q";
    if (rank == 13) return "K";
    return std::to_string(rank);
}

} // namespace sanguosha
