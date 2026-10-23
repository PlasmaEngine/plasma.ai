#include <AiPlugin/Eqs/EqsCapture.h>
#include <AiPlugin/Eqs/EqsQueryResource.h>
#include <AiPlugin/Eqs/EqsWorldModule.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/IO/MemoryStream.h>
#include <Tests/AiTestCheck.h>
#include <Tests/AiTestWorld.h>

namespace
{
  plUInt32 s_uiGenerateCalls = 0;

  plCVarFloat& GetMaxQueryAgeCVar()
  {
    return *static_cast<plCVarFloat*>(plCVar::FindCVarByName("AI.EQS.MaxQueryAge"));
  }
} // namespace

/// \brief Generates a row of items one meter apart.
class plAiTestEqsGenerator : public plAiEqsGenerator
{
  PL_ADD_DYNAMIC_REFLECTION(plAiTestEqsGenerator, plAiEqsGenerator);

public:
  virtual void Generate(plAiEqsEvalContext& ref_ctx, plDynamicArray<plAiEqsItem>& out_items) const override
  {
    ++s_uiGenerateCalls;

    for (plUInt32 i = 0; i < plMath::Min(m_uiCount, ref_ctx.m_uiMaxCandidates); ++i)
    {
      out_items.ExpandAndGetRef().m_vPosition = plVec3(static_cast<float>(i), 0, 0);
    }
  }

  plUInt32 m_uiCount = 8;
};

/// \brief An expensive test that processes one item per run, so a query takes one frame per item.
class plAiTestEqsTest_OnePerRun : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiTestEqsTest_OnePerRun, plAiEqsTest);

public:
  virtual Cost GetCost() const override { return Cost::Expensive; }

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override
  {
    if (uiFirstItem >= items.GetCount())
      return 0;

    items[uiFirstItem].m_fRaw = 1.0f;
    return 1;
  }
};

/// \brief An expensive test that never processes an item, like a test whose budget never suffices.
class plAiTestEqsTest_Stuck : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiTestEqsTest_Stuck, plAiEqsTest);

public:
  virtual Cost GetCost() const override { return Cost::Expensive; }

  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override { return 0; }
};

/// \brief A cheap test that measures an item's x coordinate (raw = x / 10).
class plAiTestEqsTest_ByX : public plAiEqsTest
{
  PL_ADD_DYNAMIC_REFLECTION(plAiTestEqsTest_ByX, plAiEqsTest);

public:
  virtual plUInt32 Run(plAiEqsEvalContext& ref_ctx, plArrayPtr<plAiEqsItem> items, plUInt32 uiFirstItem) const override
  {
    for (plUInt32 i = uiFirstItem; i < items.GetCount(); ++i)
    {
      items[i].m_fRaw = items[i].m_vPosition.x / 10.0f;
      items[i].m_fMeasurement = items[i].m_vPosition.x;
      items[i].m_bHasMeasurement = true;
    }

    return items.GetCount() - uiFirstItem;
  }
};

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiTestEqsTest_ByX, 1, plRTTIDefaultAllocator<plAiTestEqsTest_ByX>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiTestEqsGenerator, 1, plRTTIDefaultAllocator<plAiTestEqsGenerator>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiTestEqsTest_OnePerRun, 1, plRTTIDefaultAllocator<plAiTestEqsTest_OnePerRun>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiTestEqsTest_Stuck, 1, plRTTIDefaultAllocator<plAiTestEqsTest_Stuck>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

/// \brief Queues queries the way SubmitQuery does, minus the navmesh sector check, which needs a
/// physics scene to build sectors.
struct plAiEqsWorldModuleTestAccess
{
  using QueryID = plAiEqsWorldModule::QueryID;

