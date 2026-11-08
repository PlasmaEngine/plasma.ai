# AI runtime rules

How the brain, perception, EQS and navigation behave at runtime, where the behavior is not
obvious from the asset editors.

## Behavior selection

Each behavior scores `category + utility`, with utility in [0, 0.999]. The active behavior is
replaced when:

| Challenger | Result |
|---|---|
| Higher category | Always replaces the active behavior |
| Lower category | Never replaces it |
| Same category | Replaces it only when it scores more than the active score plus the active behavior's **Commit Bonus** |
| Any, while the active behavior no longer scores | Replaces it |

The commit bonus only protects a behavior against challengers from its own category. A Command
behavior at 4.99 with the default bonus of 0.2 is replaced by a Combat behavior at 5.05.

A behavior's **Weight** and the archetype's **Weight Scale** multiply the utility and run from 0
to 1. Utility is capped at 1, so with weights above 1 every behavior that reaches the cap ties;
the validation dock flags such weights in existing assets.

A behavior without a target sees `Ai_TargetPosition` at zero, not the previous target's position.

While the active behavior's state machine sets `Ai_LockBehavior` (logic state `Locked`), no
challenger replaces it, whatever its category. The archetype's **Lock timeout** limits that: a
behavior that has reported `Locked` for longer is treated as unlocked. Zero means no limit.

### Scoring version

How a behavior's considerations combine depends on the archetype's **Scoring version**:

| Version | Utility before weights | Four considerations at 0.8 |
|---|---|---|
| 1 (existing assets) | The product, rescaled once: `p + (1 - p) * p * (1 - 1/n)` | 0.59 |
| 2 | The geometric mean: `p^(1/n)` | 0.80 |

A consideration at 0 vetoes the behavior in both versions.

### Target switching

Within the active behavior, a different target only takes over when it scores more than the
current target plus the archetype's **Switch margin** (default 0.1), and the current target has
been held for at least **Minimum hold** (default 0.5 s). The behavior is then reactivated with
the new target.

## Early decisions

Agents decide on a schedule set by their LOD tier (`AI.Agents.DecisionHz` and the warm/cold
rates). These events make an agent decide sooner, no earlier than the archetype's **Minimum wake
spacing** (default 0.1 s) after its last decision:

| Event | Condition |
|---|---|
| A stimulus the agent may perceive | Archetype: wake on stimulus (default on) |
| The active behavior reports Done or Failed | Archetype: wake on behavior end (default on) |
| A command is issued, cancelled or expires | Always |
| A behavior is enabled, disabled or reweighted, or a weight override expires | Always |
| `RequestDecision` | Always |

Woken agents are served before the scheduled ones and use the same
`AI.Agents.MaxDecisionsPerFrame` budget.

## Script API

`plAiAgentComponent` offers these functions to scripts, state machines and game code:

| Function | Effect |
|---|---|
| `RequestDecision()` | Decide as soon as the wake spacing allows |
| `SetBehaviorEnabled(Behavior, Enabled)` | A disabled behavior doesn't compete. Disabling the active one ends it, even while locked |
| `SetBehaviorWeight(Behavior, Scale, DurationSeconds)` | Scales the behavior's weight; duration 0 keeps it until changed, scale 1 restores the authored weight |
| `IssueCommand(Command, Position, Target, ExpirySeconds)` | Writes `Ai_CommandActive`, `Ai_Command`, `Ai_CommandPosition` and `Ai_CommandTarget`; expiry 0 keeps it until cancelled |
| `CancelCommand()` | Sets `Ai_CommandActive` to false |
| `GetActiveBehavior()`, `GetActiveTarget()` | The running behavior's name and target |

Behaviors carry out commands by scoring on `Ai_CommandActive`, usually in the Command category.

## Debugging data

`plAiBrainWorldModule` provides the data for the AI debugging tools. Watching an agent never
changes its decisions.

| Function | Returns |
|---|---|
| `GetAgentSummaries` | One row per agent: active behavior, target, LOD tier, locked, configured |
| `SetAgentWatched` | Starts or stops detailed traces for one agent |
| `GetAgentDebugSnapshot` | For a watched agent: every behavior's category, utility, score and veto reason (not loaded, disabled, cooldown, no target, a consideration at 0), each consideration's raw, normalized and curved value, the decision outcome, targets and blackboard |
| `GetAgentHistory` | The last 64 switches and endings, each with its outcome (higher category, beat commit bonus, done, failed, disabled and so on) |

