# Stage 13 AI Full Functional Coverage

Authority: current `src/`, the standard deck definition, and the 26 registered
CTest targets. `SUPPORTED` means the AI has a legal production action path and
the path is exercised directly or by a focused engine/session regression test.
No historical feature list is treated as implementation evidence.

The AI decision boundary is deliberately limited to `PlayerViewState`, legal
response/selection candidates, and session legality callbacks. It does not read
another hand, a hidden identity, the draw pile, an unrevealed judgment, or a
server-side card behind an opaque selection option.

## Card and interaction matrix

| Area | Status | Evidence / boundary |
| --- | --- | --- |
| Slash / Fire Slash / Thunder Slash | SUPPORTED | One Slash-family legality path is used for active play; all three are accepted for Slash-family responses. |
| Dodge | SUPPORTED | Response candidates are selected only from the private legal list; empty list produces Pass. |
| Peach / Wine | SUPPORTED | Active self-heal/buff, Peach rescue, and legal Wine self-rescue are covered. |
| Ex Nihilo | SUPPORTED | Active use is followed by a fresh controller dispatch after cards are drawn. |
| Dismantlement | SUPPORTED | Legal target plus opaque hand/equipment/judgment option selection; hidden hand identity is never exposed. |
| Snatch | SUPPORTED | Authoritative distance/target legality plus opaque second selection. |
| Duel | SUPPORTED | Active use and repeated Slash/pass dispatch, including Slash variants and Serpent Spear. |
| Savage Assault | SUPPORTED | Scoped Nullification, Slash-family response/pass, and target progression. |
| Archery Attack | SUPPORTED | Scoped Nullification, Dodge response/pass, and target progression. |
| Peach Garden | SUPPORTED | Active multi-target resolution and scoped Nullification are engine-owned; AI does not invent an extra response. |
| Harvest | SUPPORTED | Scoped Nullification per recipient and selection from the public pool; a nullified recipient cannot stall the next one. |
| Borrowed Sword | SUPPORTED | Complete legal holder/target pairs come from `GameSession`; real or Serpent Spear Slash and Pass both resume correctly. |
| Fire Attack | SUPPORTED | Target reveal and matching-suit discard use offered options; optional empty discard safely declines; fire/chain damage resumes. |
| Iron Chain | SUPPORTED | Legal target selection and recast are session-authoritative; chained damage does not leave an AI interaction pending. |
| Nullification | SUPPORTED | All responder rounds use request IDs and scoped context; legal card or Pass is deterministic. |
| Indulgence / Supply Shortage / Lightning | SUPPORTED | Active target selection, scoped Nullification, server judgment, and phase continuation are covered. Lightning death/rescue plus chain continuation is a Stage 13 regression. |
| Judgment | SUPPORTED | AI never chooses the judgment card and acts only if a real scoped response exists. |
| Dying / rescue / death | SUPPORTED | Self/other rescue, Peach/Wine/Pass, multi-rescuer progression, death, and GameOver controller shutdown are covered. |
| Normal discard | SUPPORTED | Exact required count is chosen from the latest private hand snapshot. |

## Equipment matrix

