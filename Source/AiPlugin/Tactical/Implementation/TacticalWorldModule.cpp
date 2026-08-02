#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Tactical/Components/CoverPointComponent.h>
#include <AiPlugin/Tactical/Implementation/CoverGeneration.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/Utils/Blackboard.h>
#include <Core/World/World.h>
#include <DetourNavMesh.h>
#include <Foundation/Configuration/CVar.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>

// NOTE: what cover IS (which navmesh, spacing, probe heights...) lives in the AI project settings
// (plAiTacticalConfig, edited via the AI project settings dialog). The cvars below are runtime
// debugging and budget tuning only.
plCVarBool cvar_TacticalVisualizeCover("AI.Tactical.VisualizeCover", false, plCVarFlags::Default, "Visualize baked cover points (yellow = low, green = high, red = claimed).");
plCVarBool cvar_TacticalShowStats("AI.Tactical.ShowStats", false, plCVarFlags::Default, "Show tactical layer statistics on screen.");
plCVarInt cvar_TacticalMaxCoverBakesPerFrame("AI.Tactical.MaxCoverBakesPerFrame", 2, plCVarFlags::Default, "How many sector cover bakes may be started per frame.");
plCVarBool cvar_TacticalVisualizeQueries("AI.Tactical.VisualizeQueries", false, plCVarFlags::Default, "Visualize recent tactical query results (score-colored candidates, best in white).");
plCVarInt cvar_TacticalMaxQueriesPerFrame("AI.Tactical.MaxQueriesPerFrame", 8, plCVarFlags::Default, "How many tactical queries may execute per frame; excess stays queued.");
plCVarInt cvar_TacticalMaxRaycastsPerQuery("AI.Tactical.MaxRaycastsPerQuery", 24, plCVarFlags::Default, "Line-of-sight raycast budget per tactical query.");
plCVarInt cvar_TacticalMaxAgentProbesPerFrame("AI.Tactical.MaxAgentProbesPerFrame", 16, plCVarFlags::Default, "How many agents may have their tactical situation (exposure, cover status) probed per frame.");
plCVarFloat cvar_TacticalCoverSearchRadius("AI.Tactical.CoverSearchRadius", 15.0f, plCVarFlags::Default, "Search radius for the 'cover nearby' agent probe, in meters.");
plCVarFloat cvar_TacticalAgentProbeInterval("AI.Tactical.AgentProbeInterval", 0.25f, plCVarFlags::Default, "Minimum time between tactical probes of the same agent, in seconds.");
plCVarFloat cvar_TacticalCoverOccupiedRadius("AI.Tactical.CoverOccupiedRadius", 2.0f, plCVarFlags::Default, "How close (XY, meters) an agent must stand to its claimed cover point to count as IN cover ('Ai_CoverStatus' > 0; also keeps the claim from expiring).");
plCVarFloat cvar_TacticalCoverOccupiedZRange("AI.Tactical.CoverOccupiedZRange", 2.0f, plCVarFlags::Default, "Max height difference (meters) between an agent and its claimed cover point to still count as IN cover (multi-floor: standing below the point is NOT in cover); <= 0 disables the check.");

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiCoverQuality, 1)
  PL_ENUM_CONSTANTS(plAiCoverQuality::None, plAiCoverQuality::Low, plAiCoverQuality::High)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiTacticalQueryPreset, 1)
  PL_ENUM_CONSTANTS(plAiTacticalQueryPreset::TakeCoverFromTarget, plAiTacticalQueryPreset::FlankTarget, plAiTacticalQueryPreset::RetreatFromTarget, plAiTacticalQueryPreset::RandomNearSelf, plAiTacticalQueryPreset::HideFromTarget)
PL_END_STATIC_REFLECTED_ENUM;

PL_IMPLEMENT_WORLD_MODULE(plAiTacticalWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiTacticalWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiTacticalWorldModule::plAiTacticalWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
}

plAiTacticalWorldModule::~plAiTacticalWorldModule() = default;

void plAiTacticalWorldModule::Initialize()
{
  SUPER::Initialize();

  // Ordering note: both update functions use a low PRIORITY (= runs late in the phase) instead of
  // m_DependsOn. A missing m_DependsOn target hard-asserts in plWorld, and neither the navmesh nor
  // the brain module is guaranteed to exist in every world that creates this module.
  // UpdateMaintain must run after plAiNavMeshWorldModule::Update (priority 0) - the changed-sector
  // accessor asserts if that ever breaks. UpdateExecute must run after the brain's UpdateApply
  // (priority 0), so all SM-state submissions and claims of the frame are in.

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiTacticalWorldModule::UpdateExecute, this);
    updateDesc.m_Phase = plWorldModule::UpdateFunctionDesc::Phase::PostAsync;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    updateDesc.m_fPriority = -1000.0f;

    RegisterUpdateFunction(updateDesc);
  }

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiTacticalWorldModule::UpdateMaintain, this);
    updateDesc.m_Phase = plWorldModule::UpdateFunctionDesc::Phase::PostTransform;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    updateDesc.m_fPriority = -1000.0f;

    RegisterUpdateFunction(updateDesc);
  }
}

void plAiTacticalWorldModule::Deinitialize()
{
  for (auto& slot : m_BakeSlots)
  {
    if (slot.m_bInFlight)
    {
      plTaskSystem::CancelGroup(slot.m_TaskID).IgnoreResult();
      plTaskSystem::WaitForGroup(slot.m_TaskID);
      slot.m_bInFlight = false;
    }
  }

  SUPER::Deinitialize();
}

plAiTacticalWorldModule::QueryID plAiTacticalWorldModule::SubmitQuery(const plAiTacticalQueryDesc& desc)
{
  auto* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>();
  if (pNavMeshModule == nullptr)
    return InvalidQueryID;

  plAiNavMesh* pNavMesh = nullptr;

  if (!desc.m_sNavmeshConfig.IsEmpty())
  {
    pNavMesh = pNavMeshModule->GetNavMesh(desc.m_sNavmeshConfig.GetView());
  }
  else
  {
    pNavMesh = ResolveCoverNavMesh(*pNavMeshModule);
  }

  if (pNavMesh == nullptr)
    return InvalidQueryID;

  plUInt32 uiSlot = plInvalidIndex;

  if (!m_FreeQueries.IsEmpty())
  {
    uiSlot = m_FreeQueries.PeekBack();
    m_FreeQueries.PopBack();
  }
  else
  {
    if (m_Queries.GetCount() >= 512)
      return InvalidQueryID; // runaway leak guard; slots auto-reclaim after a few seconds

    uiSlot = m_Queries.GetCount();
    m_Queries.ExpandAndGetRef();
  }

  QuerySlot& slot = m_Queries[uiSlot];
  slot.m_Desc = desc;
  slot.m_Desc.m_uiCandidates = plMath::Min<plUInt8>(desc.m_uiCandidates, 32);
  slot.m_Desc.m_uiMaxResults = plMath::Min<plUInt8>(desc.m_uiMaxResults, 16);
  slot.m_pNavMesh = pNavMesh;
  slot.m_pFilter = &pNavMeshModule->GetPathSearchFilter(desc.m_sPathSearchConfig.GetView());
  slot.m_Result.m_TopN.Clear();
  slot.m_Result.m_uiFrameStamp = GetWorld()->GetUpdateCounter();
  slot.m_bInUse = true;

  // side effect: requests missing sectors, so a retry shortly after will find them loaded
  const plVec2 vHalf(plMath::Max(desc.m_fRadiusMax, 1.0f));

  if (!pNavMesh->RequestSector(desc.m_vCenter.GetAsVec2(), vHalf))
  {
    slot.m_Result.m_Status = plAiTacticalQueryResult::Status::AreaNotReady;
    slot.m_CompletedAt = GetWorld()->GetClock().GetAccumulatedTime();
  }
  else
  {
    slot.m_Result.m_Status = plAiTacticalQueryResult::Status::Pending;
    m_PendingQueue.PushBack(uiSlot);
  }

  return MakeQueryID(uiSlot, slot.m_uiGeneration);
}

