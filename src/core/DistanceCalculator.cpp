#include "core/DistanceCalculator.h"

#include <algorithm>
#include <limits>

namespace sanguosha {

int DistanceCalculator::distance(const std::vector<std::shared_ptr<Player>> &players, PlayerId source, PlayerId target) const
{
    std::vector<const Player *> alive;
    for (const auto &player : players) if (player->isAlive()) alive.push_back(player.get());
    auto sourceIt = std::find_if(alive.begin(), alive.end(), [source](const Player *player) { return player->id() == source; });
    auto targetIt = std::find_if(alive.begin(), alive.end(), [target](const Player *player) { return player->id() == target; });
    if (sourceIt == alive.end() || targetIt == alive.end() || source == target) return std::numeric_limits<int>::max();
    const int sourceIndex = static_cast<int>(std::distance(alive.begin(), sourceIt));
    const int targetIndex = static_cast<int>(std::distance(alive.begin(), targetIt));
    const int direct = std::abs(sourceIndex - targetIndex);
    return std::min(direct, static_cast<int>(alive.size()) - direct);
}

} // namespace sanguosha
