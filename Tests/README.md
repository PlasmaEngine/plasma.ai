# AI semantics regression tests

From the package directory:

```powershell
plasmabuild build AiSemanticsTests --configuration Development
# Point this at the engine binaries used by the package SDK.
$env:PATH = "<engine>/Binaries/Windows_Development;" + $env:PATH
./Bin/Windows_x64_Development/AiSemanticsTests.exe
```

The executable returns nonzero on failure and reports the number of checks.
It exercises the actual plugin implementation, including reflection serialization and graph
compatibility patches. It does not require a running editor, physics world, or GPU.

The world tests create a `plWorld`, step it at a fixed 60 Hz and check runtime rules:

| File | Covers |
|---|---|
| `AiBrainWorldTests.cpp` | Category precedence over the commit bonus, behaviors that delete their own agent, stimulus delivery, debugging snapshots and history, early decisions and the script API, lock timeout, target switching, scoring versions, archetype serialization, senses, event positions, the target cap, alert levels, the stimulus source component |
| `AiEqsWorldTests.cpp` | Queue fairness with stuck queries, the query deadline, cancellation, restarting after an asset reload, capture, replay, shared result selection |
| `AiNavigationTests.cpp` | 64-bit polygon references and navmesh initialization, sector status, invalidation during builds, crowd grid growth |
| `AiVoxelTests.cpp` | Corner cutting, clearance values and local updates, agent radius, incremental search, obstacle layers, readiness without physics, early destinations, replanning around a new obstacle |

EQS world tests queue queries through `plAiEqsWorldModuleTestAccess`, because a real submit needs
navmesh sectors, which need a physics scene to build.

`AiTestPhysics.h` stands in for a physics engine: `plAiTestPhysicsWorldModule` answers raycasts,
box overlaps and object bounds against axis-aligned boxes, and `plAiTestNavGeoWorldModule`
provides flat floors as navmesh input. Navmesh tests wait on real time (up to 10 s), because
sectors build on worker threads.

Optionally pass paths to runtime `.plAiEqsQuery` files freshly transformed from version-1
source assets. These additional checks verify that their curve-domain and missing-data
compatibility settings survived the actual editor migration. Do not use this mode for new
assets or assets deliberately migrated away from legacy settings.

Build `EditorPluginAi` separately to validate the editor integration. Real navigation/physics
queries and visual editor interaction require a scene-level smoke test.
