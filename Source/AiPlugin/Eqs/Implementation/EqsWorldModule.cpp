#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Eqs/EqsWorldModule.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/World/World.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Threading/TaskSystem.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>

plCVarInt cvar_EqsMaxQueriesPerFrame("AI.EQS.MaxQueriesPerFrame", 8, plCVarFlags::Default, "How many EQS queries may execute per frame; excess stays queued.");
plCVarInt cvar_EqsMaxRaycastsPerQuery("AI.EQS.MaxRaycastsPerQuery", 24, plCVarFlags::Default, "Line-of-sight raycast budget per EQS query per frame; queries suspend and resume when it runs out.");
plCVarInt cvar_EqsMaxPathQueriesPerFrame("AI.EQS.MaxPathQueriesPerFrame", 32, plCVarFlags::Default, "Shared per-frame budget for navmesh path/reachability tests across all EQS queries.");
plCVarInt cvar_EqsVisualizeQueries("AI.EQS.VisualizeQueries", 0, plCVarFlags::Default, "Visualize EQS query results: 0 = off, 1 = on (score-colored candidates, winner highlighted, filtered items as crosses).");
plCVarBool cvar_EqsVisualizeScores("AI.EQS.VisualizeScores", false, plCVarFlags::Default, "Draw the per-test score breakdown next to the winning EQS candidate.");
plCVarBool cvar_EqsShowStats("AI.EQS.Stats", false, plCVarFlags::Default, "Show EQS statistics on screen.");

// clang-format off
PL_IMPLEMENT_WORLD_MODULE(plAiEqsWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiEqsWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiEqsWorldModule::plAiEqsWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
}

plAiEqsWorldModule::~plAiEqsWorldModule()
{
  for (plUInt32 uiSlot = 0; uiSlot < m_Queries.GetCount(); ++uiSlot)
  {
    if (m_Queries[uiSlot].m_bInUse)
    {
      FreeSlot(uiSlot);
    }
  }
}

void plAiEqsWorldModule::Initialize()
{
  SUPER::Initialize();

  // PostAsync, after plAiBrainWorldModule::UpdateApply (0), plAiTacticalWorldModule::UpdateExecute
  // (-1000) and plAiSquadWorldModule::UpdateSquads (-1100), so queries submitted by fresh behavior
  // activations still start this frame. Low priority instead of m_DependsOn: a missing dependency
  // target hard-asserts in plWorld, and none of those modules is guaranteed to exist.
  auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiEqsWorldModule::UpdateExecute, this);
  updateDesc.m_Phase = plWorldModule::UpdateFunctionDesc::Phase::PostAsync;
  updateDesc.m_bOnlyUpdateWhenSimulating = true;
  updateDesc.m_fPriority = -1200.0f;

  RegisterUpdateFunction(updateDesc);
}