  static QueryID Queue(plAiEqsWorldModule& ref_eqs, const plAiEqsQueryResourceHandle& hQuery, plAiNavMesh& ref_navMesh)
  {
    plAiEqsQueryResource* pResource = plResourceManager::BeginAcquireResource(hQuery, plResourceAcquireMode::BlockTillLoaded_NeverFail);

    plUInt32 uiSlot = ref_eqs.m_Queries.GetCount();

    if (!ref_eqs.m_FreeQueries.IsEmpty())
    {
      uiSlot = ref_eqs.m_FreeQueries.PeekBack();
      ref_eqs.m_FreeQueries.PopBack();
    }
    else
    {
      ref_eqs.m_Queries.ExpandAndGetRef();
    }

    plAiEqsWorldModule::QuerySlot& slot = ref_eqs.m_Queries[uiSlot];
    slot.m_hResource = hQuery;
    slot.m_pResource = pResource;
    slot.m_uiResourceChangeCounter = pResource->GetCurrentResourceChangeCounter();
    slot.m_pDesc = &pResource->GetDescriptor();
    slot.m_Params = plAiEqsQueryParams();
    slot.m_pNavMesh = &ref_navMesh;
    slot.m_pFilter = nullptr;
    slot.m_Result.m_TopN.Clear();
    slot.m_Result.m_uiFrameStamp = ref_eqs.GetWorld()->GetUpdateCounter();
    slot.m_Result.m_Status = plAiEqsQueryResult::Status::Pending;
    slot.m_ResolvedSlots.Clear();
    slot.m_ResolvedSlots.ExpandAndGetRef().m_sName.Assign("Querier");
    slot.m_PreCollected.Clear();
    slot.m_Items.Clear();
    slot.m_DiscardedDebug.Clear();
    slot.m_SubmittedAt = ref_eqs.GetWorld()->GetClock().GetAccumulatedTime();
    slot.m_StageSummary = "0 generated";
    slot.m_Phase = plAiEqsWorldModule::QuerySlot::Phase::Generate;
    slot.m_uiTestCursor = 0;
    slot.m_uiItemCursor = 0;
    slot.m_bInUse = true;

    ref_eqs.m_PendingQueue.PushBack(uiSlot);
    return plAiEqsWorldModule::MakeQueryID(uiSlot, slot.m_uiGeneration);
  }

  static plUInt32 GetPendingCount(const plAiEqsWorldModule& eqs) { return eqs.m_PendingQueue.GetCount(); }
};

namespace
{
  using QueryID = plAiEqsWorldModule::QueryID;
  using Status = plAiEqsQueryResult::Status;

  template <typename T>
  plUniquePtr<T> MakeUnique()
  {
    return plUniquePtr<T>(PL_DEFAULT_NEW(T));
  }

  template <typename TestType>
  void AddTest(plAiEqsQueryDesc& ref_desc)
  {
    plUniquePtr<TestType> pTest = MakeUnique<TestType>();
    pTest->m_Purpose = plAiEqsTestPurpose::ScoreOnly;
    ref_desc.m_Tests.ExpandAndGetRef().m_pTest = std::move(pTest);
  }

  plAiEqsQueryResourceHandle CreateQuery(const char* szResourceID, plAiEqsQueryDesc&& desc)
  {
    desc.m_pGenerator = MakeUnique<plAiTestEqsGenerator>();
    return plResourceManager::CreateResource<plAiEqsQueryResource>(szResourceID, std::move(desc));
  }

  Status GetStatus(plAiEqsWorldModule& ref_eqs, QueryID id)
  {
    // TryGetResult only writes the status of unknown and completed queries
    plAiEqsQueryResult result;
    result.m_Status = Status::Pending;
    ref_eqs.TryGetResult(id, result);
    return result.m_Status;
  }

  /// A world with an EQS module and an empty navmesh for the queries' navmesh queries.
  struct EqsFixture
  {
    EqsFixture()
      : m_NavMesh(plAiNavmeshConfig())
      , m_World("AiTest EQS")
    {
      PL_LOCK(m_World.GetWorld().GetWriteMarker());
      m_pEqs = m_World.GetWorld().GetOrCreateModule<plAiEqsWorldModule>();
    }

    QueryID Queue(const plAiEqsQueryResourceHandle& hQuery)
    {
      PL_LOCK(m_World.GetWorld().GetWriteMarker());
      return plAiEqsWorldModuleTestAccess::Queue(*m_pEqs, hQuery, m_NavMesh);
    }

    plAiNavMesh m_NavMesh; // outlives the world, whose queries point at it
    plAiTestWorld m_World;
    plAiEqsWorldModule* m_pEqs = nullptr;
  };

  //////////////////////////////////////////////////////////////////////////

