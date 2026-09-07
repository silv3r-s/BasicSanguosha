#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Player.h"

namespace sanguosha {

enum class CardSelectionPurpose {
    Dismantlement,
    Snatch,
    Harvest,
    FireAttackReveal,
    FireAttackDiscard,
    AxeDiscard,
    KirinBowMount,
    DoubleSwordDiscard,
    IceSwordPrompt,
    IceSwordDiscard
};

enum class CardZone {
    Hand,
    Equipment
};

struct SelectableCard {
    CardId cardId;
    CardZone zone;
    std::optional<EquipmentSlot> equipmentSlot;
    bool hidden {false};
    std::string displayName;
};

struct CardSelectionRequest {
    std::uint64_t requestId;
    CardSelectionPurpose purpose;
    PlayerId requester;
    PlayerId target;
    int minCount {1};
    int maxCount {1};
    bool allowHand {true};
    bool allowEquipment {true};
    std::vector<SelectableCard> selectableCards;
};

} // namespace sanguosha