plAiEqsWorldModule::QueryID plAiEqsWorldModule::SubmitQuery(const plAiEqsQueryResourceHandle& hQuery, const plAiEqsQueryParams& params)
{
  if (!hQuery.IsValid())
    return InvalidQueryID;

  auto* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>();
  if (pNavMeshModule == nullptr)
    return InvalidQueryID;

  // pin the resource for the slot's lifetime, so reloads cannot pull the descriptor from under
  // in-flight (possibly suspended) executions
  plResourceAcquireResult acquireResult = plResourceAcquireResult::None;
  plAiEqsQueryResource* pResource = plResourceManager::BeginAcquireResource(hQuery, plResourceAcquireMode::BlockTillLoaded_NeverFail, plAiEqsQueryResourceHandle(), &acquireResult);

  if (acquireResult != plResourceAcquireResult::Final)
  {
    if (pResource != nullptr)
    {
      plResourceManager::EndAcquireResource(pResource);
    }

    return InvalidQueryID;
  }

  const plAiEqsQueryDesc& desc = pResource->GetDescriptor();

  plAiNavMesh* pNavMesh = nullptr;

  if (!desc.m_sNavmeshConfig.IsEmpty())
  {
    pNavMesh = pNavMeshModule->GetNavMesh(desc.m_sNavmeshConfig.GetView());
  }
  else if (!pNavMeshModule->GetConfig().m_NavmeshConfigs.IsEmpty())
  {
    pNavMesh = pNavMeshModule->GetNavMesh(pNavMeshModule->GetConfig().m_NavmeshConfigs[0].m_sName);
  }

  if (pNavMesh == nullptr || desc.m_pGenerator == nullptr)
  {
    plResourceManager::EndAcquireResource(pResource);
    return InvalidQueryID;
  }

  plUInt32 uiSlot = plInvalidIndex;

  if (!m_FreeQueries.IsEmpty())
  {
    uiSlot = m_FreeQueries.PeekBack();
    m_FreeQueries.PopBack();
  }
  else
  {
    if (m_Queries.GetCount() >= 512)
    {
      plResourceManager::EndAcquireResource(pResource);
      return InvalidQueryID; // runaway leak guard; slots auto-reclaim after a few seconds
    }

    uiSlot = m_Queries.GetCount();
    m_Queries.ExpandAndGetRef();
  }

  QuerySlot& slot = m_Queries[uiSlot];
  slot.m_hResource = hQuery;
  slot.m_pResource = pResource;
  slot.m_pDesc = &desc;
  slot.m_Params = params;
  slot.m_pNavMesh = pNavMesh;
  slot.m_pFilter = &pNavMeshModule->GetPathSearchFilter(desc.m_sPathSearchConfig.GetView());
  slot.m_Result.m_TopN.Clear();
  slot.m_Result.m_uiFrameStamp = GetWorld()->GetUpdateCounter();
  slot.m_ResolvedSlots.Clear();
  slot.m_PreCollected.Clear();
  slot.m_Items.Clear();
  slot.m_DiscardedDebug.Clear();
  slot.m_SubmittedAt = GetWorld()->GetClock().GetAccumulatedTime();
  slot.m_StageSummary = "0 generated";
  slot.m_Phase = QuerySlot::Phase::Generate;
  slot.m_uiTestCursor = 0;
  slot.m_uiItemCursor = 0;
  slot.m_bInUse = true;

  // ---- resolve the querier ----

  plGameObject* pQuerier = nullptr;

  if (!params.m_hQuerier.IsInvalidated())
  {
    GetWorld()->TryGetObject(params.m_hQuerier, pQuerier);
  }

  if (params.m_bHasQuerierPosition)
  {
    slot.m_vQuerier = params.m_vQuerierPosition;
  }
  else if (pQuerier != nullptr)
  {
    slot.m_vQuerier = pQuerier->GetGlobalPosition();
  }
  else
  {
    slot.m_vQuerier = plVec3::MakeZero();
  }

  // ---- snapshot all contexts (main thread, submit time) ----

  plAiEqsResolveContext resolveCtx;
  resolveCtx.m_pWorld = GetWorld();
  resolveCtx.m_pQuerier = pQuerier;
  resolveCtx.m_vQuerierPosition = slot.m_vQuerier;
  resolveCtx.m_pParams = &params;

  plSharedPtr<plBlackboard> pBlackboard;

  if (pQuerier != nullptr)
  {
    pBlackboard = plBlackboardComponent::FindBlackboard(pQuerier);
    resolveCtx.m_pBlackboard = pBlackboard.Borrow();
  }

  {
    auto& querierSlot = slot.m_ResolvedSlots.ExpandAndGetRef();
    querierSlot.m_sName.Assign("Querier");
    querierSlot.m_Positions.PushBack(slot.m_vQuerier);

    if (pQuerier != nullptr)
    {
      querierSlot.m_Objects.PushBack(pQuerier->GetHandle());
    }
  }

  for (const auto& slotDesc : desc.m_ContextSlots)
  {
    if (slotDesc.m_pContext == nullptr || slotDesc.m_sName.IsEmpty())
      continue;

    auto& resolved = slot.m_ResolvedSlots.ExpandAndGetRef();
    resolved.m_sName = slotDesc.m_sName;

    // per-submit override: an explicit position passed under this slot's name replaces the
    // asset's binding entirely (the escape hatch for C++/test-component submits)
    bool bOverridden = false;

    for (const auto& named : params.m_Positions)
    {
      if (named.m_sName == slotDesc.m_sName)
      {
        resolved.m_Positions.PushBack(named.m_vPosition);
        bOverridden = true;
      }
    }

    if (!bOverridden)
    {
      resolveCtx.m_sSlotName = slotDesc.m_sName;
      slotDesc.m_pContext->Resolve(resolveCtx, resolved);
    }
  }

  // ---- generator center: sector request + main-thread pre-collection ----

  plVec3 vCenter = slot.m_vQuerier;

  {
    const plTempHashedString sCenter(desc.m_pGenerator->m_sCenterContext.GetData());

    for (const auto& resolved : slot.m_ResolvedSlots)
    {
      if (resolved.m_sName == sCenter && !resolved.m_Positions.IsEmpty())
      {
        vCenter = resolved.m_Positions[0];
        break;
      }
    }
  }

  if (desc.m_pGenerator->NeedsMainThreadCollect())
  {
    if (const plAiTacticalWorldModule* pTactical = GetWorld()->GetModule<plAiTacticalWorldModule>())
    {
      desc.m_pGenerator->CollectOnMainThread(vCenter, params.m_hQuerier, *pTactical, slot.m_PreCollected);
    }
  }

  ++m_uiStatQueriesSubmitted;

  // side effect: requests missing sectors, so a retry shortly after will find them loaded
  const plVec2 vHalf(plMath::Max(desc.m_pGenerator->m_fRadiusMax, 1.0f));

  if (!pNavMesh->RequestSector(vCenter.GetAsVec2(), vHalf))
  {
    slot.m_Result.m_Status = plAiEqsQueryResult::Status::AreaNotReady;
    slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
  }
  else
  {
    slot.m_Result.m_Status = plAiEqsQueryResult::Status::Pending;
    m_PendingQueue.PushBack(uiSlot);
  }

  return MakeQueryID(uiSlot, slot.m_uiGeneration);
}

