#include "core/DamageSystem.h"

namespace sanguosha {

bool DamageSystem::apply(const Damage &damage, Player &target) const
{
    if (damage.amount <= 0 || !target.isAlive()) return false;
    target.receiveDamage(damage.amount);
    return true;
}

} // namespace sanguosha
