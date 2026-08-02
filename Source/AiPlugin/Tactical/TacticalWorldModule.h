#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Tactical/SmartObjects.h>
#include <AiPlugin/Tactical/TacticalTypes.h>
#include <Core/World/WorldModule.h>
#include <DetourNavMeshQuery.h>
#include <Foundation/Threading/AtomicInteger.h>
#include <Foundation/Threading/TaskSystem.h>

class plAiCoverBakeTask;
class plAiNavMeshWorldModule;
class plPhysicsWorldModuleInterface;

/// \brief The AI tactical layer: baked cover points with runtime claiming, and (soon) budgeted
/// spatial position queries.
///
/// Cover points are generated from navmesh boundary edges whenever a sector is (re)built:
/// boundary segments are extracted right after the tile swap, then a background task probes them
/// with physics raycasts and classifies Low/High cover. Points regenerate automatically when the
/// world changes (obstacles move, blockers toggle) because those already rebuild the sectors.
///
/// All public APIs are main-thread only. Cover handles stay valid until their sector re-bakes;
/// claims on re-baked sectors are force-released, so claimants must be prepared for their claimed
/// point to disappear (poll via ResolveCover / GetClaimedCover).
class PL_AIPLUGIN_DLL plAiTacticalWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiTacticalWorldModule, plWorldModule);
  PL_DISALLOW_COPY_AND_ASSIGN(plAiTacticalWorldModule);