const plAiEqsWorldModule::QuerySlot* plAiEqsWorldModule::GetSlotChecked(QueryID id) const
{
  if (id == InvalidQueryID)
    return nullptr;

  const plUInt32 uiSlot = QuerySlotIndex(id);

  if (uiSlot >= m_Queries.GetCount())
    return nullptr;

  const QuerySlot& slot = m_Queries[uiSlot];

  if (!slot.m_bInUse || slot.m_uiGeneration != QueryGeneration(id))
    return nullptr;

  return &slot;
}

bool plAiEqsWorldModule::TryGetResult(QueryID id, plAiEqsQueryResult& out_result) const
{
  const QuerySlot* pSlot = GetSlotChecked(id);

  if (pSlot == nullptr)
  {
    out_result.m_Status = plAiEqsQueryResult::Status::Invalid;
    return false;
  }

  if (pSlot->m_Result.m_Status == plAiEqsQueryResult::Status::Pending)
    return false;

  out_result = pSlot->m_Result;
  return true;
}

void plAiEqsWorldModule::FreeSlot(plUInt32 uiSlot)
{
  QuerySlot& slot = m_Queries[uiSlot];

  if (slot.m_pResource != nullptr)
  {
    plResourceManager::EndAcquireResource(slot.m_pResource);
    slot.m_pResource = nullptr;
  }

  slot.m_bInUse = false;
  ++slot.m_uiGeneration;
  slot.m_pDesc = nullptr;
  slot.m_hResource.Invalidate();
  slot.m_Result.m_TopN.Clear();
  slot.m_Result.m_TopN.Compact();
  slot.m_Items.Clear();
  slot.m_Items.Compact();
  slot.m_PreCollected.Clear();
  slot.m_PreCollected.Compact();
  slot.m_DiscardedDebug.Clear();
  slot.m_DiscardedDebug.Compact();
  slot.m_ResolvedSlots.Clear();

  m_FreeQueries.PushBack(uiSlot);
}

