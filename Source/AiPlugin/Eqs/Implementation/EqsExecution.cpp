#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsCapture.h>
#include <AiPlugin/Eqs/EqsWorldModule.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <Core/World/World.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Math/Random.h>

extern plCVarInt cvar_EqsMaxRaycastsPerQuery;

// The worker-side EQS pipeline: generate -> cheap tests -> (sort best-first) -> budgeted expensive
// tests -> finalize. Runs inside plAiEqsWorldModule::UpdateExecute's parallel batch; a query whose
// expensive phase exhausts its budget saves its (test, item) cursor and resumes here next frame.

namespace
{
  /// Cheap-first, stable order of the descriptor's tests.
  void BuildTestOrder(const plAiEqsQueryDesc& desc, plHybridArray<plUInt16, 16>& out_order, plUInt16& out_uiFirstExpensive)
  {
    for (plUInt16 i = 0; i < desc.m_Tests.GetCount(); ++i)
    {
      if (desc.m_Tests[i].m_pTest != nullptr && desc.m_Tests[i].m_pTest->GetCost() == plAiEqsTest::Cost::Cheap)
      {
        out_order.PushBack(i);
      }
    }

    out_uiFirstExpensive = static_cast<plUInt16>(out_order.GetCount());

    for (plUInt16 i = 0; i < desc.m_Tests.GetCount(); ++i)
    {
      if (desc.m_Tests[i].m_pTest != nullptr && desc.m_Tests[i].m_pTest->GetCost() == plAiEqsTest::Cost::Expensive)
      {
        out_order.PushBack(i);
      }
    }
  }

  void SortByPartialScore(plDynamicArray<plAiEqsItem>& ref_items)
  {
    for (auto& item : ref_items)
    {
      item.m_fFinal = (item.m_fWeightSum > 0.0f) ? (item.m_fScoreSum / item.m_fWeightSum) : 1.0f;
    }

    ref_items.Sort([](const plAiEqsItem& lhs, const plAiEqsItem& rhs) { return lhs.m_fFinal > rhs.m_fFinal; });
  }
} // namespace