const plAiTacticalWorldModule::QuerySlot* plAiTacticalWorldModule::GetSlotChecked(QueryID id) const
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

bool plAiTacticalWorldModule::TryGetResult(QueryID id, plAiTacticalQueryResult& out_result) const
{
  const QuerySlot* pSlot = GetSlotChecked(id);

  if (pSlot == nullptr)
  {
    out_result.m_Status = plAiTacticalQueryResult::Status::Invalid;
    return false;
  }

  if (pSlot->m_Result.m_Status == plAiTacticalQueryResult::Status::Pending)
    return false;

  out_result = pSlot->m_Result;
  return true;
}

void plAiTacticalWorldModule::ReleaseQuery(QueryID id)
{
  const QuerySlot* pSlot = GetSlotChecked(id);

  if (pSlot == nullptr)
    return;

  const plUInt32 uiSlot = QuerySlotIndex(id);
  QuerySlot& slot = m_Queries[uiSlot];

  // pending slots stay alive until executed (the execute list may already reference them);
  // they are reclaimed by the sweep afterwards
  if (slot.m_Result.m_Status == plAiTacticalQueryResult::Status::Pending)
  {
    slot.m_CompletedAt = plTime::MakeZero(); // reclaim as soon as it completed
    return;
  }

  slot.m_bInUse = false;
  ++slot.m_uiGeneration;
  slot.m_Result.m_TopN.Clear();
  slot.m_Result.m_TopN.Compact();
  m_FreeQueries.PushBack(uiSlot);
}

void plAiTacticalWorldModule::SweepQuerySlots()
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  for (plUInt32 uiSlot = 0; uiSlot < m_Queries.GetCount(); ++uiSlot)
  {
    QuerySlot& slot = m_Queries[uiSlot];

    if (!slot.m_bInUse || slot.m_Result.m_Status == plAiTacticalQueryResult::Status::Pending)
      continue;

    if (now - slot.m_CompletedAt > plTime::MakeFromSeconds(5.0))
    {
      slot.m_bInUse = false;
      ++slot.m_uiGeneration;
      slot.m_Result.m_TopN.Clear();
      slot.m_Result.m_TopN.Compact();
      m_FreeQueries.PushBack(uiSlot);
    }
  }
}

void plAiTacticalWorldModule::UpdateExecute(const UpdateContext& ctxt)
{
  m_uiStatQueriesExecuted = 0;
  m_uiStatRaycastsThisFrame = 0;

  const plPhysicsWorldModuleInterface* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();

  RunAgentProbes(pPhysics);

  if (m_PendingQueue.IsEmpty())
    return;

  m_pPhysicsForQueries = pPhysics;

  const plUInt32 uiMaxQueries = static_cast<plUInt32>(plMath::Clamp<plInt32>(cvar_TacticalMaxQueriesPerFrame, 1, 64));
  const plUInt32 uiCount = plMath::Min(uiMaxQueries, m_PendingQueue.GetCount());

  m_ExecuteList.Clear();

  for (plUInt32 i = 0; i < uiCount; ++i)
  {
    m_ExecuteList.PushBack(m_PendingQueue[i]);
  }

  m_PendingQueue.RemoveAtAndCopy(0, uiCount);

  // one scratch per potential parallel invocation (crowd-module idiom)
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
    "AiTacticalQueries", plTaskNesting::Never, params);

  m_uiStatQueriesExecuted = m_ExecuteList.GetCount();

  // retain completed queries for the visualization (main thread again)
  if (cvar_TacticalVisualizeQueries)
  {
    const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

    for (plUInt32 uiSlot : m_ExecuteList)
    {
      const QuerySlot& slot = m_Queries[uiSlot];

      if (slot.m_Result.m_Status != plAiTacticalQueryResult::Status::Ready)
        continue;

      while (m_DebugQueries.GetCount() >= 8)
      {
        m_DebugQueries.PopFront();
      }

      DebugQuery& debug = m_DebugQueries.ExpandAndGetRef();
      debug.m_Expiry = now + plTime::MakeFromSeconds(2.0);
      debug.m_vQuerier = slot.m_Desc.m_vQuerier;
      debug.m_TopN = slot.m_Result.m_TopN;
    }
  }

  m_ExecuteList.Clear();
}

plAiNavMesh* plAiTacticalWorldModule::ResolveCoverNavMesh(plAiNavMeshWorldModule& navMeshModule) const
{
  plStringView sName = navMeshModule.GetConfig().m_TacticalConfig.m_sCoverNavmesh;

  if (sName.IsEmpty())
  {
    if (navMeshModule.GetConfig().m_NavmeshConfigs.IsEmpty())
      return nullptr;

    sName = navMeshModule.GetConfig().m_NavmeshConfigs[0].m_sName;
  }

  return navMeshModule.GetNavMesh(sName);
}

void plAiTacticalWorldModule::UpdateMaintain(const UpdateContext& ctxt)
{
  auto* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>();
  if (pNavMeshModule == nullptr)
    return;

  plAiNavMesh* pNavMesh = ResolveCoverNavMesh(*pNavMeshModule);

  if (pNavMesh != m_pCoverNavMesh)
  {
    // navmesh switched (or first run): drop everything, in-flight bakes get discarded on collect
    m_CoverSectors.Clear();
    m_FreeCoverSectors.Clear();
    m_SectorLookup.Clear();
    m_Claims.Clear();
    m_FreeClaims.Clear();
    m_ClaimByOwner.Clear();
    m_BakeQueue.Clear();

    for (auto& slot : m_BakeSlots)
    {
      slot.m_uiEntryIndex = plInvalidIndex;
    }

    // authored cover entries were wiped with the registry - they re-apply in MaintainAuthoredCover
    for (auto& authored : m_AuthoredCovers)
    {
      authored.m_uiSectorEntry = plInvalidIndex;
    }

    m_pCoverNavMesh = pNavMesh;
    m_bInitialBakeQueued = false;
  }

  if (m_pCoverNavMesh == nullptr)
    return;

  CollectFinishedBakes();

  if (!m_bInitialBakeQueued)
  {
    // the module may be created after sectors already finished building - catch up once
    QueueInitialBakes(*m_pCoverNavMesh);
    m_bInitialBakeQueued = true;
  }

  for (plAiNavMesh::SectorID sectorID : pNavMeshModule->GetChangedSectorsThisFrame(m_pCoverNavMesh))
  {
    InvalidateAndQueueSector(sectorID);
  }

  auto* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();
  DispatchBakes(*m_pCoverNavMesh, pPhysics);

  MaintainAuthoredCover(pPhysics);

  SweepClaims();
  SweepSmartObjectClaims();
  SweepQuerySlots();

  DrawSmartObjectDebug(); // gates on its own cvar

  if (cvar_TacticalVisualizeCover)
  {
    DrawCoverVisualization();
  }

  if (cvar_TacticalVisualizeQueries)
  {
    DrawQueryVisualization();
  }

  if (cvar_TacticalShowStats)
  {
    DrawStats();
  }
}

