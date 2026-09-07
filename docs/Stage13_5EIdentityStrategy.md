# Stage 13.5E Identity Advanced Strategy

`AIIdentityStrategy` is the stateless role-policy layer above the existing
tactical, threat, resource, target, and identity-inference evaluators. It is
enabled only for Identity mode and rebuilds its result from the current
filtered `PlayerViewState` on every decision.

## Inputs and privacy

The layer accepts no `GameEngine`, deck, private opponent hand, or server role
table. Known roles come only from identities present in the filtered view;
suspected roles come only from Stage 13.5D's public-event belief output.
Known identity and confidence-scaled inference remain separate inputs.

## Output

`AIIdentityStrategyResult` records a dynamic phase, strategic intent, public
table-balance estimate, survival/risk/aggression values, response modifiers,
global-card modifiers, and per-target damage/resource/delayed-trick modifiers.
These values are additive: engine legality and the existing tactical layers
remain authoritative.

## Role policies

- Lord preserves defensive resources when pressured, targets likely Rebels,
  avoids likely Loyalists, and transitions toward Renegade in the endgame.
- Loyalist protects and rescues Lord while retaining enough resources to stay
  alive, and pressures likely Rebels.
- Rebel focuses Lord, amplifies legal lethal opportunities, can remove Lord's
  defense first, and reduces pressure on likely Rebel teammates.
- Renegade dynamically estimates which side is stronger from public HP,
  resources, threat, and beliefs. It protects Lord against a premature Rebel
  victory, pressures an over-strong Lord side, preserves itself, and switches
  unconditionally to killing Lord in the final duel.

AOE, Peach Garden, Harvest, Nullification, rescue, delayed tricks, and
Dismantlement/Snatch receive role-aware adjustments. FFA returns a disabled,
zero-modifier result.

Because the strategy has no mutable cache, reconnect reconstructs the same
answer from the same public view, Return to Lobby leaves no strategy state,
and a second game starts from its new public state.