void plAiEqsWorldModule::ExecuteQueryJob(plUInt32 uiSlotIndex, plUInt32 uiScratchIndex)
{
  QuerySlot& slot = m_Queries[uiSlotIndex];
  const plAiEqsQueryDesc* pDesc = slot.m_pDesc;

  if (pDesc == nullptr)
  {
    slot.m_Result.m_Status = plAiEqsQueryResult::Status::NoResult;
    slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
    return;
  }

  Scratch& scratch = m_Scratch[uiScratchIndex];

  if (scratch.m_pInitializedFor != slot.m_pNavMesh)
  {
    if (dtStatusFailed(scratch.m_NavQuery.init(slot.m_pNavMesh->GetDetourNavMesh(), 512)))
    {
      slot.m_Result.m_Status = plAiEqsQueryResult::Status::NoResult;
      slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
      return;
    }

    scratch.m_pInitializedFor = slot.m_pNavMesh;
  }

  plAiEqsEvalContext ctx;
  ctx.m_pDesc = pDesc;
  ctx.m_vQuerier = slot.m_vQuerier;
  ctx.m_hQuerier = slot.m_Params.m_hQuerier;
  ctx.m_ResolvedSlots = slot.m_ResolvedSlots.GetArrayPtr();
  ctx.m_PreCollectedItems = slot.m_PreCollected.GetArrayPtr();
  ctx.m_pNavQuery = &scratch.m_NavQuery;
  ctx.m_pFilter = slot.m_pFilter;
  ctx.m_pPhysics = m_pPhysicsForQueries;
  ctx.m_pTactical = m_pTacticalForQueries;
  ctx.m_pThreat = slot.m_pThreat.Borrow();
  ctx.m_uiCollisionLayer = slot.m_pNavMesh->GetConfig().m_uiCollisionLayer;
  ctx.m_uiRngSeed = uiSlotIndex * 977 + slot.m_Result.m_uiFrameStamp;
  ctx.m_uiMaxCandidates = plMath::Min<plUInt32>(plMath::Max<plUInt8>(pDesc->m_uiCandidates, 1), 64);
  ctx.m_iRaycastBudget = plMath::Clamp<plInt32>(cvar_EqsMaxRaycastsPerQuery, 1, 256);
  ctx.m_pPathQueryBudget = &m_iPathQueryBudget;
  ctx.m_pStatRaycasts = &m_uiStatRaycastsThisFrame;

  // ---- generate ----

  if (slot.m_Phase == QuerySlot::Phase::Generate)
  {
    slot.m_Items.Clear();
    slot.m_Items.Reserve(ctx.m_uiMaxCandidates);
    pDesc->m_pGenerator->Generate(ctx, slot.m_Items);

    if (slot.m_Items.GetCount() > ctx.m_uiMaxCandidates)
    {
      slot.m_Items.SetCount(ctx.m_uiMaxCandidates);
    }

    if (slot.m_Items.IsEmpty())
    {
      slot.m_Result.m_Status = plAiEqsQueryResult::Status::NoResult;
      slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
      slot.m_Phase = QuerySlot::Phase::Done;
      return;
    }

    slot.m_Phase = QuerySlot::Phase::Tests;
    slot.m_StageSummary.SetFormat("{} generated", slot.m_Items.GetCount());
    slot.m_uiTestCursor = 0;
    slot.m_uiItemCursor = 0;
  }

  // ---- tests, cheap first, expensive best-first and budgeted ----

  plHybridArray<plUInt16, 16> testOrder;
  plUInt16 uiFirstExpensive = 0;
  BuildTestOrder(*pDesc, testOrder, uiFirstExpensive);

  while (slot.m_uiTestCursor < testOrder.GetCount())
  {
    const plUInt16 uiTestIndex = testOrder[slot.m_uiTestCursor];
    const plAiEqsTestDesc& testDesc = pDesc->m_Tests[uiTestIndex];
    const plAiEqsTest& test = *testDesc.m_pTest;

    // spend the expensive budget on the most promising candidates: sort by the partial score
    // accumulated by the cheap tests when entering the expensive phase
    if (slot.m_uiTestCursor == uiFirstExpensive && slot.m_uiItemCursor == 0 && uiFirstExpensive < testOrder.GetCount())
    {
      SortByPartialScore(slot.m_Items);
    }

    for (plUInt32 i = slot.m_uiItemCursor; i < slot.m_Items.GetCount(); ++i)
    {
      slot.m_Items[i].m_TestData = plAiEqsTestData::Valid;
      slot.m_Items[i].m_bHasMeasurement = false;
      slot.m_Items[i].m_bReachable = false;
      slot.m_Items[i].m_bDirectPath = false;
    }
    const plUInt32 uiProcessed = test.Run(ctx, slot.m_Items.GetArrayPtr(), slot.m_uiItemCursor);

    const plUInt32 uiEnd = slot.m_uiItemCursor + uiProcessed;

    for (plUInt32 i = slot.m_uiItemCursor; i < uiEnd; ++i)
    {
      plAiEqsItem& item = slot.m_Items[i];

      if (!testDesc.ProcessItem(test, item, uiTestIndex))
      {
        slot.m_DiscardedDebug.PushBack(item);
        slot.m_StageSummary.AppendFormat(" -> T{} failed: {}", uiTestIndex + 1, plAiEqsTestDataName(item.m_TestData));
        slot.m_Result.m_Status = plAiEqsQueryResult::Status::MissingData;
        slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
        slot.m_Phase = QuerySlot::Phase::Done;
        return;
      }
    }

    slot.m_uiItemCursor = static_cast<plUInt16>(uiEnd);

    if (uiEnd < slot.m_Items.GetCount())
    {
      // budget exhausted mid-test: suspend, resume at this exact test+item next frame
      return;
    }

    // test complete: drop discarded items before the next test runs
    for (plUInt32 i = slot.m_Items.GetCount(); i-- > 0;)
    {
      if (slot.m_Items[i].m_bDiscarded)
      {
        slot.m_DiscardedDebug.PushBack(slot.m_Items[i]);
        slot.m_Items.RemoveAtAndSwap(i);
      }
    }

    slot.m_StageSummary.AppendFormat(" -> T{}: {}", uiTestIndex + 1, slot.m_Items.GetCount());
    if (slot.m_Items.IsEmpty())
    {
      slot.m_Result.m_Status = plAiEqsQueryResult::Status::NoResult;
      slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
      slot.m_Phase = QuerySlot::Phase::Done;
      return;
    }

    ++slot.m_uiTestCursor;
    slot.m_uiItemCursor = 0;
  }

  FinalizeQuery(slot, ctx.m_uiRngSeed);
}