bool plAiTacticalWorldModule::FindPeekPosition(plGameObjectHandle hClaimant, const plVec3& vThreatPos, float fMaxSideStep, plVec3& out_vPosition)
{
  plAiCoverPointHandle hClaimed;
  plAiCoverPoint point;

  if (!GetClaimedCover(hClaimant, hClaimed) || !ResolveCover(hClaimed, point))
    return false;

  const auto* pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();

  if (pPhysics == nullptr || m_pCoverNavMesh == nullptr)
    return false;

  if (m_pMainThreadQueryInitFor != m_pCoverNavMesh)
  {
    if (dtStatusFailed(m_MainThreadNavQuery.init(m_pCoverNavMesh->GetDetourNavMesh(), 64)))
      return false;

    m_pMainThreadQueryInitFor = m_pCoverNavMesh;
  }

  auto* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>();

  if (pNavMeshModule == nullptr)
    return false;

  const dtQueryFilter& filter = pNavMeshModule->GetPathSearchFilter({});
  const plPhysicsQueryParameters params(m_pCoverNavMesh->GetConfig().m_uiCollisionLayer, plPhysicsShapeType::Static);

  const plVec3 vThreatEye = vThreatPos + plVec3(0, 0, 1.55f);
  const plVec3 vTangent = plVec3(-point.m_vWallDir.y, point.m_vWallDir.x, 0);

  auto hasStandingLos = [&](const plVec3& vFrom) -> bool {
    const plVec3 vEye = vFrom + plVec3(0, 0, 1.55f);
    plVec3 vDir = vThreatEye - vEye;
    const float fDist = vDir.GetLength();

    if (fDist < 0.01f)
      return true;

    vDir /= fDist;

    plPhysicsCastResult hit;
    m_uiStatRaycastsThisFrame.Increment();
    return !pPhysics->Raycast(hit, vEye, vDir, fDist, params); // nothing hit = clear line of sight
  };

  // low cover: standing at the point already looks over the wall
  if (hasStandingLos(point.m_vPosition))
  {
    out_vPosition = point.m_vPosition;
    return true;
  }

  // cover surface: walk the wall strip point-by-point, alternating sides, nearest first.
  // The strip follows the wall contour (including corners) and its points are baked stand
  // positions, so no navmesh projection is needed.
  {
    const CoverSector& entry = m_CoverSectors[hClaimed.m_uiSectorEntry];
    const plUInt16 uiSurface = point.m_uiSurface;

    if (uiSurface != 0xFFFF && uiSurface < entry.m_Surfaces.GetCount())
    {
      const plAiCoverSurface& surface = entry.m_Surfaces[uiSurface];
      const plInt32 iFirst = surface.m_uiFirstPoint;
      const plInt32 iLast = iFirst + surface.m_uiNumPoints - 1;

      plInt32 iPlus = hClaimed.m_uiPoint;
      plInt32 iMinus = hClaimed.m_uiPoint;
      float fPlusDist = 0.0f;
      float fMinusDist = 0.0f;

      while (true)
      {
        bool bAdvanced = false;

        if (iPlus < iLast && fPlusDist < fMaxSideStep)
        {
          fPlusDist += (entry.m_Points[iPlus + 1].m_vPosition - entry.m_Points[iPlus].m_vPosition).GetLength();
          ++iPlus;
          bAdvanced = true;

          if (fPlusDist <= fMaxSideStep + 0.01f && hasStandingLos(entry.m_Points[iPlus].m_vPosition))
          {
            out_vPosition = entry.m_Points[iPlus].m_vPosition;
            return true;
          }
        }

        if (iMinus > iFirst && fMinusDist < fMaxSideStep)
        {
          fMinusDist += (entry.m_Points[iMinus - 1].m_vPosition - entry.m_Points[iMinus].m_vPosition).GetLength();
          --iMinus;
          bAdvanced = true;

          if (fMinusDist <= fMaxSideStep + 0.01f && hasStandingLos(entry.m_Points[iMinus].m_vPosition))
          {
            out_vPosition = entry.m_Points[iMinus].m_vPosition;
            return true;
          }
        }

        if (!bAdvanced)
          break;
      }

      // fall through: the tangent side-step below can still find a spot just past the surface
      // ends (peeking around the corner the strip stops at)
    }
  }

  // high cover: side-step along the wall, alternating sides, until line of sight opens
  const float fStep = 0.7f;
  const float fExtents[3] = {1.5f, 3.0f, 1.5f};

  for (float fOffset = fStep; fOffset <= fMaxSideStep + 0.01f; fOffset += fStep)
  {
    for (float fSide = 1.0f; fSide >= -1.0f; fSide -= 2.0f)
    {
      const plVec3 vCandidate = point.m_vPosition + vTangent * (fOffset * fSide);

      // must stay on the navmesh (steps past a wall corner often leave it)
      dtPolyRef polyRef = 0;
      plRcPos projected;

      if (dtStatusFailed(m_MainThreadNavQuery.findNearestPoly(plRcPos(vCandidate), fExtents, &filter, &polyRef, projected)) || polyRef == 0)
        continue;

      const plVec3 vProjected = projected;

      if ((vProjected.GetAsVec2() - vCandidate.GetAsVec2()).GetLengthSquared() > plMath::Square(0.5f))
        continue; // projection snapped somewhere else entirely

      if (hasStandingLos(vProjected))
      {
        out_vPosition = vProjected;
        return true;
      }
    }
  }

  return false;
}

plAiTacticalWorldModule::AgentID plAiTacticalWorldModule::RegisterAgent(plComponentHandle hAgentComponent)
{
  AgentID id = InvalidAgentID;

  if (!m_FreeAgentSlots.IsEmpty())
  {
    id = m_FreeAgentSlots.PeekBack();
    m_FreeAgentSlots.PopBack();
  }
  else
  {
    id = m_AgentSlots.GetCount();
    m_AgentSlots.ExpandAndGetRef();
  }

  AgentSlot& slot = m_AgentSlots[id];
  slot.m_hComponent = hAgentComponent;
  slot.m_NextProbe = plTime::MakeZero();
  slot.m_bInUse = true;

  return id;
}

void plAiTacticalWorldModule::UnregisterAgent(AgentID id)
{
  if (id >= m_AgentSlots.GetCount() || !m_AgentSlots[id].m_bInUse)
    return;

  m_AgentSlots[id].m_bInUse = false;
  m_AgentSlots[id].m_hComponent.Invalidate();
  m_FreeAgentSlots.PushBack(id);
}

void plAiTacticalWorldModule::RunAgentProbes(const plPhysicsWorldModuleInterface* pPhysics)
{
  if (m_AgentSlots.IsEmpty())
    return;

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();
  const plUInt32 uiMaxProbes = static_cast<plUInt32>(plMath::Clamp<plInt32>(cvar_TacticalMaxAgentProbesPerFrame, 1, 256));
  const plTime interval = plTime::Seconds(plMath::Max(0.05f, cvar_TacticalAgentProbeInterval.GetValue()));

  plUInt32 uiProbed = 0;

  for (plUInt32 i = 0; i < m_AgentSlots.GetCount() && uiProbed < uiMaxProbes; ++i)
  {
    m_uiProbeCursor = (m_uiProbeCursor + 1) % m_AgentSlots.GetCount();
    AgentSlot& slot = m_AgentSlots[m_uiProbeCursor];

    if (!slot.m_bInUse || now < slot.m_NextProbe)
      continue;

    plComponent* pComponent = nullptr;

    if (!GetWorld()->TryGetComponent(slot.m_hComponent, pComponent) || !pComponent->IsActiveAndSimulating())
      continue;

    slot.m_NextProbe = now + interval;
    ++uiProbed;

    ProbeAgent(pComponent->GetOwner(), pPhysics);
  }
}

