#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <Core/World/WorldModule.h>
#include <DetourNavMeshQuery.h>
#include <Foundation/Threading/AtomicInteger.h>

class plAiNavMesh;
class plAiTacticalWorldModule;
class plPhysicsWorldModuleInterface;

/// \brief The Environment Query System: budgeted, asynchronous execution of data-driven spatial
/// queries (plAiEqsQueryResource assets).
///
/// Submit on the main thread at any time; contexts are snapshotted immediately, the pipeline
/// (generate -> cheap tests -> budgeted expensive tests -> finalize) runs in a parallel batch at
/// the end of the frame, results are readable the NEXT frame via TryGetResult (poll while
/// Pending). A query whose expensive phase exhausts its raycast/path budget suspends and resumes
/// next frame at the exact test+item index instead of degrading result quality.
///
/// Call ReleaseQuery when done - unreleased queries are reclaimed automatically after a few
/// seconds, but holding slots is wasteful.
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

private:
  friend class plAiEqsQueryTestComponent;

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
    plDynamicArray<plVec3> m_DiscardedDebug;               // positions of filtered-out items (visualization)

    plVec3 m_vQuerier = plVec3::MakeZero();
    plAiNavMesh* m_pNavMesh = nullptr;
    const dtQueryFilter* m_pFilter = nullptr;

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
    plUInt8 m_uiGeneration = 0;
    bool m_bInUse = false;
  };

  static QueryID MakeQueryID(plUInt32 uiSlot, plUInt8 uiGeneration) { return uiSlot | (static_cast<plUInt32>(uiGeneration) << 24); }
  static plUInt32 QuerySlotIndex(QueryID id) { return id & 0x00FFFFFF; }
  static plUInt8 QueryGeneration(QueryID id) { return static_cast<plUInt8>(id >> 24); }

  const QuerySlot* GetSlotChecked(QueryID id) const;
  void FreeSlot(plUInt32 uiSlot);
  void SweepQuerySlots();

  void ExecuteQueryJob(plUInt32 uiSlotIndex, plUInt32 uiScratchIndex); // worker; EqsExecution.cpp
  void FinalizeQuery(QuerySlot& slot);                                // worker; EqsExecution.cpp

  void RetainDebugQuery(const QuerySlot& slot);
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
    plHybridArray<plVec3, 32> m_Discarded;
    plString m_sName;
  };

  plDeque<DebugQuery> m_DebugQueries;

  // stats
  plUInt32 m_uiStatQueriesExecuted = 0;
  plUInt32 m_uiStatQueriesSuspended = 0;
  plUInt32 m_uiStatQueriesSubmitted = 0;
  plAtomicInteger32 m_uiStatRaycastsThisFrame;
};