## Behaviors that delete their agent

Behavior logic, state machine states and actions may delete the agent's own object, for example
a "die" state that deletes the owner. The agent stops at that point: the brain does not run the
rest of the frame's logic or actions for it, and frees the behavior instance after the call that
deleted the object returns.

An agent that is removed (its object deleted or deactivated) destroys its behavior without
`Deactivate`, but the state machine's current state still exits and releases what it holds:
cover and smart object claims, EQS queries, navigation. When the brain module itself shuts down
with its world, `plAiBehaviorLogic::IsWorldShuttingDown()` is true and the AI states skip the
world; custom logic and states should do the same.

## Squads

A squad retreats when it has lost the configured fraction of its full strength
(`AI.Squad.RetreatLossFraction`). After `AI.Squad.RetreatHoldTime` the retreat is over and the
survivors are the squad's new full strength, so only new losses make it retreat again. A member
that joins (reinforcements, or a deactivated member that comes back) also starts a new count. A
retreat's rally point is chosen once, when the retreat begins.

### Squad orders

Game code, a script, the encounter director or the AI Debugger can order a squad
(`plAiSquadWorldModule::IssueOrder`, or `IssueSquadOrder` on an agent of the squad). An order has an
intent (Hold, Advance, Engage or Retreat), an optional region (a rectangle on the ground), a duration
and a **No retreat** flag:
- While an order is active, the squad's intent is the order's. Its own reasoning (shared target,
  losses, exposure) does not change it, and the members still read it as `Ai_SquadIntent`, so their
  behaviors need no change. Members also get `Ai_SquadOrderActive` and `Ai_SquadRegionCenter`.
- With a region, each member's `Ai_SquadMovePos` is a spot of its own inside it. Hold and Engage go
  to the spot, Advance reaches it in bounds, Retreat falls back to it.
- An order lasts its duration (0 = until cancelled), then the squad goes back to its own intent at
  once. A new order replaces the old one; `CancelOrder` ends it.
- Losses that would make the squad retreat break the order (the squad retreats by itself), unless
  the order has **No retreat**: then the squad stays at its task, and retreats when the order ends if
  it still has the losses.
- An order for a squad that has no member yet is kept, and taken when the squad forms. Its time
  counts from the moment it was given.