void plAiTacticalWorldModule::ProbeAgent(plGameObject* pOwner, const plPhysicsWorldModuleInterface* pPhysics)
{
  const plSharedPtr<plBlackboard>& pBlackboard = plBlackboardComponent::FindBlackboard(pOwner);

  if (pBlackboard == nullptr)
    return; // private brain blackboards are unreachable; the tactical inputs need a blackboard component

  static const plHashedString sTargetPosition = plMakeHashedString("Ai_TargetPosition");
  static const plHashedString sTargetConfidence = plMakeHashedString("Ai_TargetConfidence");
  static const plHashedString sTargetExposure = plMakeHashedString("Ai_TargetExposure");
  static const plHashedString sCoverStatus = plMakeHashedString("Ai_CoverStatus");
  static const plHashedString sCoverNearby = plMakeHashedString("Ai_CoverNearby");

  const plVec3 vOwnerPos = pOwner->GetGlobalPosition();

  // quality of the currently claimed cover (0 / 0.5 / 1) - but ONLY while actually standing
  // at it. A claim is made at PICK time, long before the agent arrives; reporting it early
  // would let 'fight from cover' behaviors interrupt the run to cover in the open.
  float fCoverStatus = 0.0f;
  {
    plAiCoverPointHandle hClaimed;
    plAiCoverPoint point;

    if (GetClaimedCover(pOwner->GetHandle(), hClaimed) && ResolveCover(hClaimed, point))
    {
      const float fOccupiedRadius = plMath::Max(0.5f, cvar_TacticalCoverOccupiedRadius.GetValue());
      const float fZRange = cvar_TacticalCoverOccupiedZRange.GetValue();

      if ((vOwnerPos.GetAsVec2() - point.m_vPosition.GetAsVec2()).GetLengthSquared() < plMath::Square(fOccupiedRadius) &&
          (fZRange <= 0.0f || plMath::Abs(vOwnerPos.z - point.m_vPosition.z) < fZRange))
      {
        fCoverStatus = (point.m_Quality == plAiCoverQuality::High) ? 1.0f : 0.5f;
      }
    }
  }

  // is unclaimed cover available nearby?
  float fCoverNearby = 0.0f;
  {
    plAiCoverPointHandle hNearest;
    plAiCoverPoint point;

    if (FindNearestCover(vOwnerPos, plMath::Max(1.0f, cvar_TacticalCoverSearchRadius.GetValue()), true, hNearest, point))
    {
      fCoverNearby = 1.0f;
    }
  }

  // does the perceived target have line of sight to this agent?
  float fExposure = 0.0f;
  {
    const plVariant pos = pBlackboard->GetEntryValue(sTargetPosition);
    const plVariant conf = pBlackboard->GetEntryValue(sTargetConfidence);

    if (pPhysics != nullptr && pos.IsA<plVec3>() && conf.IsValid() && conf.CanConvertTo<float>() && conf.ConvertTo<float>() > 0.01f)
    {
      const plVec3 vThreatEye = pos.Get<plVec3>() + plVec3(0, 0, 1.55f);
      const plVec3 vOwnerEye = vOwnerPos + plVec3(0, 0, 1.55f);

      plVec3 vDir = vOwnerEye - vThreatEye;
      const float fDist = vDir.GetLength();

      if (fDist > 0.01f)
      {
        vDir /= fDist;

        plPhysicsCastResult hit;
        const plUInt8 uiLayer = (m_pCoverNavMesh != nullptr) ? m_pCoverNavMesh->GetConfig().m_uiCollisionLayer : 0;
        const plPhysicsQueryParameters params(uiLayer, plPhysicsShapeType::Static);

        fExposure = pPhysics->Raycast(hit, vThreatEye, vDir, fDist, params) ? 0.0f : 1.0f;
        m_uiStatRaycastsThisFrame.Increment();
      }
    }
  }

  pBlackboard->SetEntryValue(sTargetExposure, fExposure);
  pBlackboard->SetEntryValue(sCoverStatus, fCoverStatus);
  pBlackboard->SetEntryValue(sCoverNearby, fCoverNearby);
}

void plAiTacticalWorldModule::DrawQueryVisualization()
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  while (!m_DebugQueries.IsEmpty() && m_DebugQueries[0].m_Expiry < now)
  {
    m_DebugQueries.PopFront();
  }

  for (const DebugQuery& debug : m_DebugQueries)
  {
    for (plUInt32 i = 0; i < debug.m_TopN.GetCount(); ++i)
    {
      const auto& cand = debug.m_TopN[i];
      const plColor color = (i == 0) ? plColor::White : plMath::Lerp(plColor::Red, plColor::LawnGreen, plMath::Saturate(cand.m_fScore));

      plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(cand.m_vPosition + plVec3(0, 0, 0.15f), 0.15f), color);
      plDebugRenderer::Draw3DText(GetWorld(), plFmt("{}", plArgF(cand.m_fScore, 2)), cand.m_vPosition + plVec3(0, 0, 0.5f), color);
    }

    if (!debug.m_TopN.IsEmpty())
    {
      plDebugRenderer::Line line(debug.m_vQuerier + plVec3(0, 0, 0.3f), debug.m_TopN[0].m_vPosition + plVec3(0, 0, 0.3f));
      line.m_startColor = line.m_endColor = plColor::White;
      plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(&line, 1), plColor::White);
    }
  }
}

void plAiTacticalWorldModule::QueueInitialBakes(const plAiNavMesh& navMesh)
{
  const plUInt32 uiNumSectors = navMesh.GetConfig().m_uiNumSectorsX * navMesh.GetConfig().m_uiNumSectorsY;

  for (plUInt32 sectorID = 0; sectorID < uiNumSectors; ++sectorID)
  {
    const plAiNavMeshSector* pSector = navMesh.GetSector(sectorID);

    if (pSector != nullptr && pSector->m_FlagUsable == 1)
    {
      InvalidateAndQueueSector(sectorID);
    }
  }
}

void plAiTacticalWorldModule::InvalidateAndQueueSector(plAiNavMesh::SectorID sectorID)
{
  plUInt32 uiEntry = plInvalidIndex;

  if (!m_SectorLookup.TryGetValue(sectorID, uiEntry))
  {
    if (!m_FreeCoverSectors.IsEmpty())
    {
      uiEntry = m_FreeCoverSectors.PeekBack();
      m_FreeCoverSectors.PopBack();
    }
    else
    {
      uiEntry = m_CoverSectors.GetCount();
      m_CoverSectors.ExpandAndGetRef();
    }

    CoverSector& newEntry = m_CoverSectors[uiEntry];
    newEntry.m_SectorID = sectorID;
    newEntry.m_uiBakeGeneration = 0;
    newEntry.m_bInUse = true;
    newEntry.m_bBakePending = false;
    newEntry.m_Points.Clear();

    m_SectorLookup[sectorID] = uiEntry;
  }

  CoverSector& entry = m_CoverSectors[uiEntry];

  // release all claims into this sector before dropping its points
  for (const plAiCoverPoint& point : entry.m_Points)
  {
    if (point.m_uiClaimIndex != 0xFFFF)
    {
      ReleaseClaimAtIndex(point.m_uiClaimIndex);
    }
  }

  entry.m_Points.Clear();
  entry.m_Surfaces.Clear();
  ++entry.m_uiBakeGeneration; // stale handles and in-flight bakes now mismatch

  const plAiNavMeshSector* pSector = m_pCoverNavMesh->GetSector(sectorID);
  const bool bUsable = pSector != nullptr && pSector->m_FlagUsable == 1;

  if (bUsable && !entry.m_bBakePending)
  {
    entry.m_bBakePending = true;
    m_BakeQueue.PushBack(sectorID);
  }
}

