#include <AiPlugin/Navigation/CrowdWorldModule.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Utils/RcMath.h>
#include <Core/World/World.h>
#include <DetourProximityGrid.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/Threading/TaskSystem.h>
#include <RendererCore/Debug/DebugRenderer.h>

plCVarBool cvar_AiCrowdEnable("AI.Crowd.Enable", true, plCVarFlags::Default, "Enable local avoidance between navigating agents.");
plCVarBool cvar_AiCrowdVisualize("AI.Crowd.Visualize", false, plCVarFlags::Default, "Visualize crowd agents: radius, desired (yellow) vs adjusted (green) velocity, wall segments (red).");
plCVarFloat cvar_AiCrowdSeparationWeight("AI.Crowd.SeparationWeight", 2.0f, plCVarFlags::Default, "Strength of the separation force used by the Low avoidance quality tier.");
plCVarInt cvar_AiCrowdQualityOverride("AI.Crowd.QualityOverride", -1, plCVarFlags::Default, "Force an avoidance quality tier for all agents: 0 = Low, 1 = Medium, 2 = High, -1 = per-agent setting.");

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiCrowdAvoidanceQuality, 1)
  PL_ENUM_CONSTANTS(plAiCrowdAvoidanceQuality::Low, plAiCrowdAvoidanceQuality::Medium, plAiCrowdAvoidanceQuality::High)
PL_END_STATIC_REFLECTED_ENUM;

PL_IMPLEMENT_WORLD_MODULE(plAiCrowdWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiCrowdWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiCrowdWorldModule::Scratch::Scratch()
{
  m_pAvoidance = dtAllocObstacleAvoidanceQuery();
  m_pAvoidance->init(32, 8);
}

plAiCrowdWorldModule::Scratch::~Scratch()
{
  dtFreeObstacleAvoidanceQuery(m_pAvoidance);
  m_pAvoidance = nullptr;
}

plAiCrowdWorldModule::plAiCrowdWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
  // seeded with the parameter presets that dtCrowd ships with - proven starting points
  for (auto& params : m_AvoidanceParams)
  {
    params.velBias = 0.4f;
    params.weightDesVel = 2.0f;
    params.weightCurVel = 0.75f;
    params.weightSide = 0.75f;
    params.weightToi = 2.5f;
    params.horizTime = 2.5f;
    params.gridSize = 33;
    params.adaptiveDivs = 7;
    params.adaptiveRings = 2;
    params.adaptiveDepth = 5;
  }

  // Medium uses fewer adaptive resources via grid sampling; keep the defaults otherwise
  m_AvoidanceParams[plAiCrowdAvoidanceQuality::Medium].gridSize = 33;
}

plAiCrowdWorldModule::~plAiCrowdWorldModule() = default;

void plAiCrowdWorldModule::Initialize()
{
  SUPER::Initialize();

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiCrowdWorldModule::UpdateSolve, this);
    updateDesc.m_Phase = plWorldUpdatePhase::PostAsync;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;
    RegisterUpdateFunction(updateDesc);
  }

  m_pGrid = dtAllocProximityGrid();
  m_pGrid->init(1024, 2.0f);
}

void plAiCrowdWorldModule::Deinitialize()
{
  dtFreeProximityGrid(m_pGrid);
  m_pGrid = nullptr;

  m_Slots.Clear();
  m_Scratch.Clear();

  SUPER::Deinitialize();
}

plAiCrowdWorldModule::AgentID plAiCrowdWorldModule::RegisterAgent()
{
  AgentID id = InvalidAgentID;

  if (!m_FreeSlots.IsEmpty())
  {
    id = m_FreeSlots.PeekBack();
    m_FreeSlots.PopBack();
  }
  else
  {
    if (m_Slots.GetCount() >= InvalidAgentID)
    {
      plLog::Error("AI crowd: too many agents ({}).", m_Slots.GetCount());
      return InvalidAgentID;
    }

    id = static_cast<AgentID>(m_Slots.GetCount());
    m_Slots.ExpandAndGetRef();
    m_Results[0].SetCount(m_Slots.GetCount());
    m_Results[1].SetCount(m_Slots.GetCount());
  }

  Slot& slot = m_Slots[id];
  slot.m_bInUse = true;
  slot.m_uiSubmitFrame = 0;
  slot.m_State = plAiCrowdAgentState();
  slot.m_Boundary.reset();
  slot.m_vBoundaryUpdatePos = plVec3(plMath::HighValue<float>());
  slot.m_pBoundaryNavMesh = nullptr;

  // stale results from a previous user of this slot must not leak to the new agent
  m_Results[0][id] = Result();
  m_Results[1][id] = Result();

  return id;
}

