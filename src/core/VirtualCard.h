#pragma once

#include <string>
#include <vector>

#include "cards/Card.h"

namespace sanguosha {

// A transient card-use description. It never enters Deck storage.
struct VirtualCard {
    CardType effectiveType {CardType::Slash};
    std::vector<CardId> subcards;
    std::string sourceEquipment;
    bool isVirtual {true};
};

} // namespace sanguosha