void plAiTacticalWorldModule::DispatchBakes(const plAiNavMesh& navMesh, const plPhysicsWorldModuleInterface* pPhysics)
{
  if (pPhysics == nullptr)
  {
    // without physics there is nothing to probe against - drop the queue
    for (plAiNavMesh::SectorID sectorID : m_BakeQueue)
    {
      plUInt32 uiEntry = plInvalidIndex;
      if (m_SectorLookup.TryGetValue(sectorID, uiEntry))
      {
        m_CoverSectors[uiEntry].m_bBakePending = false;
      }
    }

    m_BakeQueue.Clear();
    return;
  }

  const plUInt32 uiMaxDispatch = static_cast<plUInt32>(plMath::Clamp<plInt32>(cvar_TacticalMaxCoverBakesPerFrame, 1, 16));
  plUInt32 uiDispatched = 0;

  while (uiDispatched < uiMaxDispatch && !m_BakeQueue.IsEmpty())
  {
    const plAiNavMesh::SectorID sectorID = m_BakeQueue[0];
    m_BakeQueue.RemoveAtAndCopy(0);

    plUInt32 uiEntry = plInvalidIndex;
    if (!m_SectorLookup.TryGetValue(sectorID, uiEntry))
      continue;

    CoverSector& entry = m_CoverSectors[uiEntry];

    const plAiNavMeshSector* pSector = navMesh.GetSector(sectorID);

    if (pSector == nullptr || pSector->m_FlagUsable == 0)
    {
      entry.m_bBakePending = false;
      continue;
    }

    const dtMeshTile* pTile = navMesh.GetDetourNavMesh()->getTileByRef(pSector->m_TileRef);

    if (pTile == nullptr)
    {
      // empty sector (no walkable geometry) - nothing to bake
      entry.m_bBakePending = false;
      continue;
    }

    // find or create a free bake slot
    BakeSlot* pSlot = nullptr;

    for (auto& slot : m_BakeSlots)
    {
      if (!slot.m_bInFlight)
      {
        pSlot = &slot;
        break;
      }
    }

    if (pSlot == nullptr)
    {
      if (m_BakeSlots.GetCount() >= 8)
      {
        // all slots busy - retry next frame
        m_BakeQueue.PushBack(sectorID);
        return;
      }

      pSlot = &m_BakeSlots.ExpandAndGetRef();
      pSlot->m_pTask = PL_DEFAULT_NEW(plAiCoverBakeTask);
      pSlot->m_pTask->ConfigureTask("Bake AI Cover", plTaskNesting::Maybe);
    }

    plAiCoverBakeTask& task = *pSlot->m_pTask;
    task.m_SectorID = sectorID;
    task.m_pPhysics = pPhysics;
    task.m_Edges.Clear();

    // main thread + PostTransform-after-navmesh-update: safe to read the live tile
    plAiCoverGen::ExtractBoundaryEdges(*pTile, task.m_Edges);

    if (task.m_Edges.IsEmpty())
    {
      entry.m_bBakePending = false;
      continue;
    }

    const plAiTacticalConfig& tacticalConfig = GetWorld()->GetModule<plAiNavMeshWorldModule>()->GetConfig().m_TacticalConfig;

    task.m_fSpacing = plMath::Max(0.25f, tacticalConfig.m_fCoverSpacing);
    task.m_fInset = navMesh.GetConfig().m_fAgentRadius + 0.15f;
    task.m_fProbeDistance = plMath::Max(0.1f, tacticalConfig.m_fCoverProbeDistance);
    task.m_fCrouchHeight = plMath::Max(0.1f, tacticalConfig.m_fCoverCrouchHeight);
    task.m_fStandHeight = plMath::Max(task.m_fCrouchHeight + 0.1f, tacticalConfig.m_fCoverStandHeight);
    task.m_uiCollisionLayer = navMesh.GetConfig().m_uiCollisionLayer; // probe the same world the navmesh was built from
    task.m_uiMaxPoints = plMath::Clamp<plUInt32>(tacticalConfig.m_uiMaxCoverPointsPerSector, 1, 1024);

    pSlot->m_uiEntryIndex = uiEntry;
    pSlot->m_uiGenerationAtDispatch = entry.m_uiBakeGeneration;
    pSlot->m_TaskID = plTaskSystem::StartSingleTask(pSlot->m_pTask, plTaskPriority::LongRunning);
    pSlot->m_bInFlight = true;

    ++uiDispatched;
  }
}

void plAiTacticalWorldModule::CollectFinishedBakes()
{
  for (auto& slot : m_BakeSlots)
  {
    if (!slot.m_bInFlight || !plTaskSystem::IsTaskGroupFinished(slot.m_TaskID))
      continue;

    slot.m_bInFlight = false;

    if (slot.m_uiEntryIndex == plInvalidIndex || slot.m_uiEntryIndex >= m_CoverSectors.GetCount())
      continue; // registry was reset while the bake ran

    CoverSector& entry = m_CoverSectors[slot.m_uiEntryIndex];

    if (!entry.m_bInUse || entry.m_SectorID != slot.m_pTask->m_SectorID)
      continue;

    if (entry.m_uiBakeGeneration != slot.m_uiGenerationAtDispatch)
    {
      // the sector changed again while this bake ran - discard and bake once more
      if (!m_BakeQueue.Contains(entry.m_SectorID))
      {
        m_BakeQueue.PushBack(entry.m_SectorID);
      }

      continue;
    }

    entry.m_Points.Swap(slot.m_pTask->m_Points);
    entry.m_Surfaces.Swap(slot.m_pTask->m_Surfaces);
    entry.m_bBakePending = false;
  }
}

plAiTacticalWorldModule::AuthoredCoverID plAiTacticalWorldModule::RegisterAuthoredCover(plComponentHandle hComponent)
{
  AuthoredCoverID id = InvalidAuthoredCoverID;

  if (!m_FreeAuthoredCovers.IsEmpty())
  {
    id = m_FreeAuthoredCovers.PeekBack();
    m_FreeAuthoredCovers.PopBack();
  }
  else
  {
    id = m_AuthoredCovers.GetCount();
    m_AuthoredCovers.ExpandAndGetRef();
  }

  AuthoredCover& authored = m_AuthoredCovers[id];
  authored.m_hComponent = hComponent;
  authored.m_uiSectorEntry = plInvalidIndex;
  authored.m_bInUse = true;
  authored.m_bPendingRemoval = false;

  return id;
}

void plAiTacticalWorldModule::UnregisterAuthoredCover(AuthoredCoverID id)
{
  if (id >= m_AuthoredCovers.GetCount() || !m_AuthoredCovers[id].m_bInUse)
    return;

  // the entry (and any claim on it) is retired in the next UpdateMaintain - the registry only
  // mutates there, worker jobs may still be reading it right now
  m_AuthoredCovers[id].m_bPendingRemoval = true;
}