void plAiCrowdWorldModule::UnregisterAgent(AgentID id)
{
  if (id >= m_Slots.GetCount() || !m_Slots[id].m_bInUse)
    return;

  m_Slots[id].m_bInUse = false;
  m_FreeSlots.PushBack(id);
}

void plAiCrowdWorldModule::SubmitAgentState(AgentID id, const plAiCrowdAgentState& state)
{
  if (id >= m_Slots.GetCount() || !m_Slots[id].m_bInUse)
    return;

  Slot& slot = m_Slots[id];
  slot.m_State = state;
  slot.m_uiSubmitFrame = m_uiFrameCounter;
}

bool plAiCrowdWorldModule::TryGetAdjustedVelocity(AgentID id, plVec3& out_vVelocity) const
{
  if (id >= m_Slots.GetCount() || !m_Slots[id].m_bInUse)
    return false;

  const Result& result = m_Results[m_uiReadBuffer][id];

  if (result.m_uiSolveFrame == 0 || (m_uiFrameCounter - result.m_uiSolveFrame) > 2)
    return false; // no recent solve for this agent

  out_vVelocity = result.m_vVelocity;
  return true;
}

void plAiCrowdWorldModule::UpdateSolve(const UpdateContext& context)
{
  ++m_uiFrameCounter; // never 0 -> a zero solve-frame in a Result means 'never solved'

  if (!cvar_AiCrowdEnable)
    return;

  // collect everyone who submitted state this frame
  m_SolveList.Clear();

  for (plUInt32 uiSlot = 0; uiSlot < m_Slots.GetCount(); ++uiSlot)
  {
    const Slot& slot = m_Slots[uiSlot];

    if (slot.m_bInUse && slot.m_uiSubmitFrame == m_uiFrameCounter - 1)
    {
      m_SolveList.PushBack(static_cast<AgentID>(uiSlot));
    }
  }

  if (!m_SolveList.IsEmpty())
  {
    // group agents of the same navmesh so the per-scratch nav queries rarely re-init
    m_SolveList.Sort([this](AgentID lhs, AgentID rhs) {
      return m_Slots[lhs].m_State.m_pNavMesh < m_Slots[rhs].m_State.m_pNavMesh;
    });

    // rebuild the proximity grid (pure 2D, engine XY)
    m_pGrid->clear();

    for (AgentID id : m_SolveList)
    {
      const plAiCrowdAgentState& state = m_Slots[id].m_State;
      m_pGrid->addItem(id, state.m_vPosition.x - state.m_fRadius, state.m_vPosition.y - state.m_fRadius,
        state.m_vPosition.x + state.m_fRadius, state.m_vPosition.y + state.m_fRadius);
    }

    // one scratch per potential parallel invocation
    const plUInt32 uiMaxInvocations = plTaskSystem::GetWorkerThreadCount(plWorkerThreadType::ShortTasks) * 2 + 1;

    while (m_Scratch.GetCount() < uiMaxInvocations)
    {
      m_Scratch.ExpandAndGetRef();
    }

    m_uiScratchCounter = 0;

    plParallelForParams params;
    params.m_uiBinSize = 16;
    params.m_uiMaxTasksPerThread = 2;

    plTaskSystem::ParallelForIndexed(0u, m_SolveList.GetCount(),
      [this](plUInt32 uiFirst, plUInt32 uiEnd) {
        const plUInt32 uiScratchIndex = static_cast<plUInt32>(m_uiScratchCounter.Increment() - 1);

        for (plUInt32 i = uiFirst; i < uiEnd; ++i)
        {
          SolveAgent(i, uiScratchIndex);
        }
      },
      "AiCrowdSolve", plTaskNesting::Never, params);

    // results just written become readable, agents consume them next frame
    m_uiReadBuffer ^= 1;
  }

  if (cvar_AiCrowdVisualize)
  {
    DrawDebugVisualization();
  }
}