public:
  plAiTacticalWorldModule(plWorld* pWorld);
  ~plAiTacticalWorldModule();

  virtual void Initialize() override;
  virtual void Deinitialize() override;

  /// \name Tactical position queries
  ///
  /// Submit on the main thread at any time; queries execute in a budgeted parallel batch at the
  /// end of the frame, results are readable the NEXT frame via TryGetResult (poll while Pending).
  /// Call ReleaseQuery when done - unreleased queries are reclaimed automatically after a few
  /// seconds, but holding slots is wasteful.
  ///@{

  using QueryID = plUInt32;
  static constexpr QueryID InvalidQueryID = plInvalidIndex;

  QueryID SubmitQuery(const plAiTacticalQueryDesc& desc);
  bool TryGetResult(QueryID id, plAiTacticalQueryResult& out_result) const;
  void ReleaseQuery(QueryID id);

  ///@}

  /// \name Cover registry
  ///@{

  /// \brief Finds the closest baked cover point within fRadius (XY) of vPosition.
  ///
  /// Points more than fMaxZDelta above/below vPosition are skipped (0 = no height filter);
  /// ranking among the survivors uses full 3D distance, so same-floor cover wins.
  bool FindNearestCover(const plVec3& vPosition, float fRadius, bool bUnclaimedOnly, plAiCoverPointHandle& out_hPoint, plAiCoverPoint& out_point, float fMaxZDelta = 2.5f) const;

  /// \brief Resolves a handle to the current point data. Returns false when the sector re-baked.
  bool ResolveCover(const plAiCoverPointHandle& hPoint, plAiCoverPoint& out_point) const;

  /// \brief Returns all points of the cover surface hPoint belongs to, ordered along the wall.
  ///
  /// A cover surface is a contiguous strip of points along one wall contour (including corners) -
  /// agents can move between its points while staying in cover; claim the point being moved to.
  /// Standalone points return just themselves. Returns false when the handle is stale.
  bool GetCoverSurface(const plAiCoverPointHandle& hPoint, plDynamicArray<plAiCoverPointHandle>& out_points) const;

  ///@}

  /// \name Cover claims
  ///
  /// One claim per claimant: claiming a new point releases the previous one automatically.
  /// Claims expire after their timeout unless the claimant stays close to the point (the expiry
  /// keeps refreshing then), and are force-released when the point's sector re-bakes or the
  /// claimant object dies - so no explicit release is required, but releasing early is polite.
  ///@{

  bool ClaimCover(const plAiCoverPointHandle& hPoint, plGameObjectHandle hClaimant, plTime timeout);
  void ReleaseClaim(plGameObjectHandle hClaimant);
  bool GetClaimedCover(plGameObjectHandle hClaimant, plAiCoverPointHandle& out_hPoint) const;

  /// \brief Finds a position from which the claimant can fire at the threat from its claimed cover.
  ///
  /// Low cover (peek over): the cover position itself already has standing line of sight - it is
  /// returned directly. High cover (solid wall): side-steps along the wall in both directions
  /// (up to fMaxSideStep) until standing line of sight to the threat opens up. The result stays
  /// on the navmesh. Returns false when the claimant has no valid claimed cover or no reachable
  /// firing position exists. Main thread only.
  bool FindPeekPosition(plGameObjectHandle hClaimant, const plVec3& vThreatPos, float fMaxSideStep, plVec3& out_vPosition);

  ///@}

  /// \name Smart objects
  ///
  /// Authored interaction points (plAiSmartObjectComponent) agents find, claim and use.
  /// Claims share the cover-claim semantics (one-per-claimant, auto-sweep) - and an agent holds
  /// at most ONE tactical claim of EITHER kind: claiming a smart object releases its cover claim
  /// and vice versa. Implementation lives in Implementation/SmartObjects.cpp.
  ///@{

  using SmartObjectID = plUInt32;
  static constexpr SmartObjectID InvalidSmartObjectID = plInvalidIndex;

  SmartObjectID RegisterSmartObject(plComponentHandle hComponent);
  void UnregisterSmartObject(SmartObjectID id); // force-releases all claims on it

  /// \brief Finds the nearest FREE slot of a matching smart object within fRadius (XY).
  bool FindSmartObject(const plVec3& vPosition, float fRadius, const plTempHashedString& sType, plGameObjectHandle hClaimant, plAiSmartObjectHandle& out_hSlot, plVec3& out_vSlotPosition) const;

  bool ClaimSmartObject(const plAiSmartObjectHandle& hSlot, plGameObjectHandle hClaimant, plTime timeout);
  void ReleaseSmartObjectClaim(plGameObjectHandle hClaimant);
  bool GetClaimedSmartObject(plGameObjectHandle hClaimant, plAiSmartObjectHandle& out_hSlot) const;

  /// \brief Resolves a slot handle to its current world position and the owning component.
  bool ResolveSmartObject(const plAiSmartObjectHandle& hSlot, plVec3& out_vSlotPosition, plComponentHandle& out_hComponent) const;

  /// \brief Starts using the claimed smart object: sends the start message to its owner.
  /// Returns whether game code HANDLED it (false = caller runs the built-in UseDuration wait).
  bool BeginUse(plGameObjectHandle hUser);

  /// \brief Game code signals that the interaction finished (the FinishLinkTraversal analogue).
  void FinishUse(plGameObjectHandle hUser);

  /// \brief Ends the use: sends the end message and drops back to the claimed-but-not-using phase.
  void EndUse(plGameObjectHandle hUser);

  plAiSmartObjectUsePhase::Enum GetUsePhase(plGameObjectHandle hUser) const;

  ///@}

  /// \name EQS data provider
  ///
  /// Read-only registry access for the EQS module's generators and tests. The Collect* methods
  /// mirror the internal candidate collection of the tactical query executor.
  ///@{

  /// \brief Collects the closest baked cover points around vCenter, nearest (3D) first.
  ///
  /// The radius is tested in XY; points more than fMaxZDelta above/below vCenter are skipped
  /// (0 = no height filter). Safe to call from EQS worker jobs during the PostAsync phase: the
  /// cover registry only mutates in this module's PostTransform maintenance update.
  void CollectCoverPoints(const plVec3& vCenter, float fRadius, plUInt32 uiMaxPoints, plDynamicArray<plAiCoverPointHandle>& out_points, plDynamicArray<plVec3>& out_positions, float fMaxZDelta = 2.5f) const;

  /// \brief Whether the cover point is currently claimed by someone other than hSelf.
  bool IsCoverClaimedByOther(const plAiCoverPointHandle& hPoint, plGameObjectHandle hSelf) const;

  /// \brief Collects FREE slots of matching smart objects around vCenter (XY), nearest first.
  /// sType empty = any type. Main thread only (resolves live object transforms).
  void CollectFreeSmartObjectSlots(const plVec3& vCenter, float fRadius, const plTempHashedString& sType, plGameObjectHandle hClaimant, plUInt32 uiMaxSlots, plDynamicArray<plAiSmartObjectHandle>& out_slots, plDynamicArray<plVec3>& out_positions) const;

  /// \brief Whether the smart object slot is currently claimed by someone other than hSelf.
  bool IsSmartObjectSlotClaimedByOther(const plAiSmartObjectHandle& hSlot, plGameObjectHandle hSelf) const;

  ///@}

  /// \name Authored cover (plAiCoverPointComponent)
  ///
  /// Designer-placed cover markers injected into the registry: claimable, queryable and
  /// visualized like baked cover, but owned by a component and never re-baked. Registration
  /// takes effect on the next UpdateMaintain (the registry only mutates there - EQS/tactical
  /// worker jobs read it during PostAsync); unregistering releases any claim on the point.
  /// Requires a configured cover navmesh (the registry is inert without one).
  ///@{

  using AuthoredCoverID = plUInt32;
  static constexpr AuthoredCoverID InvalidAuthoredCoverID = plInvalidIndex;

  AuthoredCoverID RegisterAuthoredCover(plComponentHandle hComponent);
  void UnregisterAuthoredCover(AuthoredCoverID id);

  ///@}

  /// \name Agent tactical probes
  ///
  /// Registered agents (plAiAgentComponent does this automatically) get their tactical situation
  /// probed on a budget: the module writes 'Ai_TargetExposure', 'Ai_CoverStatus' and
  /// 'Ai_CoverNearby' into the agent's blackboard, where the corresponding consideration inputs
  /// (and SM transitions) read them. Requires a plBlackboardComponent on the agent.
  ///@{

  using AgentID = plUInt32;
  static constexpr AgentID InvalidAgentID = plInvalidIndex;

  AgentID RegisterAgent(plComponentHandle hAgentComponent);
  void UnregisterAgent(AgentID id);

  ///@}

