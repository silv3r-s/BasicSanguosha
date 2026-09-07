# Stage 12B Identity AI Functional Coverage

Authoritative scope for this run: Stages 1-12A are complete; finish Stage 12B
only. Existing Stage13/Stage13.5 documents and the existing public tactical
threat term do not authorize advancing the roadmap. No identity inference,
suspicion, faction balancing, search, resource planning or external API added.

## Audit and baseline

The existing `gameModeForPlayerCount` maps 2-4 seats to FreeForAll and 5-8
to Identity. Core validates mode/seat compatibility and formal distributions.
Lobby, HUD, protocol, reconnect, GameOver and self/public identity filtering
were already implemented; they were retained.

Before edits, the existing Release binaries passed the complete CTest inventory:
26/26, 15.85 seconds. The inventory was read from the current CMake registration,
not assumed. `ComplexUiStateTests.cpp` is registered as
`BasicSanguoshaComplexUiTests`; the other requested suites are present.

## Implementation

- Extend the existing `AIIdentityObjective` constructed from filtered
  `PlayerViewState`. It states complete rule victory conditions for all roles.
  No engine pointer or hidden identity/card/deck input enters the policy.
- Retain keep values, equipment values, target tactical scores and existing
  public threat term. Add identity relation bias and explicit protected-target
  filtering so a sole legal Lord target is not accidentally chosen by Loyalist.
- Loyalist protects Lord from direct attacks, both Borrowed Sword endpoints,
  virtual Slash, voluntary attack responses, AOE and visible elemental-chain
  risk. Injured Lord increases Peach Garden value. Legal Peach rescue is active.
- Rebel prefers legal hostile Lord targets and declines Lord rescue; no unknown
  player becomes a presumed teammate. Urgent self-healing precedes attack bias.
- Lord uses ordinary public tactical choices and prioritizes survival. Renegade
  uses an independent conservative policy and a minimal victory rule guard:
  do not attack Lord while another publicly living opponent remains; release
  the guard for the final duel. No faction estimation or balancing is involved.
- Identity Nullification distinguishes harmful/beneficial effects and original
  effect cancellation/restoration by chain parity. Only self and Loyalist's
  public Lord receive protection. Unknown players are not presumed allies.
- Fill public `ResponseView.targetId` for actual Peach Rescue and Borrowed Sword
  responses; prior synthetic tests had populated this field but real rescue
  snapshots did not.
- Add authoritative play/turn legality checks, resume accepted actions within
  the same phase, and wake AI after complete session action settlement. This
  fixes a missing wakeup when a human's final Nullification decline finishes a
  trick without another engine event. Rejected keys remain suppressed.
- FreeForAll bypasses identity policy, even with stray role fields. Shared legal
  dispatch fixes also allow its existing tactical policy to complete turns.
- Identity context is rebuilt locally on each decision. Return to Lobby destroys
  AI controllers; a new game obtains its newly assigned self identity.

## Tests and evidence

`tests/AIIdentityTests.cpp` is compiled into `BasicSanguoshaAITests`:

- All four actual identities, complete victory goals, public Lord, hidden
  Loyalist/Rebel/Renegade absence.
- Swap actual hidden server identities, compare complete encoded player views,
  then compare complete encoded AI decisions with identical legal actions.
- FFA bypass including stray identity data; urgent survival; protected sole
  targets; direct tricks, AOE, Borrowed Sword, virtual Slash; Rebel Lord priority
  and range constraints; Renegade independent policy and final duel.
- Actual engine-driven dying-Lord rescue views and legal Peach responses;
  self Wine rescue, Nullification parity, AOE defense and legal selection values.
- 1 human + 4/5/7 real AI controllers, each completing three full table rounds,
  with normal session submissions and the formal 5/6/8-seat identity counts.
  Uses a deterministic defensive draw stream to retain every seat for the full
  multi-round assertion; this is not a claim of natural full-game win-rate testing.
  Per-run deadline is 10 seconds, interaction timeout is 60 seconds. Checks zero
  rejected/stale actions, no timeout logs, no duplicate TurnEnded settlement,
  and identity filtering throughout progression.
- Actual TCP human reconnect with AI present, filtered restored identities,
  continued AI turns, Lobby cleanup, changed identity on the second game,
  and all three formal Identity winning sides with AI stopping at GameOver.

Existing tests are retained. The original ordinary Slash/Dodge subcase now
explicitly resets the target's equipment after its preceding random AI turn,
and additionally verifies that Dodge was consumed. The diagnosed old fixture
failure was `Target is invalid`, distance 2 versus range 1 from a defensive horse.
This preparation isolates the intended response test without weakening it.

Updated AITests passed ten consecutive runs after resolving the fixture drift.

## Final validation

Completed 2026-09-07:

- Full Release build: PASS, `cmake --build gaming/build --config Release --parallel 4`.
  Build log: `build/stage12b-release-build.log`.
- Full CTest: **26/26 PASS**, 13.08 seconds,
  `ctest --test-dir gaming/build -C Release --output-on-failure`.
  Detailed results: `build/Testing/Temporary/LastTest.log`.
- Deploy: PASS, `cmake --build gaming/build --config Release --target deploy`.
  The resolved target was verified as the workspace's `gaming/dist/BasicSanguosha`.
  Deployment log: `build/stage12b-deploy.log`.
- `dist/BasicSanguosha/BasicSanguosha.exe`: UPDATED, 718336 bytes.
- Both build and dist SHA-256:
  `9229CA4C6DA3C2F5A0A152C0FD8E7FF68D23B9393E806DA6511B892CFD55D238`.
- Lord, Loyalist, Rebel, Renegade, self identity, victory goal, privacy, FFA
  isolation, Identity response and target modifier: PASS.
- Reconnect, Return To Lobby, Second Game, Human + AI Identity smoke: PASS.

Added source/document files:

- `tests/AIIdentityTests.cpp`
- `docs/Stage12BIdentityAICoverage.md`

Modified source/configuration files:

- `CMakeLists.txt`
- `src/ai/AIActionDecider.h`
- `src/ai/AIController.cpp`
- `src/ai/AIIdentityObjective.h`
- `src/session/GameSession.h`
- `src/session/GameSession.cpp`
- `src/network/GameServer.cpp`
- `tests/AITests.cpp`

Stage 12B: **DONE**. Blocker: **NONE**.
Next roadmap stage: Stage 13 AI full functional closure; not started by this run.
