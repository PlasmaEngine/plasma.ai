#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <AiPlugin/Tactical/ThreatMap.h>
#include <Core/World/WorldModule.h>
#include <DetourNavMeshQuery.h>
#include <Foundation/Threading/AtomicInteger.h>
#include <Foundation/Types/SharedPtr.h>

class plAiNavMesh;
class plAiTacticalWorldModule;
struct plAiEqsCapture;
class plPhysicsWorldModuleInterface;

/// \brief What the EQS scheduler did in its last update, for status displays.
struct plAiEqsFrameStats
{
  plUInt32 m_uiExecuted = 0;  ///< queries that ran this frame
  plUInt32 m_uiSuspended = 0; ///< of those, suspended on the raycast or path budget
  plUInt32 m_uiQueued = 0;    ///< waiting for a later frame
  plUInt32 m_uiTimedOut = 0;  ///< since the world started
  plUInt32 m_uiRaycasts = 0;  ///< this frame
};

/// \brief The Environment Query System: budgeted, asynchronous execution of data-driven spatial
/// queries (plAiEqsQueryResource assets).
///
/// Submit on the main thread at any time; contexts are snapshotted immediately, the pipeline
/// (generate -> cheap tests -> budgeted expensive tests -> finalize) runs in a parallel batch at
/// the end of the frame, results are readable the NEXT frame via TryGetResult (poll while
/// Pending). A query whose expensive phase exhausts its raycast/path budget suspends and resumes
/// at the exact test+item index instead of degrading result quality. Pending queries run in FIFO
/// order, resumed ones queue behind new submissions, and a query still pending after
/// AI.EQS.MaxQueryAge completes as TimedOut.
///
/// Call ReleaseQuery when done; releasing a pending query cancels it. Unreleased queries are
/// reclaimed automatically after a few seconds, but holding slots is wasteful.
class PL_AIPLUGIN_DLL plAiEqsWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiEqsWorldModule, plWorldModule);
  PL_DISALLOW_COPY_AND_ASSIGN(plAiEqsWorldModule);

public:
  plAiEqsWorldModule(plWorld* pWorld);
  ~plAiEqsWorldModule();

  virtual void Initialize() override;

  using QueryID = plUInt32;
  static constexpr QueryID InvalidQueryID = plInvalidIndex;

  QueryID SubmitQuery(const plAiEqsQueryResourceHandle& hQuery, const plAiEqsQueryParams& params);
  bool TryGetResult(QueryID id, plAiEqsQueryResult& out_result) const;
  void ReleaseQuery(QueryID id);

  /// \brief Records the next query that completes for the given querier, or for any querier when the handle is invalid.
  void CaptureNextQuery(const plGameObjectHandle& hQuerier);

  /// \brief Whether a capture is armed and waiting for a query.
  bool IsCaptureArmed() const { return m_bCaptureArmed; }

  /// \brief The most recent capture, or nullptr before the first one.
  const plAiEqsCapture* GetLastCapture() const { return m_pLastCapture.Borrow(); }

  /// \brief What the query scheduler did in its last update, for status displays.
  plAiEqsFrameStats GetFrameStats() const;