void plAiEqsWorldModule::FinalizeQuery(QuerySlot& slot, plUInt32 uiSeed)
{
  const plAiEqsQueryDesc* pDesc = slot.m_pDesc;

  for (auto& item : slot.m_Items)
  {
    item.m_fFinal = (item.m_fWeightSum > 0.0f) ? (item.m_fScoreSum / item.m_fWeightSum) : 1.0f;
  }

  slot.m_Items.Sort([](const plAiEqsItem& lhs, const plAiEqsItem& rhs) { return lhs.m_fFinal > rhs.m_fFinal; });

  // ---- run mode ----

  // the query's own seed: queries finishing in the same frame must not all pick the same rank
  slot.m_uiSelectSeed = uiSeed;
  const plUInt32 uiPick = plAiEqsSelectCandidate(slot.m_Items.GetCount(), static_cast<plAiEqsRunMode::Enum>(pDesc->m_RunMode.GetValue()), pDesc->m_fTopPercent, uiSeed);

  if (uiPick != 0)
  {
    plMath::Swap(slot.m_Items[0], slot.m_Items[uiPick]);
  }

  // ---- fill the result ----

  plUInt8 uiMaxResults = (slot.m_Params.m_uiMaxResultsOverride != 0) ? slot.m_Params.m_uiMaxResultsOverride : pDesc->m_uiMaxResults;
  uiMaxResults = plMath::Clamp<plUInt8>(uiMaxResults, 1, 16);

  if (pDesc->m_RunMode == plAiEqsRunMode::SingleBest)
  {
    uiMaxResults = plMath::Min<plUInt8>(uiMaxResults, 8); // keep a few fallbacks for claim races
  }

  const plUInt8 uiRecordedTests = static_cast<plUInt8>(plMath::Min<plUInt32>(pDesc->m_Tests.GetCount(), plAiEqsItem::MaxRecordedTests));

  slot.m_Result.m_TopN.Clear();

  const plUInt32 uiResults = plMath::Min<plUInt32>(uiMaxResults, slot.m_Items.GetCount());

  for (plUInt32 i = 0; i < uiResults; ++i)
  {
    const plAiEqsItem& item = slot.m_Items[i];

    if (item.m_fFinal <= 0.0f)
      break;

    auto& result = slot.m_Result.m_TopN.ExpandAndGetRef();
    result.m_vPosition = item.m_vPosition;
    result.m_fScore = item.m_fFinal;
    result.m_hCover = item.m_hCover;
    result.m_hSmartObject = item.m_hSmartObject;
    result.m_hObject = item.m_hObject;
    result.m_Payload = item.m_Payload;
    result.m_uiRecordedTests = uiRecordedTests;

    for (plUInt32 t = 0; t < uiRecordedTests; ++t)
    {
      result.m_TestScores[t] = item.m_TestScores[t];
      result.m_TestTrace[t] = item.m_TestTrace[t];
    }
  }

  slot.m_Result.m_Status = slot.m_Result.m_TopN.IsEmpty() ? plAiEqsQueryResult::Status::NoResult : plAiEqsQueryResult::Status::Ready;
  slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
  slot.m_Phase = QuerySlot::Phase::Done;
}