void plAiCrowdWorldModule::SolveAgent(plUInt32 uiSolveIndex, plUInt32 uiScratchIndex)
{
  const AgentID id = m_SolveList[uiSolveIndex];
  Slot& slot = m_Slots[id];
  const plAiCrowdAgentState& state = slot.m_State;

  if (state.m_bObstacleOnly)
    return; // influences others (via the grid), never influenced

  const plUInt32 uiWriteBuffer = m_uiReadBuffer ^ 1;

  plAiCrowdAvoidanceQuality::Enum quality = static_cast<plAiCrowdAvoidanceQuality::Enum>(state.m_Quality.GetValue());

  const plInt32 iQualityOverride = cvar_AiCrowdQualityOverride;
  if (iQualityOverride >= 0 && iQualityOverride <= 2)
  {
    quality = static_cast<plAiCrowdAvoidanceQuality::Enum>(iQualityOverride);
  }

  // ---- gather neighbors from the proximity grid ----
  const float fQueryRange = state.m_fRadius * 12.0f; // dtCrowd's default collision query range

  unsigned short queriedIds[32];
  const int iNumQueried = m_pGrid->queryItems(state.m_vPosition.x - fQueryRange, state.m_vPosition.y - fQueryRange,
    state.m_vPosition.x + fQueryRange, state.m_vPosition.y + fQueryRange, queriedIds, PL_ARRAY_SIZE(queriedIds));

  struct Neighbor
  {
    AgentID m_ID;
    float m_fDistSquared;
  };

  Neighbor neighbors[6]; // dtCrowd caps at 6 neighbors as well
  plUInt32 uiNumNeighbors = 0;

  for (int i = 0; i < iNumQueried; ++i)
  {
    const AgentID nid = queriedIds[i];

    if (nid == id || nid >= m_Slots.GetCount())
      continue;

    const Slot& nslot = m_Slots[nid];

    if (!nslot.m_bInUse || nslot.m_uiSubmitFrame != slot.m_uiSubmitFrame)
      continue; // not part of this solve

    const float fDistSquared = (nslot.m_State.m_vPosition - state.m_vPosition).GetLengthSquared();

    if (fDistSquared > plMath::Square(fQueryRange))
      continue;

    // insertion sort into the fixed neighbor array, closest first
    plUInt32 uiInsert = uiNumNeighbors < PL_ARRAY_SIZE(neighbors) ? uiNumNeighbors : PL_ARRAY_SIZE(neighbors) - 1;

    if (uiInsert == PL_ARRAY_SIZE(neighbors) - 1 && uiNumNeighbors == PL_ARRAY_SIZE(neighbors) && neighbors[uiInsert].m_fDistSquared <= fDistSquared)
      continue;

    while (uiInsert > 0 && neighbors[uiInsert - 1].m_fDistSquared > fDistSquared)
    {
      neighbors[uiInsert] = neighbors[uiInsert - 1];
      --uiInsert;
    }

    neighbors[uiInsert] = {nid, fDistSquared};
    uiNumNeighbors = plMath::Min<plUInt32>(uiNumNeighbors + 1, PL_ARRAY_SIZE(neighbors));
  }

  plVec3 vNewVelocity = state.m_vDesiredVelocity;

  const bool bCanUseWalls = state.m_CurrentPoly != 0 && state.m_pNavMesh != nullptr && state.m_pFilter != nullptr;

  if (quality == plAiCrowdAvoidanceQuality::Low || !bCanUseWalls)
  {
    // ---- separation force only ----
    plVec3 vDisplacement = plVec3::MakeZero();

    for (plUInt32 i = 0; i < uiNumNeighbors; ++i)
    {
      const plAiCrowdAgentState& nstate = m_Slots[neighbors[i].m_ID].m_State;

      plVec3 vDiff = state.m_vPosition - nstate.m_vPosition;
      vDiff.z = 0.0f;

      const float fDist = vDiff.GetLength();
      const float fSepRange = (state.m_fRadius + nstate.m_fRadius) * 4.0f;

      if (fDist >= fSepRange || fDist < 0.0001f)
        continue;

      const float fWeight = cvar_AiCrowdSeparationWeight * (1.0f - plMath::Square(fDist / fSepRange));
      vDisplacement += vDiff * (fWeight / fDist);
    }

    vNewVelocity = state.m_vDesiredVelocity + vDisplacement;
    vNewVelocity.z = 0.0f;

    const float fSpeed = vNewVelocity.GetLength();
    if (fSpeed > state.m_fMaxSpeed)
    {
      vNewVelocity *= state.m_fMaxSpeed / fSpeed;
    }
  }
  else
  {
    // ---- sampled velocity obstacles + navmesh wall segments ----
    Scratch& scratch = m_Scratch[uiScratchIndex];

    if (scratch.m_pQueryInitializedFor != state.m_pNavMesh)
    {
      scratch.m_NavQuery.init(state.m_pNavMesh->GetDetourNavMesh(), 512);
      scratch.m_pQueryInitializedFor = state.m_pNavMesh;
    }

    // refresh the local wall boundary only when the agent moved far enough (dtCrowd's own memo)
    if (slot.m_pBoundaryNavMesh != state.m_pNavMesh ||
        (state.m_vPosition - slot.m_vBoundaryUpdatePos).GetLengthSquared() > plMath::Square(fQueryRange * 0.25f))
    {
      slot.m_Boundary.update(state.m_CurrentPoly, plRcPos(state.m_vPosition), fQueryRange, &scratch.m_NavQuery, state.m_pFilter);
      slot.m_vBoundaryUpdatePos = state.m_vPosition;
      slot.m_pBoundaryNavMesh = state.m_pNavMesh;
    }

    if (uiNumNeighbors == 0 && slot.m_Boundary.getSegmentCount() == 0)
    {
      // nothing to avoid: pass the desired velocity through exactly instead of sampling it
      vNewVelocity.z = 0.0f;
    }
    else
    {
      dtObstacleAvoidanceQuery* pAvoidance = scratch.m_pAvoidance;
      pAvoidance->reset();

      for (plUInt32 i = 0; i < uiNumNeighbors; ++i)
      {
        const plAiCrowdAgentState& nstate = m_Slots[neighbors[i].m_ID].m_State;
        pAvoidance->addCircle(plRcPos(nstate.m_vPosition), nstate.m_fRadius, plRcPos(nstate.m_vVelocity), plRcPos(nstate.m_vDesiredVelocity));
      }

      for (int i = 0; i < slot.m_Boundary.getSegmentCount(); ++i)
      {
        const float* pSegment = slot.m_Boundary.getSegment(i);
        pAvoidance->addSegment(pSegment, pSegment + 3);
      }

      float navVelocity[3];
      const dtObstacleAvoidanceParams& params = m_AvoidanceParams[quality];

      if (quality == plAiCrowdAvoidanceQuality::High)
      {
        pAvoidance->sampleVelocityAdaptive(plRcPos(state.m_vPosition), state.m_fRadius, state.m_fMaxSpeed,
          plRcPos(state.m_vVelocity), plRcPos(state.m_vDesiredVelocity), navVelocity, &params, nullptr);
      }
      else
      {
        pAvoidance->sampleVelocityGrid(plRcPos(state.m_vPosition), state.m_fRadius, state.m_fMaxSpeed,
          plRcPos(state.m_vVelocity), plRcPos(state.m_vDesiredVelocity), navVelocity, &params, nullptr);
      }

      vNewVelocity = plRcPos(navVelocity);
      vNewVelocity.z = 0.0f;
    }
  }

  m_Results[uiWriteBuffer][id] = {vNewVelocity, m_uiFrameCounter};
}