| Equipment | Status | AI functional boundary |
| --- | --- | --- |
| Crossbow | SUPPORTED | Equip/replace plus authoritative unlimited-Slash legality. |
| Qinggang Sword | SUPPORTED | Equip/replace; ignore-armor effect is automatic. |
| Double Sword | SUPPORTED | Optional trigger selection and discard/pass continuation. |
| Ice Sword | SUPPORTED | Activate/pass plus the required one/two opaque discard selections. |
| Guding Blade | SUPPORTED | Equip/replace; empty-hand bonus is automatic. |
| Axe | SUPPORTED | Optional exact-two hand/equipment cost selection or legal decline. |
| Qinglong Blade | SUPPORTED | Follow-up Slash-family response or Pass. |
| Serpent Spear | SUPPORTED | Exactly two distinct owned hand cards for active, Duel, Savage Assault, and Borrowed Sword Slash paths. |
| Halberd | SUPPORTED | Engine supplies legal multi-target count; ordinary AI use remains legal. |
| Vermilion Fan | SUPPORTED | Equip/replace; normal Slash conversion and chain effects are automatic. |
| Kirin Bow | SUPPORTED | Offered mount option selection or legal decline. |
| Silver Moon Spear triggered skill | NOT_APPLICABLE | The card is in the current deck and can be equipped/replaced, but its separate triggered skill is intentionally absent from current formal rules (`EquipmentSealTests` excludes it). |
| Eight Trigrams | SUPPORTED | Automatic judgment completes without a second Dodge response. |
| Renwang Shield | SUPPORTED | Automatic black-Slash prevention; Qinggang bypass remains authoritative. |
| Vine Armor | SUPPORTED | Automatic normal-Slash/AOE prevention and fire amplification resume normally. |
| Silver Lion | SUPPORTED | Automatic damage cap and leave-play heal; controller resumes from the new view. |
| All +1 / -1 mounts | SUPPORTED | Generic equip/replace; target distance is recalculated by the engine for every action. |

## Enum and lifecycle closure

| Area | Status | Evidence |
| --- | --- | --- |
| Response coverage | SUPPORTED | Every current value is enumerated in `AITests`: Dodge, Slash, PeachRescue, BorrowedSwordSlash, Nullification, QinglongSlash. |
| Selection coverage | SUPPORTED | Every current `CardSelectionPurpose` is enumerated: Dismantlement, Snatch, Harvest, FireAttackReveal, FireAttackDiscard, AxeDiscard, KirinBowMount, DoubleSwordDiscard, IceSwordPrompt, IceSwordDiscard. Only offered option IDs are returned. |
| Duplicate/stale protection | SUPPORTED | Controller keys responses/selections by request ID, suppresses a rejected key, clears an accepted key for a fresh view, and prevents duplicate queued scheduling. Simulations require zero rejected actions. |
| Unsupported official interaction | NONE | All current response and selection enum values have a deterministic path. There is no separate interaction enum in the current model. |
| FFA | SUPPORTED | Deterministic 4-AI game reaches natural GameOver through production actions. |
| Identity | SUPPORTED | Stage 12B privacy/objective tests plus deterministic 5-AI and 8-AI natural GameOver simulations. No Stage 13.5 inference is added. |
| Human + AI | SUPPORTED | 1 human-driver + 7 production AI controllers use the same public `GameSession` action APIs; five consecutive complete games pass. |
| Reconnect | SUPPORTED | Human reconnect/grace tests retain AI controllers and restore only the correct private interaction. |
| GameOver | SUPPORTED | Controllers stop on the GameOver view; simulation asserts zero later actions. |
| Return to Lobby / second game | SUPPORTED | Existing lifecycle tests clear/recreate controllers and cover Identity/FFA transitions without stale state. |
| Privacy / no cheating | SUPPORTED | Permuting server-only hidden identities while keeping serialized AI views identical cannot change the chosen action; hidden selection options carry opaque IDs only. |
| Deadlock watchdog | SUPPORTED | Complete-game harness tracks actions, turn, current player, phase, response request ID, and selection request ID; one second without authoritative progress fails with recent logs. |

## Deterministic complete-game results

The continuation deck contains ordinary Slash cards added through the test deck
API. Seats draw and use them through normal rules. The harness never changes HP,
hands, phases, pending interactions, or winners, and never bypasses request IDs.

| Simulation | Result |
| --- | --- |
| 4 AI FreeForAll | PASS, natural GameOver (16 turns) |
| 5 AI Identity | PASS, natural GameOver (19 turns) |
| 8 AI Identity | PASS, natural GameOver (41 turns) |
| 1 human + 7 AI Identity | 5/5 PASS, natural GameOver (23, 35, 23, 20, 38 turns) |

Stage 13 is functional closure only. Threat search, suspicion, inference, MCTS,
LLM/API integration, and other intelligence upgrades remain outside this stage.