void plAiEqsWorldModule::ReleaseQuery(QueryID id)
{
  const QuerySlot* pSlot = GetSlotChecked(id);

  if (pSlot == nullptr)
    return;

  const plUInt32 uiSlot = QuerySlotIndex(id);
  QuerySlot& slot = m_Queries[uiSlot];

  // pending slots stay alive until executed (the execute list may already reference them);
  // they are reclaimed by the sweep afterwards
  if (slot.m_Result.m_Status == plAiEqsQueryResult::Status::Pending)
  {
    slot.m_CompletedAt = plTime::MakeZero(); // reclaim as soon as it completed
    return;
  }

  FreeSlot(uiSlot);
}

void plAiEqsWorldModule::SweepQuerySlots()
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  for (plUInt32 uiSlot = 0; uiSlot < m_Queries.GetCount(); ++uiSlot)
  {
    QuerySlot& slot = m_Queries[uiSlot];

    if (!slot.m_bInUse || slot.m_Result.m_Status == plAiEqsQueryResult::Status::Pending)
      continue;

    if (now - slot.m_CompletedAt > plTime::MakeFromSeconds(5.0))
    {
      FreeSlot(uiSlot);
    }
  }
}

void plAiEqsWorldModule::UpdateExecute(const UpdateContext& ctxt)
{
  m_uiStatQueriesExecuted = 0;
  m_uiStatQueriesSuspended = 0;
  m_uiStatRaycastsThisFrame = 0;

  SweepQuerySlots();

  if (!m_PendingQueue.IsEmpty())
  {
    m_pPhysicsForQueries = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
    m_pTacticalForQueries = GetWorld()->GetModule<plAiTacticalWorldModule>();

    m_iPathQueryBudget = plMath::Clamp<plInt32>(cvar_EqsMaxPathQueriesPerFrame, 1, 4096);

    const plUInt32 uiMaxQueries = static_cast<plUInt32>(plMath::Clamp<plInt32>(cvar_EqsMaxQueriesPerFrame, 1, 64));
    const plUInt32 uiCount = plMath::Min(uiMaxQueries, m_PendingQueue.GetCount());

    m_ExecuteList.Clear();

    for (plUInt32 i = 0; i < uiCount; ++i)
    {
      m_ExecuteList.PushBack(m_PendingQueue[i]);
    }

    m_PendingQueue.RemoveAtAndCopy(0, uiCount);

    // one scratch per potential parallel invocation (tactical/crowd-module idiom)
    const plUInt32 uiMaxInvocations = plTaskSystem::GetWorkerThreadCount(plWorkerThreadType::ShortTasks) * 2 + 1;

    while (m_Scratch.GetCount() < uiMaxInvocations)
    {
      m_Scratch.ExpandAndGetRef();
    }

    m_uiScratchCounter = 0;

    plParallelForParams params;
    params.m_uiBinSize = 1; // queries are heavyweight units of work
    params.m_uiMaxTasksPerThread = 2;

    plTaskSystem::ParallelForIndexed(0u, m_ExecuteList.GetCount(),
      [this](plUInt32 uiFirst, plUInt32 uiEnd) {
        const plUInt32 uiScratchIndex = static_cast<plUInt32>(m_uiScratchCounter.Increment() - 1);

        for (plUInt32 i = uiFirst; i < uiEnd; ++i)
        {
          ExecuteQueryJob(m_ExecuteList[i], uiScratchIndex);
        }
      },
      "AiEqsQueries", plTaskNesting::Never, params);

    m_uiStatQueriesExecuted = m_ExecuteList.GetCount();

    // suspended queries resume FIRST next frame (their budget ran out, not their usefulness);
    // completed ones feed the visualization
    plUInt32 uiRequeued = 0;

    for (plUInt32 uiSlotIndex : m_ExecuteList)
    {
      QuerySlot& slot = m_Queries[uiSlotIndex];

      if (slot.m_Result.m_Status == plAiEqsQueryResult::Status::Pending)
      {
        m_PendingQueue.InsertAt(uiRequeued, uiSlotIndex);
        ++uiRequeued;
        ++m_uiStatQueriesSuspended;
      }
      else if (cvar_EqsVisualizeQueries > 0 && slot.m_Result.m_Status != plAiEqsQueryResult::Status::AreaNotReady)
      {
        RetainDebugQuery(slot);
      }
    }

    m_ExecuteList.Clear();
    m_pPhysicsForQueries = nullptr;
    m_pTacticalForQueries = nullptr;
  }

  if (cvar_EqsVisualizeQueries > 0)
  {
    DrawQueryVisualization();
  }

  if (cvar_EqsShowStats)
  {
    DrawStats();
  }
}

