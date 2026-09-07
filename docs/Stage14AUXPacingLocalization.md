# Stage 14A UX, AI Pacing, and Localization Closure

Stage 14A keeps game rules and AI decision policy unchanged. It adds a
presentation-only delay before each AI submission, rebuilds the game screen
from authoritative `PlayerViewState`, and routes player-visible battle text
through the existing `GameText` layer.

## AI pacing

- Normal actions: 700 ms.
- Responses and card selections: 1000 ms.
- Pass/no-action transitions: 280 ms.
- Test and simulation builds bypass pacing unless a test explicitly enables
  it with a short override.
- The timer callback re-reads the session view and validates the interaction
  key before submitting. Stop/reset cancels the timer, covering GameOver,
  Return To Lobby, reconnect state changes, and second-game reconstruction.

## Game screen

- Other players use individual panels with public identity, HP, hand count,
  online/control/chained state, four equipment slots, and judgment cards.
- The local player remains in a distinct lower area; only the local hand is
  rendered as card-shaped controls.
- Current player and current phase are separately highlighted.
- The center action banner mirrors the newest localized visible event while
  the full log remains in a narrower right-side pane.
- All panels are recreated from the latest view, so reconnect and a second
  game do not depend on stale widget state.

## Verification

`BasicSanguoshaAITests` covers nonblocking delay, normal/critical/pass timing,
test bypass, stale callback rejection, and GameOver cancellation. Existing
lobby lifecycle coverage verifies AI controller cleanup. `GameTextTests`
covers the Iron Chain resolution regression and Chinese fallback, while
`BasicSanguoshaUXTests` checks card/panel structure and hidden-hand privacy.