- An encounter wave can carry an order for the squad it spawns: **Order**, **Order Duration**,
  **Order Radius** (a square around the wave's object; 0 = no region) and **Order No Retreat**.

The squad's history (orders, intent changes, losses) is in the AI Debugger's Squad tab.

## Encounter director

`plAiEncounterDirectorComponent` paces an encounter: it decides when each wave may start. It doesn't
spawn anything. The waves are the `plAiEncounterWaveComponent`s on objects below the director, in
hierarchy order (W1, W2, ...); a wave's name is its object's name.

| Trigger | The wave is due when |
|---|---|
| EncounterStart | the encounter starts (with the simulation, or `StartEncounter()`) |
| WaveCleared | the wave named in AfterWave is cleared |
| WaveDown | the wave named in AfterWave was released and has at most AliveAtMost agents left |
| Time | Time seconds have passed since the encounter started |
| Calm | the intensity stayed below CalmBelow for CalmFor seconds |
| Script | the director's `TriggerWave(name)` names it (works for every trigger) |

A due wave is held while the intensity is above the pacing curve plus **Hold Margin**, at most
**Max Hold** seconds, then released. The pacing curve is a list of phases: each goes in a straight
line from the previous phase's target (0 for the first) to its own over its duration, in seconds or
in cleared waves. Without phases nothing is held.

Intensity runs from 0 to 1, measured ten times a second, as the weighted sum of three parts:
- agents at alert level Alert whose target belongs to **Player Team** (objects without an AI agent
  have no team, so with Player Team empty every target that isn't an AI agent counts), divided by
  **Engaged For Full**;
- the strength of sound and damage stimuli posted by Player Team within the **Window**, divided by
  **Stimuli For Full** (squad shares don't count);
- damage the players took within the Window, reported by the game through `ReportPlayerDamage()`,
  divided by **Damage For Full**.

The sum is smoothed over **Smoothing** seconds.

Releasing a wave tells its spawner object (the wave's **Spawner**, or the wave's own object) to
start, through the first of:
1. `plMsgAiEncounterWave` with Event Released, when a component of the spawner handles it;
2. the reflected function named **Release Function** (default `Activate`), with default arguments;
3. a `plSpawnComponent`, which spawns once.

The director counts a wave's agents through its squad: the wave's **Squad Id**, or the spawner's
own `SquadId` property. Without one it calls the spawner's `GetAliveCount` function. A spawner with
an `IsCleared` function says when its wave is cleared; otherwise a wave is cleared when its agents
have been gone for 2 seconds, or when none showed up within 30 seconds of the release.

Start the encounter where the spawners run (the host, or offline). The editor's **Encounter**
panel, next to the AI Debugger, shows the curve, the measured intensity, the waves and why a wave
is held.

## Smart objects

An agent that loses its smart-object claim while using it (the object was deleted or deactivated,
or the claim was released) always ends the use: the object gets the end-use message, and the
blackboard entries the use applied get back the values they had before.

## Cover during navmesh changes

A navmesh change in a sector keeps its cover points and claims until the cover rebake finishes.
Then each claim moves to the nearest new cover point within 0.75 m; a claim with no point that
close is released, and its handle no longer resolves. A sector left without a usable navmesh has
no cover points.

## Stimuli

`plAiPerceptionWorldModule::PostStimulus` can be called at any time. Every agent in range sees
each stimulus exactly once, at its next decision after the stimulus was published, regardless of
the order in which world modules update. A stimulus is kept until every agent has processed it,
however slowly it decides, and at most 30 seconds.

Agents remember where an event happened, not where its source moves afterwards. A stimulus is
broadcast from `m_vGlobalPosition`; when the event happened somewhere else, `m_vEventPosition`
says where (squad shares broadcast around the squad but report the enemy's position). Stimuli
without a source are told apart by position: one within 2 m of a remembered sourceless event
refreshes it.

`plAiStimulusSourceComponent` lets any object emit stimuli: every **Interval**, or only when
`Emit()` is called if the interval is zero.

## Senses and memory

| Sense | Source | Settings |
|---|---|---|
| Sight | `plSensorComponent`s on the agent | Gain, decay and memory on the archetype |
| Hearing, Damage, Custom | Stimuli of that type | Enabled, range scale, strength scale; decay and memory either from sight or their own |

A perceived target decays with the settings of the sense that refreshed it last, and is
forgotten when its confidence is 0 and nothing refreshed it for that sense's memory time. Each
agent keeps at most **Max targets** records (default 8) and drops the least confident one beyond
that, never the active target. Sightings keep the target's position and velocity; the other
senses keep the event position.

## Alert level

Every agent has an alert level, derived from its perceived targets when it decides:

| Level | When |
|---|---|
| Unaware | No remembered targets |
| Suspicious | The remembered targets were only heard (or came from custom stimuli) |
| Searching | A remembered target was seen, did damage or was reported by an ally, but not in the last second |
| Alert | A target was seen, or did damage, in the last second |

It is on the blackboard as `Ai_AlertLevel` (0-3), and the **Alert Level** consideration input
reads it.

## EQS scheduling

| Rule | Detail |
|---|---|
| Order | Pending queries run first in, first out. A query that runs out of budget mid-test queues behind everything already waiting. |
| Per frame | `AI.EQS.MaxQueriesPerFrame` queries run per frame (default 8). |
| Deadline | A query still pending after `AI.EQS.MaxQueryAge` seconds (default 3) completes with status `TimedOut`. |
| Cancellation | `ReleaseQuery` on a pending query removes it from the queue immediately. |
| Line of sight | Each line-of-sight run processes at least one candidate, even when the context has more positions than `AI.EQS.MaxRaycastsPerQuery`. |
| Asset reload | A query that resumes after its asset was reloaded starts over from the generator with the reloaded tests. |

The `AI.EQS.Stats` overlay shows the number of timed-out queries since the world started.
Callers treat `TimedOut` like `NoResult`; the built-in EQS state and component do.

### Capture and replay

`plAiEqsWorldModule::CaptureNextQuery(querier)` records the next query that completes for that
querier (any querier with an invalid handle) as a `plAiEqsCapture`:
- the contexts;
- every candidate, survivors best first and then rejected ones;
- what each test measured for each candidate;
- the seed and the selected candidate.

Captures can be saved and loaded (`Serialize` / `Deserialize`). `plAiEqsReplayCapture` re-scores
a capture with a query descriptor, using the captured measurements:
- An unchanged query reproduces the capture.
- An edited one shows what its new filters, curves, weights and run mode would have chosen on the same data.
- Candidates that reach a test they were never measured for (because the capture rejected them earlier) are counted as missing samples.

Samples cover the first 12 tests.

The runtime, replays and previews select results with the same `plAiEqsSelectCandidate`, so
RandomOfTopPercent picks the same candidate for the same seed everywhere.

## Navmesh sectors

Sectors build on worker threads. A sector invalidated while it is being built is rebuilt once
that build finished, so a change that happens during a build is never lost. A build that fails
logs an error and leaves the sector without polygons until it is rebuilt.

`plAiNavMesh::GetSectorStatus` reports, per requested sector:
- its state: queued, building, ready, rebuilding or failed;
- its polygon count;
- how many times it was built and how long the last build took;
- why it was last built: first build, obstacle, nav link, requested or another change;
- how far a running build probably is (an estimate from the last build's duration, never complete before it
  finishes), and the height range of its tile.

The polygon budget `AI.Navmesh.SectorPolyBudget` (default 2048) is the count the density view treats as full.
A sector that is built with 85 % of it or more logs a warning. The hard limit of the data structure is
`plAiNavMesh::MaxPolysPerSector`, which sectors never come close to.

### Sector views

`AI.Navmesh.VisualizeMode` draws the sectors instead of the polygons: 1 = sector status, 2 = rebuild
reason, 3 = polygon density (0 = polygons, with `AI.Navmesh.Visualize`). The modes draw by themselves.
- **Sector status**: ready sectors are grey, building and rebuilding ones rose with a progress bar,
  failed ones red with a cross, queued ones a dashed rose border.
- **Rebuild reason**: rose = obstacle, blue = nav link, yellow = requested, grey = changed, dark = first build.
- **Polygon density**: dark to yellow against the budget; a sector near the budget shows `! count`.

A stats box (sectors, ready, queued, building against `AI.Navmesh.MaxConcurrentBuilds`, failed,
average build time, sectors near the budget) and a legend show with the views. The AI Debugger outlines
the sector under the watched agent, or the one picked in its Navigation tab.

### Long paths

A path corridor holds 256 polygons and a path search 2048 nodes. A path that is longer is cut at the
corridor's end. The navigation keeps its state at FullPathFound, `plAiNavigation::IsPathContinued()` is
true, and once the agent has used up half of the corridor a second search from the corridor's end to the
target adds the next stretch. A target that proves unreachable ends as PartialPathFound, as before.
Giving the same destination again while a long path is followed does not restart it.

## Threat map

Each team has a map of what it knows about danger: a grid of 2 m cells over the navmesh sectors
(`plAiThreatWorldModule`). A cell holds the **threat** (line of sight to an enemy position the team's agents
remember, falling off with distance up to `AI.Threat.Range`, and with how sure the agents are), and whether
a known enemy sees it. **Danger areas** add danger anywhere: inside the radius it is 0.25 at the edge and 1
at the center, and a timed area fades. Stimuli reach the map through the agents' remembered targets.

- The map refreshes at the squad tick rate (`AI.Threat.TickInterval`), within a raycast budget per
  frame (`AI.Threat.RaycastsPerFrame`). The rays run on the main thread, after physics has stepped.
- Queries read published snapshots that never change, so tactical queries and EQS tests on worker
  threads see a consistent map. A team that no agent belongs to has no map.
- Add danger with `plAiThreatWorldModule::AddDangerArea`, a `plAiDangerAreaComponent` (follows its
  object, optionally timed) or the component's `AddDangerArea` script function. An area can be for one
  team; without a team it is for every team.
- Tactical queries: the cover presets score `Threat` (1 where known enemies cannot hurt) and `Danger`:
  a candidate inside an area's core (danger 0.5 or more) is discarded, and every score is multiplied by
  1 minus the danger. The query description's `m_sTeam` says whose map; the pick state sets it from the agent.
- EQS: the **Threat Map** test reads Safety (default), Threat, Visibility or Danger at an item.
- Agents with a plBlackboardComponent get `Ai_ThreatHere` and `Ai_DangerHere` every tick.
- An agent whose claimed cover reaches danger 0.3 (`AI.Threat.EvadeDanger`) loses the claim and its
  behavior ends (outcome Interrupted, even while locked); the behavior that wins again picks its cover
  anew. This runs on the next map tick, at most once a second per agent. `InterruptBehavior` does the same
  from a script.

`AI.Threat.Visualize` draws the map: 1 = combined (threat rose, danger red), 2 = threat, 3 = visibility,
4 = danger. It shows the team of the watched agent, or `AI.Threat.VisualizeTeam`.

## Crowd avoidance

The crowd's proximity grid grows with the number of cells the agents cover, up to 65,535
entries. Beyond that, some agents miss neighbors. `plAiCrowdWorldModule` reports the grid's
capacity, used entries and dropped entries, and logs a warning once.

Agents that stand still (navigation idle, failed or turning) are obstacles: moving agents steer
around them, but the crowd never steers them. With movement mode **Physics Character**, the
character follows the crowd's adjusted velocity.

## Nav blockers

A nav blocker only acts while the world simulates (Play or Simulate). It acts on floor surfaces
inside its box or up to 0.5 m below the box's bottom, so a box resting on or just above the floor
works. The **Mode** decides what it does:
- **Block**: the polygons in the box are closed to every path search. Toggling is instant.
- **Avoid**: the polygons stay walkable but cost `AI.Navmesh.AvoidZoneCost` times more to cross. Toggling is instant.
- **Carve**: the box is cut out of the navmesh like solid geometry, and agents keep their radius
  away from it. No collision shape is needed. Toggling or moving it rebuilds the sectors it
  overlaps, which takes about 100 ms.

The navmesh view (`AI.Navmesh.Visualize`) draws polygons closed by a Block blocker in red and Avoid
polygons in orange; a carve shows as a hole. `AI.Navmesh.VisualizeBlockers` draws the blocker boxes
themselves: red = Block, yellow = Avoid, orange = Carve, grey = switched off.

Removing a nav blocker gives each polygon under it back the area it was built with, so ground
types (surface costs) survive. Blocker volumes baked into the navmesh keep the ground type of the
surface below. A Block or Avoid blocker closes the nav links that start or end inside it while it
exists.

## Voxel navigation

The voxel grid is voxelized from **static** collision shapes once, when it is first needed. It
stays not ready in a world without physics, and voxel navigation waits instead of failing.
Anything that moves and should block flying agents needs a `plAiVoxelObstacleComponent`:
- Obstacles are counted per voxel, so removing one never frees static geometry or another obstacle.
- Obstacles added before the grid exists are applied once it is built.

Paths:
- A diagonal step needs its side voxels free, so paths never squeeze between two occupied corners.
- With an **Agent Radius**, paths keep that much clearance from occupied voxels, except right at the start and target.
- Searches run over several frames: `AI.VoxelNav.MaxNodesPerSearch` (default 4000) per search per frame, and `AI.VoxelNav.MaxNodesPerFrame` (default 20000) for all searches together.
- A destination set before the grid is ready is kept until it is (state Searching).
- When obstacles change the grid, a moving agent recomputes its path, at most every 0.25 s, and keeps following the old one until the new one is found.

## Navmesh size

Navmesh polygon references are 64 bits wide. A navmesh can have up to 2^28 sectors with up to
2^20 polygons each, so the default 64 x 64 sector grid works. A configuration that still can't be
created logs an error naming the navmesh, and queries against it behave as if the navmesh
config didn't exist.

Code that stores Detour polygon references must use `dtPolyRef`, never a 32-bit integer.
Every module that includes the Detour headers gets `DT_POLYREF64` from the Recast module, and
plugins built against an older version of this package must be rebuilt.