void plAiCrowdWorldModule::DrawDebugVisualization()
{
  plDynamicArray<plDebugRenderer::Line> wallLines;

  for (AgentID id : m_SolveList)
  {
    const Slot& slot = m_Slots[id];
    const plAiCrowdAgentState& state = slot.m_State;

    const plColor agentColor = state.m_bObstacleOnly ? plColor::Orange : plColor::CornflowerBlue;
    plDebugRenderer::DrawLineSphere(GetWorld(), plBoundingSphere::MakeFromCenterAndRadius(state.m_vPosition + plVec3(0, 0, state.m_fRadius), state.m_fRadius), agentColor);

    if (state.m_bObstacleOnly)
      continue;

    plDebugRenderer::Line velocityLines[2];
    velocityLines[0] = plDebugRenderer::Line(state.m_vPosition, state.m_vPosition + state.m_vDesiredVelocity);
    velocityLines[0].m_startColor = velocityLines[0].m_endColor = plColor::Yellow;

    plVec3 vAdjusted;
    if (TryGetAdjustedVelocity(id, vAdjusted))
    {
      velocityLines[1] = plDebugRenderer::Line(state.m_vPosition + plVec3(0, 0, 0.05f), state.m_vPosition + vAdjusted + plVec3(0, 0, 0.05f));
      velocityLines[1].m_startColor = velocityLines[1].m_endColor = plColor::LawnGreen;
      plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(velocityLines, 2), plColor::White);
    }
    else
    {
      plDebugRenderer::DrawLines(GetWorld(), plMakeArrayPtr(velocityLines, 1), plColor::White);
    }

    for (int i = 0; i < slot.m_Boundary.getSegmentCount(); ++i)
    {
      const float* pSegment = slot.m_Boundary.getSegment(i);
      auto& line = wallLines.ExpandAndGetRef();
      line.m_start = plRcPos(pSegment);
      line.m_end = plRcPos(pSegment + 3);
      line.m_startColor = line.m_endColor = plColor::Red;
    }
  }

  if (!wallLines.IsEmpty())
  {
    plDebugRenderer::DrawLines(GetWorld(), wallLines, plColor::White);
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Implementation_CrowdWorldModule);
