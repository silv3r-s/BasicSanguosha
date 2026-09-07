#pragma once

#include "core/Player.h"

namespace sanguosha {

enum class DamageNature { Normal, Fire, Thunder };

struct Damage {
    PlayerId source;
    PlayerId target;
    int amount;
    DamageNature nature {DamageNature::Normal};
    bool ignoreArmor {false};
    bool directSlash {false};
};

class DamageSystem {
public:
    bool apply(const Damage &damage, Player &target) const;
};

} // namespace sanguosha
