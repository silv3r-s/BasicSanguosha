#include "cards/StandardDeckDefinition.h"

#include <utility>

namespace sanguosha {
namespace {

const char *defaultName(CardType type)
{
    switch (type) {
    case CardType::Slash: return "Slash"; case CardType::FireSlash: return "Fire Slash"; case CardType::ThunderSlash: return "Thunder Slash";
    case CardType::Dodge: return "Dodge"; case CardType::Peach: return "Peach"; case CardType::Wine: return "Analeptic";
    case CardType::ExNihilo: return "Ex Nihilo"; case CardType::Dismantlement: return "Dismantlement"; case CardType::Snatch: return "Snatch";
    case CardType::Duel: return "Duel"; case CardType::BarbarianInvasion: return "Savage Assault"; case CardType::ArrowBarrage: return "Archery Attack";
    case CardType::PeachGarden: return "God Salvation"; case CardType::Harvest: return "Amazing Grace"; case CardType::BorrowedSword: return "Collateral";
    case CardType::Nullification: return "Nullification"; case CardType::Indulgence: return "Indulgence"; case CardType::SupplyShortage: return "Supply Shortage";
    case CardType::Lightning: return "Lightning"; case CardType::IronChain: return "Iron Chain"; case CardType::FireAttack: return "Fire Attack";
    case CardType::Weapon: return "Weapon"; case CardType::Armor: return "Armor"; case CardType::OffensiveHorse: return "Offensive Horse"; case CardType::DefensiveHorse: return "Defensive Horse";
    }
    return "Unknown";
}

void add(std::vector<StandardCardDefinition> &cards, CardType type, Suit suit, int rank, int count = 1,
    const char *name = nullptr, const char *variant = nullptr, std::optional<EquipmentData> equipment = std::nullopt, const char *horseName = nullptr)
{
    for (int i = 0; i < count; ++i) cards.push_back({type, suit, rank, name ? name : defaultName(type), variant ? variant : "", equipment, horseName ? horseName : ""});
}

void addRanks(std::vector<StandardCardDefinition> &cards, CardType type, Suit suit, std::initializer_list<std::pair<int, int>> ranks)
{
    for (const auto &[rank, count] : ranks) add(cards, type, suit, rank, count);
}
}

const std::vector<StandardCardDefinition> &standardDeckDefinition()
{
    static const std::vector<StandardCardDefinition> cards = [] {
        std::vector<StandardCardDefinition> c;
        // Basic (85)
        addRanks(c, CardType::Slash, Suit::Spade, {{7,1},{8,2},{9,2},{10,2}}); addRanks(c, CardType::Slash, Suit::Club, {{2,1},{3,1},{4,1},{5,1},{6,1},{7,1},{8,2},{9,2},{10,2},{11,2}}); addRanks(c, CardType::Slash, Suit::Heart, {{10,2},{11,1}}); addRanks(c, CardType::Slash, Suit::Diamond, {{6,1},{7,1},{8,1},{9,1},{10,1},{13,1}});
        for (int r : {4,7,10}) add(c, CardType::FireSlash, Suit::Heart, r); for (int r : {4,5}) add(c, CardType::FireSlash, Suit::Diamond, r);
        for (int r : {4,5,6,7,8}) add(c, CardType::ThunderSlash, Suit::Spade, r); for (int r : {5,6,7,8}) add(c, CardType::ThunderSlash, Suit::Club, r);
        addRanks(c, CardType::Dodge, Suit::Heart, {{2,2},{8,1},{9,1},{11,1},{12,1},{13,1}}); addRanks(c, CardType::Dodge, Suit::Diamond, {{2,2},{3,1},{4,1},{5,1},{6,2},{7,2},{8,2},{9,1},{10,2},{11,3}});
        addRanks(c, CardType::Peach, Suit::Heart, {{3,1},{4,1},{5,1},{6,2},{7,1},{8,1},{9,1},{12,1}}); addRanks(c, CardType::Peach, Suit::Diamond, {{2,1},{3,1},{12,1}});
        for (int r : {3,9}) add(c, CardType::Wine, Suit::Spade, r); for (int r : {3,9}) add(c, CardType::Wine, Suit::Club, r); add(c, CardType::Wine, Suit::Diamond, 9);
        // Non-delayed tricks (43)
        for (int r : {3,4,12}) add(c, CardType::Dismantlement, Suit::Spade, r); for (int r : {3,4}) add(c, CardType::Dismantlement, Suit::Club, r); add(c, CardType::Dismantlement, Suit::Heart, 12);
        for (int r : {3,4,11}) add(c, CardType::Snatch, Suit::Spade, r); for (int r : {3,4}) add(c, CardType::Snatch, Suit::Diamond, r);
        add(c, CardType::Duel, Suit::Spade, 1); add(c, CardType::Duel, Suit::Club, 1); add(c, CardType::Duel, Suit::Diamond, 1); add(c, CardType::BorrowedSword, Suit::Club, 12); add(c, CardType::BorrowedSword, Suit::Club, 13);
        for (int r : {7,8,9,11}) add(c, CardType::ExNihilo, Suit::Heart, r); add(c, CardType::Nullification, Suit::Spade, 11); add(c, CardType::Nullification, Suit::Spade, 13); add(c, CardType::Nullification, Suit::Club, 12); add(c, CardType::Nullification, Suit::Club, 13); add(c, CardType::Nullification, Suit::Heart, 1); add(c, CardType::Nullification, Suit::Heart, 13); add(c, CardType::Nullification, Suit::Diamond, 12, 1, nullptr, "EX-Q");
        for (int r : {11,12}) add(c, CardType::IronChain, Suit::Spade, r); for (int r : {10,11,12,13}) add(c, CardType::IronChain, Suit::Club, r); for (int r : {2,3}) add(c, CardType::FireAttack, Suit::Heart, r); add(c, CardType::FireAttack, Suit::Diamond, 12); add(c, CardType::ArrowBarrage, Suit::Heart, 1); add(c, CardType::BarbarianInvasion, Suit::Spade, 7); add(c, CardType::BarbarianInvasion, Suit::Spade, 13); add(c, CardType::BarbarianInvasion, Suit::Club, 7); add(c, CardType::PeachGarden, Suit::Heart, 1); add(c, CardType::Harvest, Suit::Heart, 3); add(c, CardType::Harvest, Suit::Heart, 4);
        // Delayed tricks (7)
        add(c, CardType::Lightning, Suit::Spade, 1); add(c, CardType::Lightning, Suit::Heart, 12, 1, nullptr, "EX-Q"); add(c, CardType::Indulgence, Suit::Spade, 6); add(c, CardType::Indulgence, Suit::Heart, 6); add(c, CardType::Indulgence, Suit::Club, 6); add(c, CardType::SupplyShortage, Suit::Spade, 10); add(c, CardType::SupplyShortage, Suit::Club, 4);
        // Equipment (26): generic equipment CardTypes preserve existing gameplay behavior; names identify each item.
        const auto weapon = [](int range, bool unlimited = false) { return EquipmentData {EquipmentSlot::Weapon, range, unlimited}; };
        add(c, CardType::Weapon, Suit::Club, 1, 1, "诸葛连弩", nullptr, weapon(1, true)); add(c, CardType::Weapon, Suit::Diamond, 1, 1, "诸葛连弩", nullptr, weapon(1, true)); add(c, CardType::Weapon, Suit::Spade, 6, 1, "青釭剑", nullptr, weapon(2)); add(c, CardType::Weapon, Suit::Spade, 2, 1, "雌雄双股剑", nullptr, weapon(2)); add(c, CardType::Weapon, Suit::Spade, 2, 1, "寒冰剑", "EX-2", weapon(2)); add(c, CardType::Weapon, Suit::Spade, 1, 1, "古锭刀", nullptr, weapon(2)); add(c, CardType::Weapon, Suit::Diamond, 5, 1, "贯石斧", nullptr, weapon(3)); add(c, CardType::Weapon, Suit::Spade, 5, 1, "青龙偃月刀", nullptr, weapon(3)); add(c, CardType::Weapon, Suit::Spade, 12, 1, "丈八蛇矛", nullptr, weapon(3)); add(c, CardType::Weapon, Suit::Diamond, 12, 1, "方天画戟", nullptr, weapon(4)); add(c, CardType::Weapon, Suit::Diamond, 1, 1, "朱雀羽扇", nullptr, weapon(4)); add(c, CardType::Weapon, Suit::Heart, 5, 1, "麒麟弓", nullptr, weapon(5)); add(c, CardType::Weapon, Suit::Diamond, 12, 1, "银月枪", "Q-SP", weapon(3));
        const auto armor = EquipmentData {EquipmentSlot::Armor, 0}; add(c, CardType::Armor, Suit::Spade, 2, 1, "八卦阵", nullptr, armor); add(c, CardType::Armor, Suit::Club, 2, 1, "八卦阵", nullptr, armor); add(c, CardType::Armor, Suit::Club, 2, 1, "仁王盾", "EX-2", armor); add(c, CardType::Armor, Suit::Spade, 2, 1, "藤甲", nullptr, armor); add(c, CardType::Armor, Suit::Club, 2, 1, "藤甲", nullptr, armor); add(c, CardType::Armor, Suit::Club, 1, 1, "白银狮子", nullptr, armor);
        const auto defensive = EquipmentData {EquipmentSlot::DefensiveHorse, 0}; const auto offensive = EquipmentData {EquipmentSlot::OffensiveHorse, 0}; add(c, CardType::DefensiveHorse, Suit::Spade, 5, 1, "JueYing", nullptr, defensive, "JueYing"); add(c, CardType::DefensiveHorse, Suit::Heart, 13, 1, "ZhuaHuangFeiDian", nullptr, defensive, "ZhuaHuangFeiDian"); add(c, CardType::DefensiveHorse, Suit::Diamond, 13, 1, "HuaLiu", nullptr, defensive, "HuaLiu"); add(c, CardType::DefensiveHorse, Suit::Club, 5, 1, "DiLu", nullptr, defensive, "DiLu"); add(c, CardType::OffensiveHorse, Suit::Spade, 13, 1, "DaYuan", nullptr, offensive, "DaYuan"); add(c, CardType::OffensiveHorse, Suit::Heart, 5, 1, "ChiTu", nullptr, offensive, "ChiTu"); add(c, CardType::OffensiveHorse, Suit::Diamond, 13, 1, "ZiXing", nullptr, offensive, "ZiXing");
        return c;
    }();
    return cards;
}

std::vector<std::shared_ptr<Card>> createStandardDeckCards()
{
    std::vector<std::shared_ptr<Card>> cards; cards.reserve(standardDeckDefinition().size()); int sequence = 1;
    for (const auto &definition : standardDeckDefinition()) cards.push_back(std::make_shared<Card>("standard-" + std::to_string(sequence++), definition.name, definition.type, definition.suit, definition.rank, definition.equipmentData, definition.variant, definition.horseName));
    return cards;
}

} // namespace sanguosha
