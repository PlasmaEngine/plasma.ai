#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/WorldModule.h>
#include <DetourLocalBoundary.h>
#include <DetourNavMeshQuery.h>
#include <DetourObstacleAvoidance.h>
#include <Foundation/Containers/Deque.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Threading/AtomicInteger.h>

class plAiNavMesh;
class dtProximityGrid;

/// \brief How much CPU an agent's local avoidance may use.
struct PL_AIPLUGIN_DLL plAiCrowdAvoidanceQuality
{
  using StorageType = plUInt8;

  enum Enum
  {
    Low,    ///< separation forces only - agents push each other away, cheapest
    Medium, ///< sampled velocity obstacles (grid sampling) + navmesh wall segments
    High,   ///< sampled velocity obstacles (adaptive sampling) + navmesh wall segments

    Default = High
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiCrowdAvoidanceQuality);

/// \brief Everything the crowd solver needs to know about one agent for one frame. Engine space (Z up).
struct PL_AIPLUGIN_DLL plAiCrowdAgentState
{
  plVec3 m_vPosition = plVec3::MakeZero();
  plVec3 m_vVelocity = plVec3::MakeZero();
  plVec3 m_vDesiredVelocity = plVec3::MakeZero();
  float m_fRadius = 0.3f;
  float m_fMaxSpeed = 5.0f;
  plEnum<plAiCrowdAvoidanceQuality> m_Quality;

  /// Navmesh polygon the agent currently stands on. 0 = unknown -> no wall segments, separation only.
  dtPolyRef m_CurrentPoly = 0;
  const plAiNavMesh* m_pNavMesh = nullptr;
  const dtQueryFilter* m_pFilter = nullptr;

  /// True for non-AI movers (e.g. the player): they influence other agents but never receive
  /// an adjusted velocity themselves.
  bool m_bObstacleOnly = false;
};

/// \brief Local avoidance for navigating agents, so they steer around each other instead of
/// walking through one another.
///
/// Reuses the battle-tested solver pieces of DetourCrowd (sampled velocity obstacles, proximity
/// grid, local wall boundary) without adopting dtCrowd itself - path planning stays in
/// plAiNavigation, movement stays in plAiNavigationComponent.
///
/// Frame flow (1 frame of latency, by design):
/// - PreAsync: agents submit their state (position, velocity, DESIRED velocity from steering)
///   via SubmitAgentState() and consume the velocity solved LAST frame via TryGetAdjustedVelocity().
/// - PostAsync: UpdateSolve() runs the avoidance solve for all submitted agents in parallel
///   (plTaskSystem::ParallelForIndexed - blocks until done, so no solve work ever overlaps the
///   navmesh tile changes that happen later in PostTransform).
class PL_AIPLUGIN_DLL plAiCrowdWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiCrowdWorldModule, plWorldModule);

  // holds Detour objects and non-copyable slots; exported classes generate all implicit members
  PL_DISALLOW_COPY_AND_ASSIGN(plAiCrowdWorldModule);

public:
  plAiCrowdWorldModule(plWorld* pWorld);
  ~plAiCrowdWorldModule();

  virtual void Initialize() override;
  virtual void Deinitialize() override;

  using AgentID = plUInt16; // dtProximityGrid stores unsigned short ids
  static constexpr AgentID InvalidAgentID = 0xFFFF;

  AgentID RegisterAgent();
  void UnregisterAgent(AgentID id);

  /// \brief Publishes the agent's state for this frame's solve. Main thread (PreAsync) only.
  void SubmitAgentState(AgentID id, const plAiCrowdAgentState& state);

  /// \brief Retrieves the avoidance-adjusted velocity solved last frame (engine space, Z = 0).
  ///
  /// Returns false when no recent result exists (agent just registered, solve disabled,
  /// obstacle-only agent) - callers then use their own unmodified steering.
  bool TryGetAdjustedVelocity(AgentID id, plVec3& out_vVelocity) const;

private:
  void UpdateSolve(const UpdateContext& context);
  void SolveAgent(plUInt32 uiSolveIndex, plUInt32 uiScratchIndex);
  void DrawDebugVisualization();

  struct Slot
  {
    bool m_bInUse = false;
    plUInt32 m_uiSubmitFrame = 0;
    plAiCrowdAgentState m_State;

    dtLocalBoundary m_Boundary; ///< persistent per agent, updated incrementally
    plVec3 m_vBoundaryUpdatePos = plVec3(plMath::HighValue<float>());
    const plAiNavMesh* m_pBoundaryNavMesh = nullptr;
  };

  struct Result
  {
    plVec3 m_vVelocity = plVec3::MakeZero();
    plUInt32 m_uiSolveFrame = 0;
  };

  /// Per-parallel-invocation scratch. dtObstacleAvoidanceQuery and dtNavMeshQuery are stateful
  /// during a solve (sampling buffers, node pools), so every parallel sub-range grabs its own.
  struct Scratch
  {
    Scratch();
    ~Scratch();

    PL_DISALLOW_COPY_AND_ASSIGN(Scratch);

    dtObstacleAvoidanceQuery* m_pAvoidance = nullptr; // dtAlloc'd
    dtNavMeshQuery m_NavQuery;
    const plAiNavMesh* m_pQueryInitializedFor = nullptr;
  };

  plDeque<Slot> m_Slots; // stable addresses, slots reused via free list
  plDynamicArray<AgentID> m_FreeSlots;

  plDynamicArray<Result> m_Results[2]; // double buffer: solve writes one, agents read the other
  plUInt32 m_uiReadBuffer = 0;
  plUInt32 m_uiFrameCounter = 0;

  plDynamicArray<AgentID> m_SolveList; // agents submitted this frame, rebuilt per solve
  dtProximityGrid* m_pGrid = nullptr;  // dtAlloc'd
  plDeque<Scratch> m_Scratch;          // one per parallel invocation, acquired via atomic counter
  plAtomicInteger32 m_uiScratchCounter = 0;

  dtObstacleAvoidanceParams m_AvoidanceParams[3]; // per quality tier
};
