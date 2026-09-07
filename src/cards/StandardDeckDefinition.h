#pragma once

#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "cards/Card.h"

namespace sanguosha {

struct StandardCardDefinition {
    CardType type;
    Suit suit;
    int rank;
    std::string name;
    std::string variant;
    std::optional<EquipmentData> equipmentData;
    std::string horseName;
};

// The sole authoritative definition of the 161-card standard deck.
const std::vector<StandardCardDefinition> &standardDeckDefinition();
std::vector<std::shared_ptr<Card>> createStandardDeckCards();

} // namespace sanguosha
