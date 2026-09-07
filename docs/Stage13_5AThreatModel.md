# Stage 13.5A AI Threat System

`AIThreatEvaluator` is an independent, deterministic evaluation layer. Its only
state input is the current `PlayerViewState` and a `PublicPlayerView` from that
snapshot. It has no engine/session/server pointer and cannot read hidden hands,
hidden identities, future deck order, unrevealed judgments, or opaque selection
identities. Scores are recalculated per decision; no cross-action cache exists.

## Outputs

| Output | Meaning | Public factors |
| --- | --- | --- |
| `ThreatScore` | Sustained danger if the player remains unchecked | HP/max HP, public hand count, weapon/reach/burst, mounts, armor persistence, chain state, delayed-trick restrictions |
| `EquipmentThreat` | Public offensive equipment pressure | Weapon identity/range, hand-count synergy, visible empty-hand/chained/Vine/mount opportunities, offensive and defensive mounts |
| `DefenseScore` | Expected difficulty of removing the target | Eight Trigrams, Renwang Shield, Vine, Silver Lion, defensive mount, public hand-count estimate |
| `KillOpportunityScore` | Immediate public chance to finish the target | Low HP/dying, low or empty public hand, armor/defense, public distance, legal attack availability, own Slash-family count |
| `ResourceValue` | Public removable/obtainable value | Hand count, equipment count, judgment-zone count |
| `PressurePriority` | Action-specific correction layered on Stage 11 tactics and Stage 12B identity bias | Intent-specific weighted combination of the outputs above |

High HP increases sustained Threat but lowers KillOpportunity. Low HP does not
incorrectly become high sustained Threat merely because it is killable.

## Equipment semantics

- Crossbow gains threat with public hand count.
- Guding Blade gains threat when a public empty-hand target exists.
- Vermilion Fan gains threat when a public chained or Vine target exists.
- Qinglong Blade and Axe gain burst pressure with sufficient public hand count.
- Kirin Bow gains threat when a public mount target exists.
- Other weapons receive a stable base/range value.
- Silver Moon Spear receives equipment-body value only. Its independent trigger
  remains `NOT_APPLICABLE`; no skill is invented.
- Armor contributes primarily to Defense and only a persistence component to
  Threat. Chained Vine has lower defense due to public fire risk.

## Action integration

| Intent | Weighting emphasis |
| --- | --- |
| Slash / offensive targeting | KillOpportunity, then Threat and Stage 11 tactical value, reduced by Defense |
| Duel | KillOpportunity and low public hand count, plus own Slash-family count |
| Dismantlement | Threat, EquipmentThreat, Defense, and public resources |
| Snatch | ResourceValue and EquipmentThreat |
| Delayed trick | Sustained Threat and ResourceValue |
| Savage Assault / Archery Attack | Aggregate public threat and kill opportunity across affected legal opponents |

The complete pressure value chooses among legal targets of one card. A
normalized correction then participates in the existing cross-card candidate
score, so Stage 11 tactics and Stage 12B identity protections are not replaced.
All target legality remains authoritative in `GameSession`/`GameEngine`.

Equal scores use lower seat, then lower player ID. No randomness is involved.

## Verification

`BasicSanguoshaAITests` covers:

- higher hand count increases Threat;
- differentiated dangerous weapons increase EquipmentThreat/Threat;
- strong armor increases Defense and lowers KillOpportunity;
- low HP and empty hand independently increase KillOpportunity;
- high HP does not automatically become the kill target;
- Slash, Dismantlement, Snatch, Duel, AOE, and delayed-trick integration;
- FFA behavior and Stage 12B Identity modifier compatibility;
- hidden-hand and hidden-identity invariance;
- deterministic seat/player-ID tie breaking;
- Stage 13 complete simulations, including 1 human + 7 AI Identity 5/5.

Stage 13.5A intentionally adds no identity inference, suspicion, loyalty,
hostility history, long-term resource planning, Renegade balancing, search tree,
MCTS, LLM, or external API behavior.