void plAiTacticalWorldModule::RetireAuthoredEntry(AuthoredCover& ref_authored)
{
  if (ref_authored.m_uiSectorEntry == plInvalidIndex || ref_authored.m_uiSectorEntry >= m_CoverSectors.GetCount())
  {
    ref_authored.m_uiSectorEntry = plInvalidIndex;
    return;
  }

  CoverSector& entry = m_CoverSectors[ref_authored.m_uiSectorEntry];

  if (entry.m_bInUse && entry.m_SectorID == AuthoredCoverSectorID)
  {
    for (const plAiCoverPoint& point : entry.m_Points)
    {
      if (point.m_uiClaimIndex != 0xFFFF)
      {
        ReleaseClaimAtIndex(point.m_uiClaimIndex);
      }
    }

    entry.m_Points.Clear();
    entry.m_Surfaces.Clear();
    ++entry.m_uiBakeGeneration; // stale handles now fail ResolveCover
    entry.m_bInUse = false;
    entry.m_SectorID = plInvalidIndex;
    m_FreeCoverSectors.PushBack(ref_authored.m_uiSectorEntry);
  }

  ref_authored.m_uiSectorEntry = plInvalidIndex;
}

void plAiTacticalWorldModule::MaintainAuthoredCover(const plPhysicsWorldModuleInterface* pPhysics)
{
  if (m_AuthoredCovers.IsEmpty())
    return;

  // probe config for AutoProbe markers (mirrors the bake task, minus the edge inset)
  plAiCoverGen::ProbeConfig probeCfg;
  float fAuthoredSpacing = 1.0f;
  {
    const plAiTacticalConfig& tacticalConfig = GetWorld()->GetModule<plAiNavMeshWorldModule>()->GetConfig().m_TacticalConfig;

    probeCfg.m_fRayDistance = plMath::Max(0.1f, tacticalConfig.m_fCoverProbeDistance) + 0.5f; // markers are placed by hand; allow the wall to be a bit off
    probeCfg.m_fCrouchHeight = plMath::Max(0.1f, tacticalConfig.m_fCoverCrouchHeight);
    probeCfg.m_fStandHeight = plMath::Max(probeCfg.m_fCrouchHeight + 0.1f, tacticalConfig.m_fCoverStandHeight);
    probeCfg.m_uiCollisionLayer = m_pCoverNavMesh->GetConfig().m_uiCollisionLayer;

    fAuthoredSpacing = plMath::Max(0.25f, tacticalConfig.m_fCoverSpacing);
  }

  for (plUInt32 id = 0; id < m_AuthoredCovers.GetCount(); ++id)
  {
    AuthoredCover& authored = m_AuthoredCovers[id];

    if (!authored.m_bInUse)
      continue;

    plAiCoverPointComponent* pCover = nullptr;

    if (!authored.m_bPendingRemoval)
    {
      plComponent* pComponent = nullptr;

      if (GetWorld()->TryGetComponent(authored.m_hComponent, pComponent))
      {
        pCover = plDynamicCast<plAiCoverPointComponent*>(pComponent);
      }
    }

    if (pCover == nullptr) // unregistered or component died
    {
      RetireAuthoredEntry(authored);
      authored.m_bInUse = false;
      authored.m_bPendingRemoval = false;
      authored.m_hComponent.Invalidate();
      m_FreeAuthoredCovers.PushBack(id);
      continue;
    }

    // acquire a registry entry on first apply
    if (authored.m_uiSectorEntry == plInvalidIndex)
    {
      plUInt32 uiEntry = plInvalidIndex;

      if (!m_FreeCoverSectors.IsEmpty())
      {
        uiEntry = m_FreeCoverSectors.PeekBack();
        m_FreeCoverSectors.PopBack();
      }
      else
      {
        uiEntry = m_CoverSectors.GetCount();
        m_CoverSectors.ExpandAndGetRef();
      }

      CoverSector& entry = m_CoverSectors[uiEntry];
      entry.m_SectorID = AuthoredCoverSectorID;
      entry.m_bInUse = true;
      entry.m_bBakePending = false;
      entry.m_Points.Clear();

      authored.m_uiSectorEntry = uiEntry;
    }

    CoverSector& entry = m_CoverSectors[authored.m_uiSectorEntry];

    // strip layout: Width meters along the wall, centered on the owner (0 = a single point);
    // the strip is one cover surface, so agents can move within it
    const float fWidth = plMath::Max(0.0f, pCover->m_fWidth);
    const plUInt32 uiPointCount = (fWidth < 0.01f) ? 1u : plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Round(fWidth / fAuthoredSpacing)) + 1, 2u, 64u);

    if (!entry.m_Points.IsEmpty() && entry.m_Points.GetCount() == uiPointCount && !pCover->GetOwner()->IsDynamic())
      continue; // static owner, layout unchanged, already applied

    // layout changed (width/spacing edited at runtime): release claims, stale-out old handles
    if (!entry.m_Points.IsEmpty() && entry.m_Points.GetCount() != uiPointCount)
    {
      for (const plAiCoverPoint& oldPoint : entry.m_Points)
      {
        if (oldPoint.m_uiClaimIndex != 0xFFFF)
        {
          ReleaseClaimAtIndex(oldPoint.m_uiClaimIndex);
        }
      }

      entry.m_Points.Clear();
      entry.m_Surfaces.Clear();
      ++entry.m_uiBakeGeneration;
    }

    if (entry.m_Points.IsEmpty())
    {
      entry.m_Points.SetCount(uiPointCount); // default points start unclaimed (0xFFFF)

      plAiCoverSurface& surface = entry.m_Surfaces.ExpandAndGetRef();
      surface.m_uiFirstPoint = 0;
      surface.m_uiNumPoints = static_cast<plUInt16>(uiPointCount);
    }

    plVec3 vWallDir = pCover->GetOwner()->GetGlobalDirForwards();
    vWallDir.z = 0.0f;
    vWallDir.NormalizeIfNotZero(plVec3(1, 0, 0)).IgnoreResult();

    const plVec3 vTangent(-vWallDir.y, vWallDir.x, 0); // along the wall
    const plVec3 vCenter = pCover->GetOwner()->GetGlobalPosition();

    for (plUInt32 i = 0; i < uiPointCount; ++i)
    {
      const float fOffset = (uiPointCount == 1) ? 0.0f : (-fWidth * 0.5f + fWidth * static_cast<float>(i) / static_cast<float>(uiPointCount - 1));

      plAiCoverPoint point;
      point.m_vPosition = vCenter + vTangent * fOffset;
      point.m_vWallDir = vWallDir;
      point.m_Quality = pCover->m_Quality;
      point.m_uiWallTopHeight = static_cast<plUInt8>(plMath::Clamp(plMath::Round(pCover->m_fHeight * 10.0f), 1.0f, 255.0f));
      point.m_uiSurface = 0;

      if (pCover->m_bAutoProbe && pPhysics != nullptr)
      {
        plAiCoverPoint probed;

        if (plAiCoverGen::ProbePoint(*pPhysics, point.m_vPosition, vWallDir, probeCfg, probed))
        {
          point.m_vPosition = probed.m_vPosition; // ground-snapped
          point.m_Quality = probed.m_Quality;
          point.m_uiWallTopHeight = probed.m_uiWallTopHeight;
        }
        // probe found no wall: keep the authored values - the designer said this is cover
      }

      point.m_uiClaimIndex = entry.m_Points[i].m_uiClaimIndex; // dynamic refresh keeps claims
      entry.m_Points[i] = point;
    }
  }
}