void plAiEqsWorldModule::RetainDebugQuery(const QuerySlot& slot)
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  while (m_DebugQueries.GetCount() >= 8)
  {
    m_DebugQueries.PopFront();
  }

  DebugQuery& debug = m_DebugQueries.ExpandAndGetRef();
  debug.m_Expiry = now + plTime::MakeFromSeconds(2.0);
  debug.m_vQuerier = slot.m_vQuerier;
  debug.m_TopN = slot.m_Result.m_TopN;
  debug.m_sName = (slot.m_pDesc != nullptr) ? slot.m_pDesc->m_sName.GetData() : "";
  debug.m_sStageSummary = slot.m_StageSummary;
  debug.m_SubmittedAt = slot.m_SubmittedAt;
  debug.m_Contexts = slot.m_ResolvedSlots;
  if (slot.m_pDesc != nullptr)
  {
    for (const auto& test : slot.m_pDesc->m_Tests)
      debug.m_TestNames.PushBack(test.m_pTest ? test.m_pTest->GetDynamicRTTI()->GetTypeName() : "<missing test>");
  }

  debug.m_Discarded.Clear();

  for (plUInt32 i = 0; i < slot.m_DiscardedDebug.GetCount(); ++i)
  {
    debug.m_Discarded.PushBack(slot.m_DiscardedDebug[i]);
  }
}