plUInt32 plAiEqsSelectCandidate(plUInt32 uiCount, plAiEqsRunMode::Enum runMode, float fTopPercent, plUInt32 uiSeed)
{
  if (runMode != plAiEqsRunMode::RandomOfTopPercent || uiCount <= 1)
    return 0;

  const float fPercent = plMath::Clamp(fTopPercent, 1.0f, 100.0f);
  const plUInt32 uiPool = plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Ceil(uiCount * fPercent / 100.0f)), 1, uiCount);

  plRandom rng;
  rng.Initialize((static_cast<plUInt64>(uiSeed) + 1) * 0x9E3779B97F4A7C15ull);

  return rng.UIntInRange(uiPool);
}

void plAiEqsReplayCapture(const plAiEqsQueryDesc& desc, const plAiEqsCapture& capture, plAiEqsReplayResult& out_result)
{
  plHybridArray<plUInt16, 16> testOrder;
  plUInt16 uiFirstExpensive = 0;
  BuildTestOrder(desc, testOrder, uiFirstExpensive);

  out_result.m_Candidates.Clear();
  out_result.m_Candidates.SetCount(capture.m_Candidates.GetCount());
  out_result.m_uiMissingSamples = 0;
  out_result.m_iSelected = -1;

  plDynamicArray<plUInt32> survivors;

  for (plUInt32 i = 0; i < capture.m_Candidates.GetCount(); ++i)
  {
    const plAiEqsCapture::Candidate& captured = capture.m_Candidates[i];
    plAiEqsReplayResult::Candidate& replayed = out_result.m_Candidates[i];

    // feed the captured measurements through the same processing the runtime applies after each test
    plAiEqsItem item;

    for (plUInt16 uiTestIndex : testOrder)
    {
      if (uiTestIndex >= captured.m_Samples.GetCount() || !captured.m_Samples[uiTestIndex].m_bMeasured)
      {
        replayed.m_bMissingSample = true;
        continue;
      }

      const plAiEqsCapture::Sample& sample = captured.m_Samples[uiTestIndex];
      item.m_fRaw = sample.m_fRaw;
      item.m_fMeasurement = sample.m_fMeasurement;
      item.m_bHasMeasurement = sample.m_bHasMeasurement;
      item.m_TestData = sample.m_Data;

      const plAiEqsTestDesc& testDesc = desc.m_Tests[uiTestIndex];

      if (!testDesc.ProcessItem(*testDesc.m_pTest, item, uiTestIndex))
      {
        // a FailQuery policy hit unavailable data: the whole query would fail, this candidate can't win
        item.m_bDiscarded = true;
        item.m_uiRejectedBy = uiTestIndex;
      }

      if (item.m_bDiscarded)
        break;
    }

    replayed.m_bDiscarded = item.m_bDiscarded;
    replayed.m_uiRejectedBy = item.m_uiRejectedBy;
    replayed.m_fFinal = item.m_bDiscarded ? 0.0f : ((item.m_fWeightSum > 0.0f) ? (item.m_fScoreSum / item.m_fWeightSum) : 1.0f);

    if (replayed.m_bMissingSample)
    {
      ++out_result.m_uiMissingSamples;
    }

    if (!replayed.m_bDiscarded)
    {
      survivors.PushBack(i);
    }
  }

  survivors.Sort([&](plUInt32 lhs, plUInt32 rhs) { return out_result.m_Candidates[lhs].m_fFinal > out_result.m_Candidates[rhs].m_fFinal; });

  if (survivors.IsEmpty())
    return;

  const plUInt32 uiPick = plAiEqsSelectCandidate(survivors.GetCount(), static_cast<plAiEqsRunMode::Enum>(desc.m_RunMode.GetValue()), desc.m_fTopPercent, capture.m_uiSeed);

  if (out_result.m_Candidates[survivors[uiPick]].m_fFinal > 0.0f)
  {
    out_result.m_iSelected = static_cast<plInt32>(survivors[uiPick]);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Implementation_EqsExecution);