bool plAiTacticalWorldModule::FindNearestCover(const plVec3& vPosition, float fRadius, bool bUnclaimedOnly, plAiCoverPointHandle& out_hPoint, plAiCoverPoint& out_point, float fMaxZDelta /*= 2.5f*/) const
{
  if (m_pCoverNavMesh == nullptr)
    return false;

  const float fRadiusSqr = plMath::Square(fRadius);
  float fBestDistSqr = plMath::HighValue<float>(); // 3D ranking; the radius stays an XY test
  bool bFound = false;

  for (plUInt32 uiEntry = 0; uiEntry < m_CoverSectors.GetCount(); ++uiEntry)
  {
    const CoverSector& entry = m_CoverSectors[uiEntry];

    if (!entry.m_bInUse || entry.m_Points.IsEmpty())
      continue;

    // authored entries hold few points and no real sector - skip the sector-bounds culling
    if (entry.m_SectorID != AuthoredCoverSectorID)
    {
      const plBoundingBox sectorBounds = m_pCoverNavMesh->GetSectorBounds(m_pCoverNavMesh->CalculateSectorCoord(entry.m_SectorID));
      const plVec2 vClamped(plMath::Clamp(vPosition.x, sectorBounds.m_vMin.x, sectorBounds.m_vMax.x), plMath::Clamp(vPosition.y, sectorBounds.m_vMin.y, sectorBounds.m_vMax.y));

      if ((vClamped - vPosition.GetAsVec2()).GetLengthSquared() > fRadiusSqr)
        continue;
    }

    for (plUInt32 uiPoint = 0; uiPoint < entry.m_Points.GetCount(); ++uiPoint)
    {
      const plAiCoverPoint& point = entry.m_Points[uiPoint];

      if (bUnclaimedOnly && point.m_uiClaimIndex != 0xFFFF)
        continue;

      if (fMaxZDelta > 0.0f && plMath::Abs(point.m_vPosition.z - vPosition.z) > fMaxZDelta)
        continue; // different floor

      if ((point.m_vPosition.GetAsVec2() - vPosition.GetAsVec2()).GetLengthSquared() > fRadiusSqr)
        continue;

      const float fDistSqr = (point.m_vPosition - vPosition).GetLengthSquared();

      if (fDistSqr < fBestDistSqr)
      {
        fBestDistSqr = fDistSqr;
        bFound = true;
        out_hPoint.m_uiSectorEntry = uiEntry;
        out_hPoint.m_uiPoint = static_cast<plUInt16>(uiPoint);
        out_hPoint.m_uiBakeGeneration = entry.m_uiBakeGeneration;
        out_point = point;
      }
    }
  }

  return bFound;
}

bool plAiTacticalWorldModule::ResolveCover(const plAiCoverPointHandle& hPoint, plAiCoverPoint& out_point) const
{
  if (!hPoint.IsValid() || hPoint.m_uiSectorEntry >= m_CoverSectors.GetCount())
    return false;

  const CoverSector& entry = m_CoverSectors[hPoint.m_uiSectorEntry];

  if (!entry.m_bInUse || entry.m_uiBakeGeneration != hPoint.m_uiBakeGeneration || hPoint.m_uiPoint >= entry.m_Points.GetCount())
    return false;

  out_point = entry.m_Points[hPoint.m_uiPoint];
  return true;
}

bool plAiTacticalWorldModule::GetCoverSurface(const plAiCoverPointHandle& hPoint, plDynamicArray<plAiCoverPointHandle>& out_points) const
{
  out_points.Clear();

  if (!hPoint.IsValid() || hPoint.m_uiSectorEntry >= m_CoverSectors.GetCount())
    return false;

  const CoverSector& entry = m_CoverSectors[hPoint.m_uiSectorEntry];

  if (!entry.m_bInUse || entry.m_uiBakeGeneration != hPoint.m_uiBakeGeneration || hPoint.m_uiPoint >= entry.m_Points.GetCount())
    return false;

  const plUInt16 uiSurface = entry.m_Points[hPoint.m_uiPoint].m_uiSurface;

  if (uiSurface == 0xFFFF || uiSurface >= entry.m_Surfaces.GetCount())
  {
    out_points.PushBack(hPoint); // standalone point: a surface of one
    return true;
  }

  const plAiCoverSurface& surface = entry.m_Surfaces[uiSurface];

  for (plUInt32 i = 0; i < surface.m_uiNumPoints; ++i)
  {
    plAiCoverPointHandle& handle = out_points.ExpandAndGetRef();
    handle.m_uiSectorEntry = hPoint.m_uiSectorEntry;
    handle.m_uiPoint = static_cast<plUInt16>(surface.m_uiFirstPoint + i);
    handle.m_uiBakeGeneration = entry.m_uiBakeGeneration;
  }

  return true;
}

bool plAiTacticalWorldModule::ClaimCover(const plAiCoverPointHandle& hPoint, plGameObjectHandle hClaimant, plTime timeout)
{
  if (hClaimant.IsInvalidated())
    return false;

  if (!hPoint.IsValid() || hPoint.m_uiSectorEntry >= m_CoverSectors.GetCount())
    return false;

  CoverSector& entry = m_CoverSectors[hPoint.m_uiSectorEntry];

  if (!entry.m_bInUse || entry.m_uiBakeGeneration != hPoint.m_uiBakeGeneration || hPoint.m_uiPoint >= entry.m_Points.GetCount())
    return false;

  plAiCoverPoint& point = entry.m_Points[hPoint.m_uiPoint];
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  if (point.m_uiClaimIndex != 0xFFFF)
  {
    Claim& existing = m_Claims[point.m_uiClaimIndex];

    if (existing.m_hClaimant == hClaimant)
    {
      existing.m_Expiry = now + timeout;
      existing.m_Timeout = timeout;
      return true;
    }

    return false; // taken by someone else
  }

  // one claim per claimant: drop the previous one - and at most ONE tactical claim of either
  // kind: taking cover means letting go of a smart object and vice versa
  ReleaseClaim(hClaimant);
  ReleaseSmartObjectClaim(hClaimant);

  plUInt32 uiClaim = plInvalidIndex;

  if (!m_FreeClaims.IsEmpty())
  {
    uiClaim = m_FreeClaims.PeekBack();
    m_FreeClaims.PopBack();
  }
  else
  {
    uiClaim = m_Claims.GetCount();
    m_Claims.ExpandAndGetRef();
  }

  Claim& claim = m_Claims[uiClaim];
  claim.m_hClaimant = hClaimant;
  claim.m_hPoint = hPoint;
  claim.m_Expiry = now + timeout;
  claim.m_Timeout = timeout;
  claim.m_bInUse = true;

  point.m_uiClaimIndex = static_cast<plUInt16>(uiClaim);
  m_ClaimByOwner[hClaimant] = uiClaim;

  return true;
}

void plAiTacticalWorldModule::ReleaseClaimAtIndex(plUInt32 uiClaimIndex)
{
  if (uiClaimIndex >= m_Claims.GetCount() || !m_Claims[uiClaimIndex].m_bInUse)
    return;

  Claim& claim = m_Claims[uiClaimIndex];

  // clear the back-reference on the point, if the handle still resolves
  if (claim.m_hPoint.IsValid() && claim.m_hPoint.m_uiSectorEntry < m_CoverSectors.GetCount())
  {
    CoverSector& entry = m_CoverSectors[claim.m_hPoint.m_uiSectorEntry];

    if (entry.m_bInUse && entry.m_uiBakeGeneration == claim.m_hPoint.m_uiBakeGeneration && claim.m_hPoint.m_uiPoint < entry.m_Points.GetCount())
    {
      entry.m_Points[claim.m_hPoint.m_uiPoint].m_uiClaimIndex = 0xFFFF;
    }
  }

  m_ClaimByOwner.Remove(claim.m_hClaimant);
  claim.m_bInUse = false;
  claim.m_hClaimant.Invalidate();
  claim.m_hPoint.Invalidate();

  m_FreeClaims.PushBack(uiClaimIndex);
}