  void TestStuckQueriesDontStarveOthers()
  {
    plAiEqsQueryDesc stuckDesc;
    AddTest<plAiTestEqsTest_Stuck>(stuckDesc);
    const plAiEqsQueryResourceHandle hStuck = CreateQuery("AiTest/R5/Stuck", std::move(stuckDesc));
    const plAiEqsQueryResourceHandle hQuick = CreateQuery("AiTest/R5/Quick", plAiEqsQueryDesc());

    EqsFixture fixture;

    // twice the per-frame dispatch of 8, ahead of the quick query
    for (plUInt32 i = 0; i < 16; ++i)
    {
      fixture.Queue(hStuck);
    }

    const QueryID quick = fixture.Queue(hQuick);

    fixture.m_World.Step(4);
    Check(GetStatus(*fixture.m_pEqs, quick) == Status::Ready, "R5: a query behind 16 stuck queries completes within a few frames");
  }

  void TestStuckQueryTimesOut()
  {
    plAiScopedCVar<plCVarFloat, float> maxAge(GetMaxQueryAgeCVar(), 0.5f);

    plAiEqsQueryDesc stuckDesc;
    AddTest<plAiTestEqsTest_Stuck>(stuckDesc);
    const plAiEqsQueryResourceHandle hStuck = CreateQuery("AiTest/R5/StuckTimeout", std::move(stuckDesc));

    EqsFixture fixture;
    const QueryID stuck = fixture.Queue(hStuck);

    fixture.m_World.Step(10);
    Check(GetStatus(*fixture.m_pEqs, stuck) == Status::Pending, "R5: a stuck query stays pending before its deadline");

    fixture.m_World.Step(40);
    Check(GetStatus(*fixture.m_pEqs, stuck) == Status::TimedOut, "R5: a stuck query completes as TimedOut after AI.EQS.MaxQueryAge");
    Check(plAiEqsWorldModuleTestAccess::GetPendingCount(*fixture.m_pEqs) == 0, "R5: the timed-out query left the queue");
  }

  void TestReleaseCancelsPendingQuery()
  {
    plAiEqsQueryDesc stuckDesc;
    AddTest<plAiTestEqsTest_Stuck>(stuckDesc);
    const plAiEqsQueryResourceHandle hStuck = CreateQuery("AiTest/R5/StuckRelease", std::move(stuckDesc));

    EqsFixture fixture;
    const QueryID stuck = fixture.Queue(hStuck);
    fixture.m_World.Step(2);

    {
      PL_LOCK(fixture.m_World.GetWorld().GetWriteMarker());
      fixture.m_pEqs->ReleaseQuery(stuck);
    }

    Check(plAiEqsWorldModuleTestAccess::GetPendingCount(*fixture.m_pEqs) == 0, "R5: releasing a pending query removes it from the queue");
    Check(GetStatus(*fixture.m_pEqs, stuck) == Status::Invalid, "R5: a released pending query is gone");
  }

  void TestReloadRestartsSuspendedQuery()
  {
    plAiEqsQueryDesc slowDesc;
    AddTest<plAiTestEqsTest_OnePerRun>(slowDesc);
    const plAiEqsQueryResourceHandle hSlow = CreateQuery("AiTest/T1/Slow", std::move(slowDesc));

    EqsFixture fixture;
    const QueryID slow = fixture.Queue(hSlow);

    s_uiGenerateCalls = 0;
    fixture.m_World.Step(3);
    Check(GetStatus(*fixture.m_pEqs, slow) == Status::Pending, "T1: the one-item-per-frame query is suspended");
    Check(s_uiGenerateCalls == 1, "T1: the generator ran once before the reload");

    // a reload replaces the tests and generator; the resource stays pinned by the query
    {
      plResourceLock<plAiEqsQueryResource> pQuery(hSlow, plResourceAcquireMode::BlockTillLoaded);
      const_cast<plAiEqsQueryDesc&>(pQuery->GetDescriptor()).m_Tests.Clear();
      pQuery->IncResourceChangeCounter();
    }

    fixture.m_World.Step(2);
    Check(s_uiGenerateCalls == 2, "T1: a query resumed after a reload starts over");
    Check(GetStatus(*fixture.m_pEqs, slow) == Status::Ready, "T1: the restarted query completes against the reloaded asset");
  }