private:
  friend class plAiEqsQueryTestComponent;
  friend struct plAiEqsWorldModuleTestAccess; // AiSemanticsTests: queues queries without a built navmesh

  void UpdateExecute(const UpdateContext& ctxt); // PostAsync, after brain apply / tactical execute / squads

  struct QuerySlot
  {
    plAiEqsQueryResourceHandle m_hResource;
    plAiEqsQueryResource* m_pResource = nullptr; // acquired at submit, released when the slot frees
    const plAiEqsQueryDesc* m_pDesc = nullptr;   // pinned via the acquired resource
    plAiEqsQueryParams m_Params;
    plAiEqsQueryResult m_Result;

    plHybridArray<plAiEqsResolvedSlot, 4> m_ResolvedSlots; // [0] = Querier
    plDynamicArray<plAiEqsItem> m_PreCollected;            // main-thread generator collection
    plDynamicArray<plAiEqsItem> m_Items;
    plDynamicArray<plAiEqsItem> m_DiscardedDebug; // bounded by the query's candidate limit

    plVec3 m_vQuerier = plVec3::MakeZero();
    plVec3 m_vGeneratorCenter = plVec3::MakeZero();
    plUInt32 m_uiResourceChangeCounter = 0; // a different counter on resume means the asset was reloaded
    plUInt32 m_uiSelectSeed = 0;            // the seed the result selection used (captures replay it)
    plAiNavMesh* m_pNavMesh = nullptr;
    const dtQueryFilter* m_pFilter = nullptr;
    plSharedPtr<const plAiThreatSnapshot> m_pThreat; // the querier team's threat map at submit

    enum class Phase : plUInt8
    {
      Generate,
      Tests,
      Done
    };

    Phase m_Phase = Phase::Generate;
    plUInt16 m_uiTestCursor = 0; // index into the cheap-first test order
    plUInt16 m_uiItemCursor = 0; // resume index within the current test

    plTime m_CompletedAt;
    plTime m_SubmittedAt;
    plStringBuilder m_StageSummary;
    plUInt8 m_uiGeneration = 0;
    bool m_bInUse = false;
  };

  static QueryID MakeQueryID(plUInt32 uiSlot, plUInt8 uiGeneration) { return uiSlot | (static_cast<plUInt32>(uiGeneration) << 24); }
  static plUInt32 QuerySlotIndex(QueryID id) { return id & 0x00FFFFFF; }
  static plUInt8 QueryGeneration(QueryID id) { return static_cast<plUInt8>(id >> 24); }

  const QuerySlot* GetSlotChecked(QueryID id) const;
  void FreeSlot(plUInt32 uiSlot);
  void SweepQuerySlots();

  void CompleteQuery(QuerySlot& slot, plAiEqsQueryResult::Status status);
  void RestartQuery(QuerySlot& slot); // after the asset was reloaded: the tests and generator were replaced
  void PreparePendingQueries();       // drops timed-out queries, restarts reloaded ones

  void ExecuteQueryJob(plUInt32 uiSlotIndex, plUInt32 uiScratchIndex); // worker; EqsExecution.cpp
  void FinalizeQuery(QuerySlot& slot, plUInt32 uiSeed);               // worker; EqsExecution.cpp

  void RetainDebugQuery(const QuerySlot& slot);
  void RecordCapture(const QuerySlot& slot);
  void DrawQueryVisualization();
  void DrawStats();

  plDeque<QuerySlot> m_Queries;
  plDynamicArray<plUInt32> m_FreeQueries;
  plDynamicArray<plUInt32> m_PendingQueue; // slot indices waiting for (or resuming) execution, FIFO
  plDynamicArray<plUInt32> m_ExecuteList;

  struct Scratch
  {
    dtNavMeshQuery m_NavQuery;
    const plAiNavMesh* m_pInitializedFor = nullptr;
  };

  plDeque<Scratch> m_Scratch;
  plAtomicInteger32 m_uiScratchCounter;

  const plPhysicsWorldModuleInterface* m_pPhysicsForQueries = nullptr; // valid during UpdateExecute's parallel section
  const plAiTacticalWorldModule* m_pTacticalForQueries = nullptr;      // valid during UpdateExecute's parallel section

  plAtomicInteger32 m_iPathQueryBudget; // shared per-frame budget, decremented by PathLength/ReachableApprox tests

  struct DebugQuery
  {
    plTime m_Expiry;
    plVec3 m_vQuerier;
    plHybridArray<plAiEqsQueryResult::Candidate, 16> m_TopN;
    plHybridArray<plAiEqsItem, 32> m_Discarded;
    plString m_sName;
    plString m_sStageSummary;
    plTime m_SubmittedAt;
    plHybridArray<plAiEqsResolvedSlot, 4> m_Contexts;
    plHybridArray<plString, 12> m_TestNames;
  };

  plDeque<DebugQuery> m_DebugQueries;

  bool m_bCaptureArmed = false;
  plGameObjectHandle m_hCaptureQuerier;
  plUniquePtr<plAiEqsCapture> m_pLastCapture;

  // stats
  plUInt32 m_uiStatQueriesExecuted = 0;
  plUInt32 m_uiStatQueriesSuspended = 0;
  plUInt32 m_uiStatQueriesSubmitted = 0;
  plUInt32 m_uiStatQueriesTimedOut = 0; // since the world started
  plAtomicInteger32 m_uiStatRaycastsThisFrame;
};
