# AI debugging tools

Editor tools for looking at running agents. They read the scene's engine process, so they show
data while the scene simulates or plays.

## AI Debugger

Every scene has an **AI Debugger** panel. It starts in the strip at the bottom of the scene
window; **Open AI Debugger** in the viewport's **AI Overlays** menu pins it under the view.

- **Agent list.** Every agent of the world with its running behavior, archetype and decision LOD
  tier (HOT, WARM, COLD). The filter matches names; **Alerted** keeps the agents whose alert level
  is above Unaware. Clicking an agent watches it and selects its object in the scene.
- **Follow selection.** While on, selecting an agent in the scene watches it.
- **Decision.** The behaviors of the latest decision, category ladder first, then utility. The
  amber line on a bar is what a challenger of the running behavior's category must beat (the
  running utility plus its commit bonus). Badges say which behavior runs, which won, and why a
  behavior did not compete. Click a behavior to see each consideration's raw value, normalized
  value and score.
- **Perception.** The alert level, and every remembered target with its senses, confidence, when
  it was last sensed and its last known position. Positions reported by an ally show in amber.
- **Blackboard.** The agent's blackboard. Values that just changed show in the accent colour.
- **Navigation.** The navigation component, its state and the point it steers toward.
- **EQS.** The last captured query of the agent. **Capture next query** records the next EQS
  query the agent runs.
- **Recording.** The timeline at the bottom shows the behavior switches of the last 30 seconds;
  the recording keeps 120 seconds. Dragging across it freezes the panel at that time (the game
  keeps running); **Frozen** goes back to following the simulation. Clicking a block explains the
  switch that started it and shows the decision that made it. While frozen, the recording keeps
  what the panel shows, for up to 10 minutes. The tabs show recorded details only for the time an
  agent was watched; the timeline has every agent's switches.
- **Pause simulation** pauses the world.
- The status bar shows the agents, decisions per frame against the budget
  (`AI.Agents.MaxDecisionsPerFrame`), EQS queries and raycasts, crowd agents and the brain's update
  time.

Watching an agent records detailed traces of its decisions; it never changes them. Agents spawned
at runtime can be watched from the list even though the scene has no object for them.

## AI Overlays menu

The viewport toolbar's **AI Overlays** menu (the robot-head icon next to the render mode and
perspective menus) switches the AI debug drawings. Each entry is an
existing cvar, so console users lose nothing:

| Entry | CVar |
|---|---|
| Navigation: Navmesh | `AI.Navmesh.Visualize` (0 = on, -1 = off) |
| Navigation: Nav links | `AI.Navmesh.VisualizeLinks` |
| Navigation: Blockers and obstacles | `AI.Navmesh.VisualizeBlockers` |
| Navigation: Crowd avoidance | `AI.Crowd.Visualize` |
| Navigation: Voxel grid | `AI.VoxelGrid.Visualize` |
| Perception: Perceived targets | `AI.Agents.ShowPerception` |
| Decisions: Behavior scores | `AI.Agents.ShowScores` |
| Decisions: Behavior history | `AI.Agents.ShowHistory` |
| Tactical: Cover points | `AI.Tactical.VisualizeCover` |
| Tactical: Tactical queries | `AI.Tactical.VisualizeQueries` |
| Tactical: Smart objects | `AI.SmartObjects.ShowDebug` |
| Tactical: Squads and intent | `AI.Squad.ShowDebug` |
| Tactical: EQS results | `AI.EQS.VisualizeQueries` |
| Tactical: EQS score breakdown | `AI.EQS.VisualizeScores` |
| Statistics: EQS statistics, Tactical statistics, Navmesh build statistics | `AI.EQS.Stats`, `AI.Tactical.ShowStats`, `AI.Navmesh.ShowBuildStats` |

**Watched agent only** (`AI.Agents.DebugWatchedOnly`) limits the per-agent drawings (scores,
perception, history) to the agent the AI Debugger watches.

## Live score preview

The Behavior window's **Score Preview** has two sources:

- **Simulated:** the sliders set each consideration's raw value.
- **Live agent:** the raw values of this behavior's considerations in the latest decision of the
  agent the AI Debugger watches. The agent menu picks another agent. Each row shows how the
  consideration's score moved over the last 40 decisions. The note under the bar gives the score
  the runtime computed, which includes the archetype's weight scale and scoring version. The
  competition list shows the behaviors of the same category and the running one.

**Copy to sliders** keeps the live values as the simulated situation.