  //////////////////////////////////////////////////////////////////////////
  // F7: capture and replay

  /// A query over items at x = 0..7: test 0 keeps x >= 2 and scores x / 10, test 1 scores x / 10 again.
  plAiEqsQueryDesc MakeByXQuery()
  {
    plAiEqsQueryDesc desc;

    plUniquePtr<plAiTestEqsTest_ByX> pFilter = MakeUnique<plAiTestEqsTest_ByX>();
    pFilter->m_Purpose = plAiEqsTestPurpose::FilterAndScore;
    pFilter->m_FilterCondition = plAiEqsFilterCondition::AtLeast;
    pFilter->m_fFilterMin = 2.0f;
    desc.m_Tests.ExpandAndGetRef().m_pTest = std::move(pFilter);

    AddTest<plAiTestEqsTest_ByX>(desc);
    return desc;
  }

  const plAiEqsCapture* RunCapturedQuery(EqsFixture& ref_fixture, const plAiEqsQueryResourceHandle& hQuery)
  {
    ref_fixture.m_pEqs->CaptureNextQuery(plGameObjectHandle());
    ref_fixture.Queue(hQuery);
    ref_fixture.m_World.Step(2);
    return ref_fixture.m_pEqs->GetLastCapture();
  }

  void TestCapture()
  {
    const plAiEqsQueryResourceHandle hQuery = CreateQuery("AiTest/F7/Capture", MakeByXQuery());

    EqsFixture fixture;
    const plAiEqsCapture* pCapture = RunCapturedQuery(fixture, hQuery);

    Check(pCapture != nullptr && !fixture.m_pEqs->IsCaptureArmed(), "F7: the armed capture recorded the next query");
    if (pCapture == nullptr)
      return;

    Check(pCapture->m_Candidates.GetCount() == 8 && pCapture->m_Status == Status::Ready, "F7: the capture holds survivors and rejected candidates");
    Check(pCapture->m_iSelected == 0 && plMath::IsEqual(pCapture->m_Candidates[0].m_vPosition.x, 7.0f, 0.001f), "F7: the selected candidate is the best one");

    plUInt32 uiRejected = 0;
    bool bSamplesComplete = true;
    for (const plAiEqsCapture::Candidate& candidate : pCapture->m_Candidates)
    {
      uiRejected += candidate.m_bDiscarded ? 1 : 0;
      bSamplesComplete &= candidate.m_Samples.GetCount() == 2 && candidate.m_Samples[0].m_bMeasured && candidate.m_Samples[0].m_bHasMeasurement;
    }

    Check(uiRejected == 2, "F7: the two candidates below x = 2 are recorded as rejected");
    Check(bSamplesComplete, "F7: every candidate has the filter test's measurement");

    plDefaultMemoryStreamStorage storage;
    plMemoryStreamWriter writer(&storage);
    pCapture->Serialize(writer).IgnoreResult();

    plMemoryStreamReader reader(&storage);
    plAiEqsCapture loaded;
    Check(loaded.Deserialize(reader).Succeeded() && loaded.m_Candidates.GetCount() == 8 && loaded.m_uiSeed == pCapture->m_uiSeed &&
            loaded.m_Candidates[3].m_Samples[1].m_fRaw == pCapture->m_Candidates[3].m_Samples[1].m_fRaw,
      "F7: a saved capture loads back unchanged");
  }

