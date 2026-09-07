#pragma once

#include <vector>

#include "core/Player.h"

namespace sanguosha {

class DistanceCalculator {
public:
    int distance(const std::vector<std::shared_ptr<Player>> &players, PlayerId source, PlayerId target) const;
};

} // namespace sanguosha
