# Stage 13.5C AI Advanced Target Selection

## Architecture

`AITargetEvaluator` is a stateless action-specific target layer. Its only
inputs are `PlayerViewState`, the current `CardView`, server-supplied legal
target predicates/pairs, Stage 13.5A public threat values, Stage 13.5B resource
costs, and Stage 12B's minimum known identity objective.

`AIAdvancedTargetEvaluation` exposes:

| Component | Purpose |
|---|---|
| TacticalValue | Immediate action-specific pressure |
| ThreatValue | Public sustained threat suppression |
| KillValue | Defense- and hand-aware lethal opportunity |
| ResourceDenialValue | Action-specific public resource removal |
| DefenseBreakValue | Armor/mount context for the current action |
| ElementalValue | Vine and damage-nature interaction |
| ChainValue | Profitable propagation or self/known-ally chain risk |
| IdentityModifier | Only public/self role rules from Stage 12B |
| RiskPenalty | Illegal, wasteful, friendly, or unaffordable choices |

`AITargetSetEvaluation` scores the complete legal set, including Iron Chain
and Halberd combinations. Equal targets use seat then player ID; equal sets use
their sorted `(seat, playerId)` vectors. No score persists across actions.

## Action-specific behavior

- Slash distinguishes lethal opportunity, public hand defense, Bagua, Renwang,
  Vine, defensive mounts, Guding Blade, and normal/fire/thunder nature.
- Fire and Thunder damage value public chained propagation. Self-chain and
  known-Lord propagation are explicit penalties. Normal damage receives no
  chain bonus.
- Duel rewards own Slash-family depth and low opponent public hand count.
- Dismantlement and Snatch interpret hand count, equipment threat, defense,
  resources, and delayed tricks differently. Their follow-up selection favors
  dangerous visible equipment and does not blindly remove an enemy's
  Indulgence or Supply Shortage. Hidden hand options remain anonymous.
- Delayed tricks favor high-threat/high-action-value targets.
- Fire Attack requires a non-empty public target hand, includes the cheapest
  visible own payment cost, Vine value, propagation, and self-chain risk.
- Iron Chain compares all legal zero-to-two target sets, can create a profitable
  competitor chain, can unlink self/known allies, and otherwise legally recasts.
- AOE sums every public target with distinct Savage Assault and Arrow Barrage
  response estimates. Non-positive totals are skipped; known Lord risk is
  subtracted and FFA has no identity adjustment.
- Peach Garden and Harvest compare own shortage/healing with public competitor
  benefit. Borrowed Sword scores `(weapon holder, victim)` as one pair.
- Halberd evaluates complete one-to-three target combinations. Limited combo
  ordering raises public defense removal before a following Slash and chain
  setup before elemental damage; the server view is reread after each action.
- Wine keeps Stage 13.5B's use gate, avoids obvious naked-one-HP overkill, and
  uses the advanced Slash evaluator for the eventual victim.

## Information and authority boundaries

The evaluator never reads `GameEngine`, target hand faces, hidden identities,
future deck cards, or unrevealed judgments. Legal distance, target count,
equipment rules, and Borrowed Sword pairs remain authoritative server inputs.
Every submitted action is still validated by `GameSession`/`GameEngine`.

Test-only rejected-action diagnostics write to stderr and never enter the
formal player log. Stage 13.5D identity inference is explicitly out of scope.