private:
  void UpdateExecute(const UpdateContext& ctxt);  // PostAsync, low priority = after the brain's UpdateApply
  void UpdateMaintain(const UpdateContext& ctxt); // PostTransform, ordered after plAiNavMeshWorldModule::Update

  plAiNavMesh* ResolveCoverNavMesh(plAiNavMeshWorldModule& navMeshModule) const;
  void QueueInitialBakes(const plAiNavMesh& navMesh);
  void InvalidateAndQueueSector(plAiNavMesh::SectorID sectorID);
  void DispatchBakes(const plAiNavMesh& navMesh, const plPhysicsWorldModuleInterface* pPhysics);
  void CollectFinishedBakes();
  void ReleaseClaimAtIndex(plUInt32 uiClaimIndex);
  void SweepClaims();
  void DrawCoverVisualization();
  void DrawStats();

  /// Baked cover for one navmesh sector.
  struct CoverSector
  {
    plAiNavMesh::SectorID m_SectorID = plInvalidIndex;
    plUInt16 m_uiBakeGeneration = 0; ///< bumped on every invalidation; stale handles/bakes detect this
    bool m_bInUse = false;
    bool m_bBakePending = false; ///< queued or in flight
    plDynamicArray<plAiCoverPoint> m_Points;
    plDynamicArray<plAiCoverSurface> m_Surfaces; ///< wall strips over m_Points (points reference them via m_uiSurface)
  };

  plDeque<CoverSector> m_CoverSectors;
  plDynamicArray<plUInt32> m_FreeCoverSectors;
  plHashTable<plUInt32, plUInt32> m_SectorLookup; ///< sector id -> registry index

  /// Sector id marking registry entries that hold authored (component-owned) cover. Such entries
  /// never enter m_SectorLookup, never re-bake and skip the sector-bounds culling in queries.
  static constexpr plAiNavMesh::SectorID AuthoredCoverSectorID = 0xFFFFFFFE;

  // ---- authored cover (plAiCoverPointComponent) ----

  struct AuthoredCover
  {
    plComponentHandle m_hComponent;
    plUInt32 m_uiSectorEntry = plInvalidIndex; ///< index into m_CoverSectors once applied
    bool m_bInUse = false;
    bool m_bPendingRemoval = false;
  };

  plDeque<AuthoredCover> m_AuthoredCovers;
  plDynamicArray<AuthoredCoverID> m_FreeAuthoredCovers;

  void MaintainAuthoredCover(const plPhysicsWorldModuleInterface* pPhysics); // UpdateMaintain only
  void RetireAuthoredEntry(AuthoredCover& ref_authored);                     ///< releases claims + recycles the sector entry

  struct Claim
  {
    plGameObjectHandle m_hClaimant;
    plAiCoverPointHandle m_hPoint;
    plTime m_Expiry;
    plTime m_Timeout;
    bool m_bInUse = false;
  };

  plDeque<Claim> m_Claims;
  plDynamicArray<plUInt32> m_FreeClaims;
  plHashTable<plGameObjectHandle, plUInt32> m_ClaimByOwner;

  // ---- smart objects (Implementation/SmartObjects.cpp) ----

  struct SmartObjectEntry
  {
    plComponentHandle m_hComponent;
    plUInt16 m_uiGeneration = 0;
    bool m_bInUse = false;
    plHybridArray<plUInt16, 2> m_SlotClaims; ///< per slot: claim table index, 0xFFFF = free
  };

  struct SoClaim
  {
    plGameObjectHandle m_hClaimant;
    plAiSmartObjectHandle m_hSlot;
    plTime m_Expiry;
    plTime m_Timeout;
    plUInt8 m_uiUsePhase = 0; ///< plAiSmartObjectUsePhase
    bool m_bInUse = false;
  };

  plDeque<SmartObjectEntry> m_SmartObjects;
  plDynamicArray<plUInt32> m_FreeSmartObjects;
  plDeque<SoClaim> m_SoClaims;
  plDynamicArray<plUInt32> m_FreeSoClaims;
  plHashTable<plGameObjectHandle, plUInt32> m_SoClaimByOwner;

  void ReleaseSoClaimAtIndex(plUInt32 uiClaimIndex);
  void SweepSmartObjectClaims();
  void DrawSmartObjectDebug();

  struct BakeSlot
  {
    plSharedPtr<plAiCoverBakeTask> m_pTask;
    plTaskGroupID m_TaskID;
    plUInt32 m_uiEntryIndex = plInvalidIndex;
    plUInt16 m_uiGenerationAtDispatch = 0;
    bool m_bInFlight = false;
  };

  plHybridArray<BakeSlot, 8> m_BakeSlots;
  plDynamicArray<plAiNavMesh::SectorID> m_BakeQueue;

  plAiNavMesh* m_pCoverNavMesh = nullptr; ///< the single navmesh cover is baked for (AI.Tactical.CoverNavmesh)
  bool m_bInitialBakeQueued = false;

  // ---- queries ----

  struct QuerySlot
  {
    plAiTacticalQueryDesc m_Desc;
    plAiTacticalQueryResult m_Result;
    plAiNavMesh* m_pNavMesh = nullptr;      // resolved at submit (module-lifetime stable)
    const dtQueryFilter* m_pFilter = nullptr; // resolved at submit (module-lifetime stable)
    plTime m_CompletedAt;
    plUInt8 m_uiGeneration = 0; // detects stale QueryIDs after slot reuse
    bool m_bInUse = false;
  };

  static QueryID MakeQueryID(plUInt32 uiSlot, plUInt8 uiGeneration) { return uiSlot | (static_cast<plUInt32>(uiGeneration) << 24); }
  static plUInt32 QuerySlotIndex(QueryID id) { return id & 0x00FFFFFF; }
  static plUInt8 QueryGeneration(QueryID id) { return static_cast<plUInt8>(id >> 24); }

  const QuerySlot* GetSlotChecked(QueryID id) const;
  void ExecuteQueryJob(plUInt32 uiSlotIndex, plUInt32 uiScratchIndex); // worker thread; implemented in TacticalQuery.cpp
  void CollectCoverCandidates(const plVec3& vCenter, float fRadius, plUInt32 uiMaxCandidates, struct plAiTacticalCandidateSet& ref_candidates, const plVec3* pFacingThreat = nullptr, float fMinFacingDot = 0.0f, float fMaxZDelta = 2.5f) const;
  void SweepQuerySlots();
  void DrawQueryVisualization();

  plDeque<QuerySlot> m_Queries;
  plDynamicArray<plUInt32> m_FreeQueries;
  plDynamicArray<plUInt32> m_PendingQueue; // slot indices waiting for execution (FIFO)
  plDynamicArray<plUInt32> m_ExecuteList;  // slot indices executing this frame

  struct Scratch
  {
    dtNavMeshQuery m_NavQuery;
    const plAiNavMesh* m_pInitializedFor = nullptr;
  };

  plDeque<Scratch> m_Scratch;
  plAtomicInteger32 m_uiScratchCounter;

  // main-thread navmesh query (peek position projection); lazy init like the scratch queries
  dtNavMeshQuery m_MainThreadNavQuery;
  const plAiNavMesh* m_pMainThreadQueryInitFor = nullptr;

  const plPhysicsWorldModuleInterface* m_pPhysicsForQueries = nullptr; // valid during UpdateExecute's parallel section

  // ---- agent probes ----

  void RunAgentProbes(const plPhysicsWorldModuleInterface* pPhysics);
  void ProbeAgent(plGameObject* pOwner, const plPhysicsWorldModuleInterface* pPhysics);

  struct AgentSlot
  {
    plComponentHandle m_hComponent;
    plTime m_NextProbe;
    bool m_bInUse = false;
  };

  plDeque<AgentSlot> m_AgentSlots;
  plDynamicArray<AgentID> m_FreeAgentSlots;
  plUInt32 m_uiProbeCursor = 0;

  struct DebugQuery
  {
    plTime m_Expiry;
    plHybridArray<plAiTacticalQueryResult::Candidate, 16> m_TopN;
    plVec3 m_vQuerier;
  };

  plDeque<DebugQuery> m_DebugQueries;

  // stats
  plUInt32 m_uiStatBakesInFlight = 0;
  plUInt32 m_uiStatTotalPoints = 0;
  plUInt32 m_uiStatBakedSectors = 0;
  plUInt32 m_uiStatQueriesExecuted = 0;
  plAtomicInteger32 m_uiStatRaycastsThisFrame;
};