void plAiTacticalWorldModule::ReleaseClaim(plGameObjectHandle hClaimant)
{
  plUInt32 uiClaim = plInvalidIndex;

  if (m_ClaimByOwner.TryGetValue(hClaimant, uiClaim))
  {
    ReleaseClaimAtIndex(uiClaim);
  }
}

bool plAiTacticalWorldModule::GetClaimedCover(plGameObjectHandle hClaimant, plAiCoverPointHandle& out_hPoint) const
{
  plUInt32 uiClaim = plInvalidIndex;

  if (!m_ClaimByOwner.TryGetValue(hClaimant, uiClaim))
    return false;

  const Claim& claim = m_Claims[uiClaim];

  if (!claim.m_bInUse)
    return false;

  out_hPoint = claim.m_hPoint;
  return true;
}

void plAiTacticalWorldModule::SweepClaims()
{
  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  for (plUInt32 uiClaim = 0; uiClaim < m_Claims.GetCount(); ++uiClaim)
  {
    Claim& claim = m_Claims[uiClaim];

    if (!claim.m_bInUse)
      continue;

    plGameObject* pClaimant = nullptr;

    if (!GetWorld()->TryGetObject(claim.m_hClaimant, pClaimant))
    {
      ReleaseClaimAtIndex(uiClaim); // claimant died
      continue;
    }

    plAiCoverPoint point;

    if (!ResolveCover(claim.m_hPoint, point))
    {
      ReleaseClaimAtIndex(uiClaim); // point's sector re-baked (defensive; invalidation already releases)
      continue;
    }

    // occupied cover never expires: refresh while the claimant stands at the point
    const plVec3 vClaimantPos = pClaimant->GetGlobalPosition();
    const float fZRange = cvar_TacticalCoverOccupiedZRange.GetValue();

    if ((vClaimantPos.GetAsVec2() - point.m_vPosition.GetAsVec2()).GetLengthSquared() < plMath::Square(plMath::Max(0.5f, cvar_TacticalCoverOccupiedRadius.GetValue())) &&
        (fZRange <= 0.0f || plMath::Abs(vClaimantPos.z - point.m_vPosition.z) < fZRange))
    {
      claim.m_Expiry = now + claim.m_Timeout;
    }
    else if (now > claim.m_Expiry)
    {
      ReleaseClaimAtIndex(uiClaim);
    }
  }
}

void plAiTacticalWorldModule::DrawCoverVisualization()
{
  plDynamicArray<plDebugRenderer::Line> lines;

  for (const CoverSector& entry : m_CoverSectors)
  {
    if (!entry.m_bInUse)
      continue;

    const bool bAuthored = entry.m_SectorID == AuthoredCoverSectorID;

    auto pointColor = [bAuthored](const plAiCoverPoint& point) -> plColor {
      if (point.m_uiClaimIndex != 0xFFFF)
        return plColor::Red;

      if (bAuthored)
        return plColor::Cyan;

      return (point.m_Quality == plAiCoverQuality::High) ? plColor::LawnGreen : plColor::Yellow;
    };

    auto wallTop = [](const plAiCoverPoint& point) -> float {
      if (point.m_uiWallTopHeight > 0)
        return plMath::Clamp(point.m_uiWallTopHeight * 0.1f, 0.2f, 2.5f);

      return (point.m_Quality == plAiCoverQuality::High) ? 0.6f : 0.35f;
    };

    for (const plAiCoverPoint& point : entry.m_Points)
    {
      const plColor color = pointColor(point);

      // tick height = measured wall top where available
      auto& tick = lines.ExpandAndGetRef();
      tick.m_start = point.m_vPosition;
      tick.m_end = point.m_vPosition + plVec3(0, 0, wallTop(point));
      tick.m_startColor = tick.m_endColor = color;

      auto& wallDash = lines.ExpandAndGetRef();
      wallDash.m_start = point.m_vPosition + plVec3(0, 0, 0.2f);
      wallDash.m_end = wallDash.m_start + point.m_vWallDir * 0.35f;
      wallDash.m_startColor = wallDash.m_endColor = color;
    }

    // wall bands: ground + top rails between the points of each cover surface, so strips read
    // as continuous wall segments instead of separate ticks
    for (const plAiCoverSurface& surface : entry.m_Surfaces)
    {
      for (plUInt32 i = 1; i < surface.m_uiNumPoints; ++i)
      {
        const plAiCoverPoint& p0 = entry.m_Points[surface.m_uiFirstPoint + i - 1];
        const plAiCoverPoint& p1 = entry.m_Points[surface.m_uiFirstPoint + i];

        const plColor color0 = pointColor(p0);
        const plColor color1 = pointColor(p1);

        auto& ground = lines.ExpandAndGetRef();
        ground.m_start = p0.m_vPosition + plVec3(0, 0, 0.05f);
        ground.m_end = p1.m_vPosition + plVec3(0, 0, 0.05f);
        ground.m_startColor = color0;
        ground.m_endColor = color1;

        auto& top = lines.ExpandAndGetRef();
        top.m_start = p0.m_vPosition + plVec3(0, 0, wallTop(p0));
        top.m_end = p1.m_vPosition + plVec3(0, 0, wallTop(p1));
        top.m_startColor = color0;
        top.m_endColor = color1;
      }
    }
  }

  if (!lines.IsEmpty())
  {
    plDebugRenderer::DrawLines(GetWorld(), lines, plColor::White);
  }
}

void plAiTacticalWorldModule::DrawStats()
{
  m_uiStatBakedSectors = 0;
  m_uiStatTotalPoints = 0;
  m_uiStatBakesInFlight = 0;
  plUInt32 uiAuthoredPoints = 0;

  for (const CoverSector& entry : m_CoverSectors)
  {
    if (entry.m_bInUse && !entry.m_Points.IsEmpty())
    {
      if (entry.m_SectorID == AuthoredCoverSectorID)
      {
        uiAuthoredPoints += entry.m_Points.GetCount();
        continue;
      }

      ++m_uiStatBakedSectors;
      m_uiStatTotalPoints += entry.m_Points.GetCount();
    }
  }

  for (const auto& slot : m_BakeSlots)
  {
    if (slot.m_bInFlight)
    {
      ++m_uiStatBakesInFlight;
    }
  }

  plUInt32 uiActiveClaims = 0;

  for (const Claim& claim : m_Claims)
  {
    if (claim.m_bInUse)
    {
      ++uiActiveClaims;
    }
  }

  plStringBuilder sStats;
  sStats.AppendFormat("Cover: {} sectors, {} points ({} authored)\n", m_uiStatBakedSectors, m_uiStatTotalPoints, uiAuthoredPoints);
  sStats.AppendFormat("Bakes: {} in flight, {} queued\n", m_uiStatBakesInFlight, m_BakeQueue.GetCount());
  sStats.AppendFormat("Claims: {} active\n", uiActiveClaims);
  sStats.AppendFormat("Queries: {} executed, {} queued, {} LOS rays\n", m_uiStatQueriesExecuted, m_PendingQueue.GetCount(), static_cast<plInt32>(m_uiStatRaycastsThisFrame));

  plDebugRenderer::DrawInfoText(GetWorld(), plDebugTextPlacement::TopLeft, "AiTacticalStats", sStats);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Implementation_TacticalWorldModule);
