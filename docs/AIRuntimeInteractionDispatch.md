# AI Runtime Interaction Dispatch

The engine now emits `GameEventType::InteractionRequested` immediately after
an authoritative `ResponseRequest` or `CardSelectionRequest` has been stored.
The event carries only the requester, responder/target, request ID, and kind;
it contains no card details or other private information.

`GameServer` already subscribes to engine events and uses one guarded queued
`AIController::schedule()` path for every AI seat. This event therefore drives
AI responses and selections after the final interaction state exists, rather
than relying on an earlier `CardUsed` or phase event.

This fixes the common runtime path for Duel, AOE, Dodge, Nullification,
Peach rescue, weapon responses, and all card selections. The timeout remains a
30-second fallback only; it is not part of normal AI response execution.