  void TestReplay()
  {
    const plAiEqsQueryResourceHandle hQuery = CreateQuery("AiTest/F7/Replay", MakeByXQuery());

    EqsFixture fixture;
    const plAiEqsCapture* pCapture = RunCapturedQuery(fixture, hQuery);
    if (pCapture == nullptr)
    {
      Check(false, "F7: replay needs a capture");
      return;
    }

    plAiEqsQueryDesc desc = MakeByXQuery();
    plAiEqsReplayResult result;

    plAiEqsReplayCapture(desc, *pCapture, result);
    bool bSame = result.m_iSelected == pCapture->m_iSelected && result.m_uiMissingSamples == 0;
    for (plUInt32 i = 0; i < pCapture->m_Candidates.GetCount(); ++i)
    {
      bSame &= result.m_Candidates[i].m_bDiscarded == pCapture->m_Candidates[i].m_bDiscarded;
      bSame &= pCapture->m_Candidates[i].m_bDiscarded || plMath::IsEqual(result.m_Candidates[i].m_fFinal, pCapture->m_Candidates[i].m_fFinal, 0.0001f);
    }
    Check(bSame, "F7: replaying an unchanged query reproduces the capture");

    // invert the second test's score: low x now scores high, and the filter still keeps x >= 2
    desc.m_Tests[1].m_pTest->m_bInvertScore = true;
    desc.m_Tests[1].m_pTest->m_fWeight = 4.0f;
    plAiEqsReplayCapture(desc, *pCapture, result);
    Check(result.m_iSelected >= 0 && plMath::IsEqual(pCapture->m_Candidates[result.m_iSelected].m_vPosition.x, 2.0f, 0.001f), "F7: an edited weight and inversion change the winner on the same data");

    // tighten the filter: x >= 5 only
    desc = MakeByXQuery();
    desc.m_Tests[0].m_pTest->m_fFilterMin = 5.0f;
    plAiEqsReplayCapture(desc, *pCapture, result);

    plUInt32 uiSurvivors = 0;
    for (const auto& candidate : result.m_Candidates)
    {
      uiSurvivors += candidate.m_bDiscarded ? 0 : 1;
    }
    Check(uiSurvivors == 3, "F7: an edited filter rejects candidates on the captured measurements");

    // relax the filter: the candidates rejected in the capture never ran the second test
    desc.m_Tests[0].m_pTest->m_fFilterMin = 0.0f;
    plAiEqsReplayCapture(desc, *pCapture, result);
    Check(result.m_uiMissingSamples == 2, "F7: replay reports candidates that reach a test the capture has no sample for");
  }

  void TestSharedSelection()
  {
    Check(plAiEqsSelectCandidate(10, plAiEqsRunMode::SingleBest, 50.0f, 7) == 0, "R7: SingleBest selects the best candidate");

    const plUInt32 uiPick = plAiEqsSelectCandidate(10, plAiEqsRunMode::RandomOfTopPercent, 50.0f, 7);
    Check(uiPick < 5 && uiPick == plAiEqsSelectCandidate(10, plAiEqsRunMode::RandomOfTopPercent, 50.0f, 7), "R7: RandomOfTopPercent picks from the top half, the same way for the same seed");
  }

  void TestSelectionSeedPerQuery()
  {
    // ranks the survivors x = 2..7 by x; the top half is x = 5..7
    plAiEqsQueryDesc desc = MakeByXQuery();
    desc.m_RunMode = plAiEqsRunMode::RandomOfTopPercent;
    desc.m_fTopPercent = 50.0f;
    const plAiEqsQueryResourceHandle hQuery = CreateQuery("AiTest/T5/RandomOfTop", std::move(desc));

    EqsFixture fixture;

    plHybridArray<QueryID, 8> queries;
    for (plUInt32 i = 0; i < 8; ++i)
    {
      queries.PushBack(fixture.Queue(hQuery));
    }

    fixture.m_World.Step(2);

    plHybridArray<float, 8> picks;
    for (QueryID id : queries)
    {
      plAiEqsQueryResult result;
      if (fixture.m_pEqs->TryGetResult(id, result) && result.m_Status == Status::Ready && !result.m_TopN.IsEmpty())
      {
        picks.PushBack(result.m_TopN[0].m_vPosition.x);
      }
    }

    bool bDiffer = false;
    for (float fPick : picks)
    {
      bDiffer |= fPick != picks[0];
    }

    Check(picks.GetCount() == 8, "T5: the eight queries completed");
    Check(bDiffer, "T5: queries that finish in the same frame don't all pick the same rank");
  }
} // namespace

void RunEqsWorldTests()
{
  TestStuckQueriesDontStarveOthers();
  TestStuckQueryTimesOut();
  TestReleaseCancelsPendingQuery();
  TestReloadRestartsSuspendedQuery();
  TestCapture();
  TestReplay();
  TestSharedSelection();
  TestSelectionSeedPerQuery();
}