void plAiEqsWorldModule::DrawQueryVisualization()
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  while (!m_DebugQueries.IsEmpty() && m_DebugQueries[0].m_Expiry < now)
  {
    m_DebugQueries.PopFront();
  }

  for (const DebugQuery& debug : m_DebugQueries)
  {
    if (cvar_EqsVisualizeScores)
    {
      plDebugRenderer::Draw3DText(GetWorld(), plFmt("{}\n{}\nage {} s", debug.m_sName, debug.m_sStageSummary, plArgF((now - debug.m_SubmittedAt).GetSeconds(), 2)),
        debug.m_vQuerier + plVec3(0, 0, 2.5f), plColor::White);
      for (const auto& context : debug.m_Contexts)
      {
        for (const auto& position : context.m_Positions)
          plDebugRenderer::Draw3DText(GetWorld(), context.m_sName.GetData(), position + plVec3(0, 0, 0.8f), plColor::Yellow);
      }
    }
    for (const auto& discarded : debug.m_Discarded)
    {
      const plVec3& vDiscarded = discarded.m_vPosition;
      if (cvar_EqsVisualizeScores)
      {
        const auto index = discarded.m_uiRejectedBy;
        const char* reason = plAiEqsTestDataName(discarded.m_TestData);
        plDebugRenderer::Draw3DText(GetWorld(), plFmt("T{} {}\n{}; value {}, raw {}", index + 1, index < debug.m_TestNames.GetCount() ? debug.m_TestNames[index].GetData() : "", discarded.m_TestData == plAiEqsTestData::Valid ? "filter rejected" : reason, plArgF(discarded.m_bHasMeasurement ? discarded.m_fMeasurement : discarded.m_fRaw, 2), plArgF(discarded.m_fRaw, 2)),
          vDiscarded + plVec3(0, 0, 0.4f), plColor::Gray);
      }
      // gray cross for filtered-out items
      plDebugRenderer::Line cross[2];
      cross[0] = plDebugRenderer::Line(vDiscarded + plVec3(-0.12f, -0.12f, 0.1f), vDiscarded + plVec3(0.12f, 0.12f, 0.1f));
      cross[1] = plDebugRenderer::Line(vDiscarded + plVec3(-0.12f, 0.12f, 0.1f), vDiscarded + plVec3(0.12f, -0.12f, 0.1f));
      cross[0].m_startColor = cross[0].m_endColor = plColor::DimGray;
      cross[1].m_startColor = cross[1].m_endColor = plColor::DimGray;
      plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(cross, 2), plColor::White);
    }

    for (plUInt32 i = 0; i < debug.m_TopN.GetCount(); ++i)
    {
      const auto& cand = debug.m_TopN[i];
      const plColor color = (i == 0) ? plColor::White : plMath::Lerp(plColor::Red, plColor::LawnGreen, plMath::Saturate(cand.m_fScore));

      plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(cand.m_vPosition + plVec3(0, 0, 0.15f), 0.15f), color);
      plDebugRenderer::Draw3DText(GetWorld(), plFmt("{}", plArgF(cand.m_fScore, 2)), cand.m_vPosition + plVec3(0, 0, 0.5f), color);
    }

    if (!debug.m_TopN.IsEmpty())
    {
      const auto& winner = debug.m_TopN[0];

      // winner column + querier line
      plDebugRenderer::Line lines[2];
      lines[0] = plDebugRenderer::Line(winner.m_vPosition, winner.m_vPosition + plVec3(0, 0, 1.2f));
      lines[1] = plDebugRenderer::Line(debug.m_vQuerier + plVec3(0, 0, 0.3f), winner.m_vPosition + plVec3(0, 0, 0.3f));
      lines[0].m_startColor = lines[0].m_endColor = plColor::MediumSpringGreen;
      lines[1].m_startColor = lines[1].m_endColor = plColor::White;
      plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(lines, 2), plColor::White);

      if (cvar_EqsVisualizeScores && winner.m_uiRecordedTests > 0)
      {
        plStringBuilder sBreakdown;

        if (!debug.m_sName.IsEmpty())
        {
          sBreakdown.AppendFormat("{}\n", debug.m_sName);
        }

        for (plUInt32 t = 0; t < winner.m_uiRecordedTests; ++t)
        {
          const auto& trace = winner.m_TestTrace[t];
          sBreakdown.AppendFormat("T{} {}: {}\nvalue {} raw {} curve {} contribution {}\n", t + 1,
            t < debug.m_TestNames.GetCount() ? debug.m_TestNames[t].GetData() : "",
            trace.m_bSkipped ? "skipped" : (trace.m_bPassed ? "pass" : "fail"),
            plArgF(trace.m_fMeasurement, 2), plArgF(trace.m_fRaw, 2), plArgF(trace.m_fCurved, 2), plArgF(trace.m_fContribution, 2));
        }

        sBreakdown.AppendFormat("= {}", plArgF(winner.m_fScore, 2));

        plDebugRenderer::Draw3DText(GetWorld(), sBreakdown, winner.m_vPosition + plVec3(0, 0, 1.5f), plColor::MediumSpringGreen);
      }
    }
  }
}

void plAiEqsWorldModule::DrawStats()
{
  plUInt32 uiActiveSlots = 0;

  for (const QuerySlot& slot : m_Queries)
  {
    if (slot.m_bInUse)
    {
      ++uiActiveSlots;
    }
  }

  plStringBuilder sStats;
  sStats.AppendFormat("EQS: {} executed, {} suspended, {} queued\n", m_uiStatQueriesExecuted, m_uiStatQueriesSuspended, m_PendingQueue.GetCount());
  sStats.AppendFormat("Slots: {} active / {} total\n", uiActiveSlots, m_Queries.GetCount());
  sStats.AppendFormat("Rays: {} this frame, path budget left {}\n", static_cast<plInt32>(m_uiStatRaycastsThisFrame), plMath::Max<plInt32>(0, m_iPathQueryBudget));

  plDebugRenderer::DrawInfoText(GetWorld(), plDebugTextPlacement::TopLeft, "AiEqsStats", sStats);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Eqs_Implementation_EqsWorldModule);
