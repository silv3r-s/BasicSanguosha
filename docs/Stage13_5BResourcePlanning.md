# Stage 13.5B AI Resource Planning

## Scope

Stage 13.5B adds a stateless `AIResourcePlanner` between the immutable
`PlayerViewState` and `BasicAIActionDecider`. It does not inspect `GameEngine`,
opponent hand faces, hidden identities, the draw pile, or future judgments.
Every decision is recomputed from the latest server view after the preceding
action resolves.

The planner separates four related values:

| Value | Meaning |
|---|---|
| `immediateUse` | Benefit of playing the card in the current public state |
| `keep` | General future utility of retaining the card |
| `emergency` | Survival value, strongly increased at one or two HP |
| `reserve` | Value of preserving a last or scarce response/resource copy |

Central thresholds are `LowHpThreshold`, `EmergencyResourceThreshold`, and
`ReserveNullificationThreshold`. Equal values use stable card ID, selection
option ID, seat, and player ID tie-breaks.

## Implemented behavior

- Peach, Dodge, Nullification, and Wine have health-sensitive emergency and
  reserve values. Public Stage 13.5A threat raises survival-resource value.
- A safe one-HP wound does not automatically spend the only Peach; low HP or
  high public pressure can make healing the highest immediate action.
- The last Nullification may be kept for a low-impact loss, while delayed
  tricks, damage tricks, low HP, or redundant copies justify use. Each chain
  request is evaluated again from its new view.
- Wine is played only when a legal Slash and a strong public kill opportunity
  coexist. Legal Wine self-rescue remains ahead of Peach.
- Ordinary Slash is preferred without elemental synergy. Fire/Thunder Slash
  gains value from public chain, Vine, and propagation opportunities. Extra
  Slash/Dodge copies receive diminishing keep value; Crossbow raises Slash
  value.
- Serpent Spear chooses the two lowest-value cards and avoids Peach plus
  Nullification outside an emergency response.
- Axe compares forced-hit benefit with the two cheapest legal costs. Ice Sword,
  Double Sword, Kirin Bow, Harvest, and Fire Attack selections use only public
  option metadata and current resources. Mandatory engine minima are always
  honored.
- Equipment is played only for a meaningful replacement improvement. Low HP
  raises armor/defensive-mount value, and wounded Silver Lion replacement
  includes its public leave-play healing value. The engine remains authoritative
  for the actual effect.
- Discard selects the lowest current resource cost, retaining emergency cards
  and shedding redundant Slash/Dodge first. Ex Nihilo and other high immediate
  actions are selected before lower-value attacks, with a fresh view used after
  every submitted action.

Identity objectives remain in `AIIdentityObjective`; the resource planner does
not infer roles. Free-for-all receives no identity adjustment. Threat scoring
continues to come from `AIThreatEvaluator` rather than being duplicated.

## Verification

`BasicSanguoshaAITests` covers low-HP reserves, redundancy, Nullification,
Wine attack/self-rescue, elemental Slash, Serpent Spear, Axe, equipment and
Silver Lion replacement, Harvest, Fire Attack costs, discard ordering,
action ordering, hidden-hand/identity independence, and deterministic ties.
The same executable runs complete 4-AI FFA, 5-AI Identity, 8-AI Identity, and
five 1-human + 7-AI Identity simulations with watchdog, rejected-action, stale
action, and post-GameOver checks.

Stage 13.5C advanced target selection is explicitly out of scope.
