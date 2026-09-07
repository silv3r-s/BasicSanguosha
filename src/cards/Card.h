#pragma once

#include <optional>
#include <string>

namespace sanguosha {

enum class CardType {
    Slash,
    FireSlash,
    ThunderSlash,
    Dodge,
    Peach,
    Wine,
    ExNihilo,
    Dismantlement,
    Snatch,
    Duel,
    BarbarianInvasion,
    ArrowBarrage,
    PeachGarden,
    Harvest,
    BorrowedSword,
    Nullification,
    Indulgence,
    SupplyShortage,
    Lightning,
    IronChain,
    FireAttack,
    Weapon,
    Armor,
    OffensiveHorse,
    DefensiveHorse
};

constexpr bool isSlashCard(CardType type) noexcept
{
    return type == CardType::Slash || type == CardType::FireSlash || type == CardType::ThunderSlash;
}

enum class Suit {
    Spade,
    Heart,
    Club,
    Diamond
};

enum class EquipmentSlot {
    Weapon,
    Armor,
    OffensiveHorse,
    DefensiveHorse
};

struct EquipmentData {
    EquipmentSlot slot;
    int attackRange {0};
    bool unlimitedSlash {false};
};

using CardId = std::string;

class Card {
public:
    Card(CardId id, std::string name, CardType type, Suit suit, int rank,
        std::optional<EquipmentData> equipmentData = std::nullopt, std::string variant = {}, std::string horseName = {});
    virtual ~Card() = default;

    const CardId &id() const noexcept;
    const std::string &name() const noexcept;
    CardType type() const noexcept;
    Suit suit() const noexcept;
    int rank() const noexcept;
    const std::optional<EquipmentData> &equipmentData() const noexcept;
    const std::string &variant() const noexcept;
    const std::string &horseName() const noexcept;

private:
    CardId id_;
    std::string name_;
    CardType type_;
    Suit suit_;
    int rank_;
    std::optional<EquipmentData> equipmentData_;
    std::string variant_;
    std::string horseName_;
};

std::optional<EquipmentSlot> equipmentSlotForCard(CardType type);

std::string suitDisplay(Suit suit);
std::string rankDisplay(int rank);

} // namespace sanguosha
