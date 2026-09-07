# Stage 13.5D AI Identity Inference

## Boundary and lifecycle

`AIIdentityInference` rebuilds one observer's belief table exclusively from its
filtered `PlayerViewState`: public player state, identities already revealed to
that observer, and structured public `GameEvent` history. It has no `GameEngine`
pointer, server identity table, opponent hand faces, future deck, or unrevealed
judgment input.

Each `AIController` owns an inference instance. Rebuilding from public history
makes reconnect deterministic. `GameEngine::createGame` clears that history,
controller reset clears its local result, and the next game starts from Unknown.
FFA returns Disabled and creates no beliefs.

## Belief model

Each hidden player has bounded `[-100, 100]` Loyalist, Rebel, and Renegade
scores, an Unknown score, evidence count, inferred label, confidence, and
friendly/hostile/uncertain relation. Lord, self, and dead revealed identities
are stored separately as `confirmedIdentity`; evidence never overwrites them.

Evidence weights are centralized by type:

- attacking, damaging, delaying, or removing resources from Lord;
- rescuing Lord or removing a bad delayed trick from Lord;
- protecting Lord with Nullification or countering that protection;
- lower-weight AOE, suspected-Rebel attack, and suspected-Rebel support;
- contradictory pro/anti-Lord behavior, which raises Renegade tendency.

Scores decay three percent at each public turn end and are clamped. Insufficient
score or margin stays Unknown. Medium/High confidence requires repeated evidence;
one action never confirms a hidden role.

## Structured public evidence

The existing typed `GameEvent` history is exposed to the server-side AI view.
Two minimal public event types were added:

- `NullificationContext` records only effect direction, public target, and
  current chain parity. It correctly distinguishes cancelling harm, restoring
  harm, cancelling benefit, and restoring benefit without parsing UI logs.
- `PublicCardMoved` records a resolved Dismantlement/Snatch move and its public
  zone/name. This separates stripping Lord equipment from removing a harmful
  delayed trick. Hidden hand faces remain absent.

## Target integration

`AITargetEvaluator` keeps `knownIdentityModifier` and `inferenceModifier` as
separate fields. Only classified confidence produces an inferred modifier, capped
at 18 versus the existing known-role policy's much larger hard boundaries. This
is intentionally a small interface proof, not Stage 13.5E coordination.

## Verification

Identity coverage tests public-event-driven initial Unknown/Lord confirmation,
repeated attacks, rescue, harmful and beneficial Nullification parity,
Dismantlement meanings, delayed tricks, contradiction/Renegade tendency,
confidence growth, score bounds/decay, death reveal, FFA disablement, deterministic
rebuild, real hidden-role swaps, real hidden-hand face changes, reconnect rebuild,
return-to-lobby clearing, and a changed-role second game.

Stage 13.5E identity-specific coordination and endgame policy are explicitly out
of scope.
