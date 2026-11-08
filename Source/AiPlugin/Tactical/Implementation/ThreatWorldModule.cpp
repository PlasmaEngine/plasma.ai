#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <AiPlugin/Tactical/ThreatWorldModule.h>
#include <AiPlugin/UtilityAI/Decision/AiBrainWorldModule.h>
#include <AiPlugin/Utils/RcMath.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/Utils/Blackboard.h>
#include <Core/World/World.h>
#include <DetourNavMesh.h>
#include <Foundation/Configuration/CVar.h>
#include <GameEngine/Gameplay/BlackboardComponent.h>
#include <RendererCore/Debug/DebugRenderer.h>

// What the threat map IS (cells of 2 m, the layers) is fixed; the cvars below tune its cost and show it.
plCVarBool cvar_ThreatEnable("AI.Threat.Enable", true, plCVarFlags::Default, "Maintain the per-team threat maps. Off: queries and agents see no threat and no danger.");
plCVarFloat cvar_ThreatRange("AI.Threat.Range", 40.0f, plCVarFlags::Default, "How far from a known enemy position the threat reaches, in meters.");
plCVarFloat cvar_ThreatTickInterval("AI.Threat.TickInterval", 0.25f, plCVarFlags::Default, "Target interval between threat map passes of a team, in seconds (the squad tick rate).");
plCVarInt cvar_ThreatRaycastsPerFrame("AI.Threat.RaycastsPerFrame", 192, plCVarFlags::Default, "Line-of-sight raycasts per frame the threat maps may spend on refreshing their cells.");
plCVarFloat cvar_ThreatEvadeDanger("AI.Threat.EvadeDanger", 0.3f, plCVarFlags::Default, "An agent whose claimed cover has at least this much danger ends its behavior and picks its cover again.");
plCVarInt cvar_ThreatVisualize("AI.Threat.Visualize", 0, plCVarFlags::None, "Draw the threat map: 0 = off, 1 = combined, 2 = threat, 3 = visibility, 4 = danger.");
plCVarString cvar_ThreatVisualizeTeam("AI.Threat.VisualizeTeam", "", plCVarFlags::None, "The team whose threat map is drawn. Empty = the team with the most data.");

PL_IMPLEMENT_WORLD_MODULE(plAiThreatWorldModule);
// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiThreatWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

namespace
{
  constexpr float s_fEyeHeight = 1.6f;    // of the enemy whose line of sight counts
  constexpr float s_fChestHeight = 1.0f;  // of the position the line of sight must reach
  constexpr plUInt32 s_uiMaxSources = 8;  // per team: the strongest ones count
  constexpr float s_fHeightQueryCost = 0.1f;

  /// A target without an object is told apart by its position, rounded to 2 m.
  plUInt32 GetSourceId(const plGameObjectHandle& hTarget, const plVec3& vPosition)
  {
    if (!hTarget.IsInvalidated())
      return hTarget.GetInternalID().m_Data;

    const plInt32 x = static_cast<plInt32>(plMath::Floor(vPosition.x * 0.5f));
    const plInt32 y = static_cast<plInt32>(plMath::Floor(vPosition.y * 0.5f));
    return 0x80000000u | (static_cast<plUInt32>(x & 0x7FFF) << 15) | static_cast<plUInt32>(y & 0x7FFF);
  }

  void AddQuad(plDynamicArray<plDebugRenderer::Triangle>& ref_triangles, const plVec3& vCenter, float fHalf, const plColor& color)
  {
    const plVec3 p00(vCenter.x - fHalf, vCenter.y - fHalf, vCenter.z);
    const plVec3 p10(vCenter.x + fHalf, vCenter.y - fHalf, vCenter.z);
    const plVec3 p11(vCenter.x + fHalf, vCenter.y + fHalf, vCenter.z);
    const plVec3 p01(vCenter.x - fHalf, vCenter.y + fHalf, vCenter.z);

    auto addTriangle = [&](const plVec3& a, const plVec3& b, const plVec3& c) {
      plDebugRenderer::Triangle& tri = ref_triangles.ExpandAndGetRef();
      tri.m_position[0] = a;
      tri.m_position[1] = b;
      tri.m_position[2] = c;
      tri.m_color = color;
    };

    addTriangle(p00, p10, p11);
    addTriangle(p00, p11, p01);
    addTriangle(p00, p11, p10);
    addTriangle(p00, p01, p11);
  }
} // namespace

plAiThreatWorldModule::plAiThreatWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
}

plAiThreatWorldModule::~plAiThreatWorldModule() = default;

void plAiThreatWorldModule::Initialize()
{
  SUPER::Initialize();

  // the map reads this frame's navmesh changes: create its dependency before registering the update
  GetWorld()->GetOrCreateModule<plAiNavMeshWorldModule>();

  auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiThreatWorldModule::Update, this);
  updateDesc.m_Phase = plWorldModule::UpdateFunctionDesc::Phase::PostTransform;
  updateDesc.m_DependsOn.PushBack(plMakeHashedString("plAiNavMeshWorldModule::Update"));
  updateDesc.m_bOnlyUpdateWhenSimulating = true;
  updateDesc.m_fPriority = -900.0f;

  RegisterUpdateFunction(updateDesc);
}

void plAiThreatWorldModule::Deinitialize()
{
  m_Teams.Clear();
  m_TeamLookup.Clear();

  SUPER::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// danger areas

plAiThreatWorldModule::DangerID plAiThreatWorldModule::AddDangerArea(const plVec3& vPosition, float fRadius, plTime duration, plStringView sTeam)
{
  plUInt32 uiIndex = plInvalidIndex;

  if (!m_FreeAreas.IsEmpty())
  {
    uiIndex = m_FreeAreas.PeekBack();
    m_FreeAreas.PopBack();
  }
  else
  {
    uiIndex = m_Areas.GetCount();
    m_Areas.ExpandAndGetRef();
  }

  Area& area = m_Areas[uiIndex];
  area.m_vPosition = vPosition;
  area.m_fRadius = plMath::Max(fRadius, 0.0f);
  area.m_Start = GetWorld()->GetClock().GetAccumulatedTime();
  area.m_Duration = duration.IsPositive() ? duration : plTime::MakeZero();
  area.m_sTeam.Assign(sTeam);
  area.m_uiGeneration = static_cast<plUInt16>((area.m_uiGeneration + 1) & 0xFFF);
  area.m_bInUse = true;

  m_bAreasDirty = true;
  return uiIndex | (static_cast<plUInt32>(area.m_uiGeneration) << 20);
}

void plAiThreatWorldModule::MoveDangerArea(DangerID id, const plVec3& vPosition)
{
  const plUInt32 uiIndex = id & 0xFFFFF;

  if (uiIndex >= m_Areas.GetCount() || !m_Areas[uiIndex].m_bInUse || m_Areas[uiIndex].m_uiGeneration != (id >> 20))
    return;

  m_Areas[uiIndex].m_vPosition = vPosition;
  m_bAreasDirty = true;
}

void plAiThreatWorldModule::RemoveDangerArea(DangerID id)
{
  const plUInt32 uiIndex = id & 0xFFFFF;

  if (uiIndex >= m_Areas.GetCount() || !m_Areas[uiIndex].m_bInUse || m_Areas[uiIndex].m_uiGeneration != (id >> 20))
    return;

  m_Areas[uiIndex].m_bInUse = false;
  m_FreeAreas.PushBack(uiIndex);
  m_bAreasDirty = true;
}

plUInt32 plAiThreatWorldModule::GetDangerAreaCount() const
{
  plUInt32 uiCount = 0;

  for (const Area& area : m_Areas)
  {
    uiCount += area.m_bInUse ? 1 : 0;
  }

  return uiCount;
}

void plAiThreatWorldModule::UpdateAreas(plTime now)
{
  for (plUInt32 i = 0; i < m_Areas.GetCount(); ++i)
  {
    Area& area = m_Areas[i];

    if (!area.m_bInUse)
      continue;

    if (area.m_Duration.IsPositive())
    {
      // a fading area changes with every frame; the next publish picks it up
      m_bAreasDirty = true;

      if (now - area.m_Start >= area.m_Duration)
      {
        area.m_bInUse = false;
        m_FreeAreas.PushBack(i);
        continue;
      }
    }

    EnsureAreaChunks(area);
  }
}

void plAiThreatWorldModule::EnsureAreaChunks(const Area& area)
{
  if (m_pNavMesh == nullptr)
    return;

  // the chunks under the area must exist, so its danger has cells to show in
  for (TeamMap& team : m_Teams)
  {
    plAiThreatSnapshot& snapshot = *team.m_pWorking;
    const float fSector = snapshot.GetSectorSize();

    for (float y = area.m_vPosition.y - area.m_fRadius; y <= area.m_vPosition.y + area.m_fRadius + fSector; y += fSector)
    {
      for (float x = area.m_vPosition.x - area.m_fRadius; x <= area.m_vPosition.x + area.m_fRadius + fSector; x += fSector)
      {
        const plVec3 vProbe(plMath::Min(x, area.m_vPosition.x + area.m_fRadius), plMath::Min(y, area.m_vPosition.y + area.m_fRadius), area.m_vPosition.z);
        const plUInt32 uiKey = snapshot.GetChunkKey(vProbe);

        if (uiKey == plInvalidIndex)
          continue;

        const plAiNavMeshSector* pSector = m_pNavMesh->GetSector(uiKey);
        if (pSector == nullptr || pSector->m_FlagUsable == 0)
          continue;

        plAiThreatSnapshot::Chunk& chunk = snapshot.GetOrCreateChunk(uiKey);

        if (chunk.m_bZStale)
        {
          PrepareChunk(snapshot, uiKey, chunk);
        }
      }
    }
  }
}

void plAiThreatWorldModule::FillAreas(const TeamMap& team, plAiThreatSnapshot& ref_snapshot, plTime now) const
{
  ref_snapshot.GetAreas().Clear();

  for (const Area& area : m_Areas)
  {
    if (!area.m_bInUse || (!area.m_sTeam.IsEmpty() && area.m_sTeam != team.m_sTeam))
      continue;

    plAiThreatDangerArea& out = ref_snapshot.GetAreas().ExpandAndGetRef();
    out.m_vPosition = area.m_vPosition;
    out.m_fRadius = area.m_fRadius;
    out.m_fFade = area.m_Duration.IsPositive() ? plMath::Saturate(1.0f - static_cast<float>((now - area.m_Start).GetSeconds() / area.m_Duration.GetSeconds())) : 1.0f;
  }
}

//////////////////////////////////////////////////////////////////////////
// queries

plSharedPtr<const plAiThreatSnapshot> plAiThreatWorldModule::GetSnapshot(const plTempHashedString& sTeam) const
{
  plUInt32 uiIndex = 0;

  if (!m_TeamLookup.TryGetValue(sTeam.GetHash(), uiIndex))
    return nullptr;

  return m_Teams[uiIndex].m_pPublished;
}

float plAiThreatWorldModule::GetThreat(const plTempHashedString& sTeam, const plVec3& vPosition) const
{
  const plSharedPtr<const plAiThreatSnapshot> pSnapshot = GetSnapshot(sTeam);
  return pSnapshot != nullptr ? pSnapshot->GetThreat(vPosition) : 0.0f;
}

float plAiThreatWorldModule::GetVisibility(const plTempHashedString& sTeam, const plVec3& vPosition) const
{
  const plSharedPtr<const plAiThreatSnapshot> pSnapshot = GetSnapshot(sTeam);
  return pSnapshot != nullptr ? pSnapshot->GetVisibility(vPosition) : 0.0f;
}

float plAiThreatWorldModule::GetDanger(const plTempHashedString& sTeam, const plVec3& vPosition) const
{
  const plSharedPtr<const plAiThreatSnapshot> pSnapshot = GetSnapshot(sTeam);
  return pSnapshot != nullptr ? pSnapshot->GetDanger(vPosition) : 0.0f;
}

void plAiThreatWorldModule::GetDebugSources(const plTempHashedString& sTeam, plDynamicArray<DebugSource>& out_sources) const
{
  out_sources.Clear();

  plUInt32 uiIndex = 0;
  if (!m_TeamLookup.TryGetValue(sTeam.GetHash(), uiIndex))
    return;

  for (const Source& source : m_Teams[uiIndex].m_Sources)
  {
    DebugSource& out = out_sources.ExpandAndGetRef();
    out.m_vPosition = source.m_vPosition;
    out.m_fConfidence = source.m_fConfidence;
  }
}

const plAiThreatSnapshot* plAiThreatWorldModule::GetVisualizedSnapshot(plHashedString* out_pTeam) const
{
  const TeamMap* pBest = nullptr;
  plUInt32 uiBestChunks = 0;

  const plStringView sWanted = cvar_ThreatVisualizeTeam.GetValue();

  for (const TeamMap& team : m_Teams)
  {
    if (team.m_pWorking == nullptr)
      continue;

    if (!sWanted.IsEmpty())
    {
      if (team.m_sTeam.GetView() == sWanted)
      {
        pBest = &team;
        break;
      }

      continue;
    }

    const plUInt32 uiChunks = team.m_pWorking->GetChunks().GetCount();

    if (pBest == nullptr || uiChunks > uiBestChunks)
    {
      pBest = &team;
      uiBestChunks = uiChunks;
    }
  }

  if (pBest == nullptr)
    return nullptr;

  if (out_pTeam != nullptr)
  {
    *out_pTeam = pBest->m_sTeam;
  }

  return pBest->m_pWorking.Borrow();
}

//////////////////////////////////////////////////////////////////////////
// the update

plAiNavMesh* plAiThreatWorldModule::ResolveNavMesh(plAiNavMeshWorldModule& navMeshModule) const
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

void plAiThreatWorldModule::ConfigureGrid(const plAiNavMesh& navMesh)
{
  const plAiNavmeshConfig& config = navMesh.GetConfig();

  for (TeamMap& team : m_Teams)
  {
    team.m_pWorking->Configure(navMesh.GetSectorSize(), config.m_uiNumSectorsX, config.m_uiNumSectorsY, CellSize);
    team.m_pPublished = nullptr;
    team.m_bPassActive = false;
    team.m_bPublishPending = true;
  }

  m_bNavQueryReady = !dtStatusFailed(m_NavQuery.init(navMesh.GetDetourNavMesh(), 64));
}

plAiThreatWorldModule::TeamMap& plAiThreatWorldModule::GetOrCreateTeam(const plHashedString& sTeam)
{
  plUInt32 uiIndex = 0;

  if (m_TeamLookup.TryGetValue(sTeam.GetHash(), uiIndex))
    return m_Teams[uiIndex];

  uiIndex = m_Teams.GetCount();
  TeamMap& team = m_Teams.ExpandAndGetRef();
  team.m_sTeam = sTeam;
  team.m_pWorking = PL_DEFAULT_NEW(plAiThreatSnapshot);

  if (m_pNavMesh != nullptr)
  {
    team.m_pWorking->Configure(m_pNavMesh->GetSectorSize(), m_pNavMesh->GetConfig().m_uiNumSectorsX, m_pNavMesh->GetConfig().m_uiNumSectorsY, CellSize);
  }

  team.m_bPublishPending = true;
  m_TeamLookup.Insert(sTeam.GetHash(), uiIndex);
  return team;
}

void plAiThreatWorldModule::MarkChangedSectors(plArrayPtr<const plUInt32> changedSectors)
{
  for (TeamMap& team : m_Teams)
  {
    for (const plUInt32 uiSector : changedSectors)
    {
      if (plAiThreatSnapshot::Chunk* pChunk = team.m_pWorking->FindChunk(uiSector))
      {
        pChunk->m_bZStale = true;
      }
    }
  }
}

void plAiThreatWorldModule::Update(const UpdateContext& ctxt)
{
  if (!cvar_ThreatEnable)
    return;

  auto* pNavMeshModule = GetWorld()->GetModule<plAiNavMeshWorldModule>();
  if (pNavMeshModule == nullptr)
    return;

  m_pNavMeshModule = pNavMeshModule;
  m_pPhysics = GetWorld()->GetModule<plPhysicsWorldModuleInterface>();

  plAiNavMesh* pNavMesh = ResolveNavMesh(*pNavMeshModule);
  if (pNavMesh == nullptr || !pNavMesh->IsValid())
    return;

  if (pNavMesh != m_pNavMesh)
  {
    m_pNavMesh = pNavMesh;
    ConfigureGrid(*pNavMesh);
  }

  MarkChangedSectors(pNavMeshModule->GetChangedSectorsThisFrame(pNavMesh));

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  UpdateAreas(now);

  if (now >= m_NextTick)
  {
    m_NextTick = now + plTime::Seconds(plMath::Max(0.05f, cvar_ThreatTickInterval.GetValue()));
    TickAgents(now);
  }

  m_iRaycastsThisFrame = 0;
  RunPasses(static_cast<float>(plMath::Clamp<plInt32>(cvar_ThreatRaycastsPerFrame, 1, 4096)));

  for (TeamMap& team : m_Teams)
  {
    if (m_bAreasDirty || team.m_bPublishPending)
    {
      Publish(team, now);
    }
  }

  m_bAreasDirty = false;

  m_Stats.m_uiTeams = m_Teams.GetCount();
  m_Stats.m_uiDangerAreas = GetDangerAreaCount();
  m_Stats.m_uiRaycasts = static_cast<plUInt32>(m_iRaycastsThisFrame);

  Draw();
}

void plAiThreatWorldModule::TickAgents(plTime now)
{
  auto* pBrain = GetWorld()->GetModule<plAiBrainWorldModule>();
  if (pBrain == nullptr)
    return;

  plDynamicArray<plAiBrainWorldModule::ThreatAgent> agents;
  plDynamicArray<plAiBrainWorldModule::ThreatSource> sources;
  pBrain->GatherThreatInfo(agents, sources);

  // every team that has an agent has a map, so danger areas reach it before an enemy is ever seen
  for (const auto& agent : agents)
  {
    if (!agent.m_sTeam.IsEmpty())
    {
      GetOrCreateTeam(agent.m_sTeam);
    }
  }

  for (const InjectedSource& injected : m_InjectedSources)
  {
    GetOrCreateTeam(injected.m_sTeam);
  }

  m_Stats.m_uiSources = 0;

  for (TeamMap& team : m_Teams)
  {
    // the strongest sources of the team, one per remembered enemy
    plHybridArray<Source, 16> teamSources;

    for (const auto& source : sources)
    {
      if (source.m_sTeam != team.m_sTeam)
        continue;

      const plUInt32 uiId = GetSourceId(source.m_hTarget, source.m_vPosition);
      Source* pExisting = nullptr;

      for (Source& other : teamSources)
      {
        if (other.m_uiTargetId == uiId)
          pExisting = &other;
      }

      if (pExisting == nullptr)
      {
        Source& added = teamSources.ExpandAndGetRef();
        added.m_vPosition = source.m_vPosition;
        added.m_fConfidence = source.m_fConfidence;
        added.m_uiTargetId = uiId;
      }
      else if (source.m_fConfidence > pExisting->m_fConfidence)
      {
        pExisting->m_vPosition = source.m_vPosition;
        pExisting->m_fConfidence = source.m_fConfidence;
      }
    }

    for (const InjectedSource& injected : m_InjectedSources)
    {
      if (injected.m_sTeam == team.m_sTeam)
      {
        teamSources.PushBack(injected.m_Source);
      }
    }

    teamSources.Sort([](const Source& a, const Source& b) { return a.m_fConfidence > b.m_fConfidence; });

    if (teamSources.GetCount() > s_uiMaxSources)
    {
      teamSources.SetCount(s_uiMaxSources);
    }

    m_Stats.m_uiSources += teamSources.GetCount();

    // a pass in progress finishes with the sources it started with; the next one sees the new ones
    if (!team.m_bPassActive)
    {
      StartPass(team, teamSources);
    }
    else
    {
      team.m_Sources = teamSources; // the debug views show the latest
    }
  }

  // agents read the map at their feet
  for (const auto& agent : agents)
  {
    plGameObject* pOwner = nullptr;
    if (!GetWorld()->TryGetObject(agent.m_hOwner, pOwner))
      continue;

    const plSharedPtr<plBlackboard>& pBlackboard = plBlackboardComponent::FindBlackboard(pOwner);
    if (pBlackboard == nullptr)
      continue;

    static const plHashedString sThreatHere = plMakeHashedString("Ai_ThreatHere");
    static const plHashedString sDangerHere = plMakeHashedString("Ai_DangerHere");

    const plVec3 vPosition = pOwner->GetGlobalPosition();
    pBlackboard->SetEntryValue(sThreatHere, GetThreat(agent.m_sTeam, vPosition));
    pBlackboard->SetEntryValue(sDangerHere, GetDanger(agent.m_sTeam, vPosition));
  }

  EvadeDanger(now);
}

void plAiThreatWorldModule::StartPass(TeamMap& ref_team, plArrayPtr<const Source> sources)
{
  ref_team.m_Sources.Clear();
  ref_team.m_Sources.PushBackRange(sources);
  ref_team.m_PassChunks.Clear();
  ref_team.m_uiPassChunk = 0;
  ref_team.m_uiPassCell = 0;

  plAiThreatSnapshot& snapshot = *ref_team.m_pWorking;
  const float fRange = plMath::Max(cvar_ThreatRange.GetValue(), 1.0f);
  const float fSector = snapshot.GetSectorSize();

  if (fSector <= 0.0f)
    return;

  plHybridArray<plUInt32, 32> keys;

  // the chunks within reach of a known enemy
  for (const Source& source : sources)
  {
    const plInt32 iMinX = static_cast<plInt32>(plMath::Floor((source.m_vPosition.x - fRange) / fSector + snapshot.GetSectorsX() * 0.5f));
    const plInt32 iMaxX = static_cast<plInt32>(plMath::Floor((source.m_vPosition.x + fRange) / fSector + snapshot.GetSectorsX() * 0.5f));
    const plInt32 iMinY = static_cast<plInt32>(plMath::Floor((source.m_vPosition.y - fRange) / fSector + snapshot.GetSectorsY() * 0.5f));
    const plInt32 iMaxY = static_cast<plInt32>(plMath::Floor((source.m_vPosition.y + fRange) / fSector + snapshot.GetSectorsY() * 0.5f));

    for (plInt32 y = iMinY; y <= iMaxY; ++y)
    {
      for (plInt32 x = iMinX; x <= iMaxX; ++x)
      {
        if (x < 0 || y < 0 || x >= static_cast<plInt32>(snapshot.GetSectorsX()) || y >= static_cast<plInt32>(snapshot.GetSectorsY()))
          continue;

        const plUInt32 uiKey = static_cast<plUInt32>(y) * snapshot.GetSectorsX() + static_cast<plUInt32>(x);

        if (!keys.Contains(uiKey))
        {
          keys.PushBack(uiKey);
        }
      }
    }
  }

  for (const plUInt32 uiKey : keys)
  {
    const plAiNavMeshSector* pSector = m_pNavMesh->GetSector(uiKey);

    if (pSector != nullptr && pSector->m_FlagUsable == 1)
    {
      snapshot.GetOrCreateChunk(uiKey);
      ref_team.m_PassChunks.PushBack(uiKey);
    }
  }

  // chunks that still hold old values are cleared by the pass
  for (auto it = snapshot.GetChunks().GetIterator(); it.IsValid(); ++it)
  {
    if (it.Value().m_bAnyData && !ref_team.m_PassChunks.Contains(it.Key()))
    {
      ref_team.m_PassChunks.PushBack(it.Key());
    }
  }

  ref_team.m_bPassActive = !ref_team.m_PassChunks.IsEmpty();
}

void plAiThreatWorldModule::RunPasses(float fBudget)
{
  for (TeamMap& team : m_Teams)
  {
    if (!team.m_bPassActive)
      continue;

    RunPass(team, fBudget);

    if (fBudget <= 0.0f)
      break;
  }
}

void plAiThreatWorldModule::PrepareChunk(plAiThreatSnapshot& ref_snapshot, plUInt32 uiKey, plAiThreatSnapshot::Chunk& ref_chunk)
{
  ref_chunk.m_bZStale = false;

  const plAiNavMeshSector* pSector = m_pNavMesh->GetSector(uiKey);
  const plUInt32 uiCells = ref_snapshot.GetCellsPerChunk();

  if (pSector == nullptr || !m_bNavQueryReady || !pSector->m_bHasTileBounds)
  {
    for (plUInt32 i = 0; i < uiCells; ++i)
    {
      ref_chunk.m_Valid[i] = 0;
    }

    return;
  }

  const dtQueryFilter& filter = m_pNavMeshModule->GetPathSearchFilter("");
  const float fMidZ = (pSector->m_fTileMinZ + pSector->m_fTileMaxZ) * 0.5f;
  const float fHalfZ = (pSector->m_fTileMaxZ - pSector->m_fTileMinZ) * 0.5f + 1.0f;
  const float fHalfCell = ref_snapshot.GetCellSize() * 0.5f;
  const float fExtents[3] = {fHalfCell, fHalfZ, fHalfCell};

  for (plUInt32 i = 0; i < uiCells; ++i)
  {
    const plVec2 vCenter = ref_snapshot.GetCellCenter(uiKey, i);

    dtPolyRef polyRef = 0;
    plRcPos nearest;
    ref_chunk.m_Valid[i] = 0;

    if (dtStatusFailed(m_NavQuery.findNearestPoly(plRcPos(plVec3(vCenter.x, vCenter.y, fMidZ)), fExtents, &filter, &polyRef, nearest)) || polyRef == 0)
      continue;

    const plVec3 vNearest = nearest;

    // a cell whose center lies outside the walkable area has no ground to stand on
    if ((vNearest.GetAsVec2() - vCenter).GetLengthSquared() > plMath::Square(fHalfCell * 1.5f))
      continue;

    ref_chunk.m_Valid[i] = 1;
    ref_chunk.m_Z[i] = vNearest.z;
  }
}

void plAiThreatWorldModule::RunPass(TeamMap& ref_team, float& ref_fBudget)
{
  plAiThreatSnapshot& snapshot = *ref_team.m_pWorking;
  const float fRange = plMath::Max(cvar_ThreatRange.GetValue(), 1.0f);
  const plUInt32 uiCells = snapshot.GetCellsPerChunk();
  const plUInt8 uiLayer = m_pNavMesh->GetConfig().m_uiCollisionLayer;
  const plPhysicsQueryParameters params(uiLayer, plPhysicsShapeType::Static);

  while (ref_team.m_uiPassChunk < ref_team.m_PassChunks.GetCount() && ref_fBudget > 0.0f)
  {
    const plUInt32 uiKey = ref_team.m_PassChunks[ref_team.m_uiPassChunk];
    plAiThreatSnapshot::Chunk* pChunk = snapshot.FindChunk(uiKey);

    if (pChunk == nullptr)
    {
      ++ref_team.m_uiPassChunk;
      ref_team.m_uiPassCell = 0;
      continue;
    }

    if (pChunk->m_bZStale)
    {
      PrepareChunk(snapshot, uiKey, *pChunk);
      ref_fBudget -= uiCells * s_fHeightQueryCost;
    }

    bool bAnyData = false;

    // the cursor is inside a chunk, so a budget that runs out resumes where it stopped
    for (; ref_team.m_uiPassCell < uiCells && ref_fBudget > 0.0f; ++ref_team.m_uiPassCell)
    {
      const plUInt32 uiCell = ref_team.m_uiPassCell;

      pChunk->m_Threat[uiCell] = 0.0f;
      pChunk->m_Visible[uiCell] = 0;

      if (pChunk->m_Valid[uiCell] == 0)
        continue;

      const plVec2 vCenter = snapshot.GetCellCenter(uiKey, uiCell);
      const plVec3 vTarget(vCenter.x, vCenter.y, pChunk->m_Z[uiCell] + s_fChestHeight);

      float fThreat = 0.0f;
      bool bVisible = false;

      for (const Source& source : ref_team.m_Sources)
      {
        const float fDistance = (vCenter - source.m_vPosition.GetAsVec2()).GetLength();

        if (fDistance >= fRange)
          continue;

        bool bSees = true;

        if (m_pPhysics != nullptr && fDistance > 0.5f)
        {
          const plVec3 vEye = source.m_vPosition + plVec3(0, 0, s_fEyeHeight);
          plVec3 vDirection = vTarget - vEye;
          const float fLength = vDirection.GetLength();

          if (fLength > 0.01f)
          {
            vDirection /= fLength;

            plPhysicsCastResult hit;
            bSees = !m_pPhysics->Raycast(hit, vEye, vDirection, fLength - 0.05f, params);

            ref_fBudget -= 1.0f;
            ++m_iRaycastsThisFrame;
          }
        }

        if (bSees)
        {
          bVisible = true;
          fThreat = plMath::Max(fThreat, (1.0f - fDistance / fRange) * source.m_fConfidence);
        }
      }

      pChunk->m_Threat[uiCell] = fThreat;
      pChunk->m_Visible[uiCell] = bVisible ? 1 : 0;
      bAnyData |= fThreat > 0.0f || bVisible;
    }

    if (ref_team.m_uiPassCell >= uiCells)
    {
      // the chunk is done: whatever it still shows is fresh
      pChunk->m_bAnyData = bAnyData || pChunk->m_bAnyData;
      ++ref_team.m_uiPassChunk;
      ref_team.m_uiPassCell = 0;
    }
    else
    {
      pChunk->m_bAnyData = true; // partially refreshed; the next pass must clear what is left
    }
  }

  if (ref_team.m_uiPassChunk >= ref_team.m_PassChunks.GetCount())
  {
    // fully refreshed chunks that came out empty need no clearing next time
    for (const plUInt32 uiKey : ref_team.m_PassChunks)
    {
      if (plAiThreatSnapshot::Chunk* pChunk = snapshot.FindChunk(uiKey))
      {
        bool bAny = false;

        for (plUInt32 i = 0; i < uiCells && !bAny; ++i)
        {
          bAny = pChunk->m_Threat[i] > 0.0f || pChunk->m_Visible[i] != 0;
        }

        pChunk->m_bAnyData = bAny;
      }
    }

    ref_team.m_bPassActive = false;
    ref_team.m_bPublishPending = true;
  }
}

void plAiThreatWorldModule::Publish(TeamMap& ref_team, plTime now)
{
  ref_team.m_bPublishPending = false;

  plAiThreatSnapshot& working = *ref_team.m_pWorking;
  working.SetTime(now.GetSeconds());
  FillAreas(ref_team, working, now);

  plSharedPtr<plAiThreatSnapshot> pCopy = PL_DEFAULT_NEW(plAiThreatSnapshot);
  pCopy->CopyFrom(working);
  ref_team.m_pPublished = pCopy;

  m_Stats.m_uiChunks = 0;
  m_Stats.m_uiCells = 0;

  for (const TeamMap& team : m_Teams)
  {
    m_Stats.m_uiChunks += team.m_pWorking->GetChunks().GetCount();
  }

  m_Stats.m_uiCells = m_Stats.m_uiChunks * working.GetCellsPerChunk();
}

//////////////////////////////////////////////////////////////////////////
// danger evasion

void plAiThreatWorldModule::EvadeDanger(plTime now)
{
  auto* pTactical = GetWorld()->GetModule<plAiTacticalWorldModule>();
  auto* pBrain = GetWorld()->GetModule<plAiBrainWorldModule>();

  if (pTactical == nullptr || pBrain == nullptr || m_Areas.IsEmpty())
    return;

  plDynamicArray<plAiTacticalWorldModule::ClaimedCover> claims;
  pTactical->GetClaimedCovers(claims);

  if (claims.IsEmpty())
    return;

  plDynamicArray<plAiBrainWorldModule::ThreatAgent> agents;
  plDynamicArray<plAiBrainWorldModule::ThreatSource> sources;
  pBrain->GatherThreatInfo(agents, sources);

  const float fLimit = plMath::Max(cvar_ThreatEvadeDanger.GetValue(), 0.01f);

  for (const auto& claim : claims)
  {
    const plAiBrainWorldModule::ThreatAgent* pAgent = nullptr;

    for (const auto& agent : agents)
    {
      if (agent.m_hOwner == claim.m_hClaimant)
        pAgent = &agent;
    }

    if (pAgent == nullptr)
      continue;

    // the danger at the cover point, from the live areas that apply to the agent's team
    float fDanger = 0.0f;

    for (const Area& area : m_Areas)
    {
      if (!area.m_bInUse || (!area.m_sTeam.IsEmpty() && area.m_sTeam != pAgent->m_sTeam))
        continue;

      plAiThreatDangerArea shape;
      shape.m_vPosition = area.m_vPosition;
      shape.m_fRadius = area.m_fRadius;
      shape.m_fFade = area.m_Duration.IsPositive() ? plMath::Saturate(1.0f - static_cast<float>((now - area.m_Start).GetSeconds() / area.m_Duration.GetSeconds())) : 1.0f;
      fDanger = plMath::Max(fDanger, shape.GetDanger(claim.m_vPosition));
    }

    if (fDanger < fLimit)
      continue;

    // once is enough: the new pick avoids the area, and a second reaction must not cut it short
    plTime lastEvaded;
    const plUInt32 uiId = claim.m_hClaimant.GetInternalID().m_Data;
    if (m_EvadedAt.TryGetValue(uiId, lastEvaded) && (now - lastEvaded).GetSeconds() < 1.0)
      continue;

    m_EvadedAt[uiId] = now;
    ++m_Stats.m_uiEvaded;

    pTactical->ReleaseClaim(claim.m_hClaimant);
    pBrain->InterruptBehavior(pAgent->m_uiSlot);
  }
}

//////////////////////////////////////////////////////////////////////////
// debug views

void plAiThreatWorldModule::Draw()
{
  const plInt32 iLayer = cvar_ThreatVisualize;
  if (iLayer <= 0)
    return;

  plHashedString sTeam;
  const plAiThreatSnapshot* pSnapshot = GetVisualizedSnapshot(&sTeam);

  if (pSnapshot == nullptr)
  {
    plDebugRenderer::DrawInfoText(GetWorld(), plDebugTextPlacement::TopRight, "AiThreatMap", "Threat map: no team has an agent yet");
    return;
  }

  const plTime now = GetWorld()->GetClock().GetAccumulatedTime();

  plDynamicArray<plDebugRenderer::Triangle> triangles;
  const float fHalf = pSnapshot->GetCellSize() * 0.5f - 0.05f;
  const plUInt32 uiCells = pSnapshot->GetCellsPerChunk();

  // the colors of the mockup: the AI's rose for threat, a warm red for danger
  const plColor threatColor = plColorGammaUB(205, 140, 150);
  const plColor dangerColor = plColorGammaUB(215, 130, 120);

  for (auto it = pSnapshot->GetChunks().GetIterator(); it.IsValid(); ++it)
  {
    const plAiThreatSnapshot::Chunk& chunk = it.Value();

    if (!chunk.m_bAnyData && pSnapshot->GetAreas().IsEmpty())
      continue;

    for (plUInt32 i = 0; i < uiCells; ++i)
    {
      if (chunk.m_Valid[i] == 0)
        continue;

      const plVec2 vCenter = pSnapshot->GetCellCenter(it.Key(), i);
      const plVec3 vPosition(vCenter.x, vCenter.y, chunk.m_Z[i] + 0.12f);

      const float fThreat = chunk.m_Threat[i];
      const float fDanger = pSnapshot->GetDanger(vPosition);
      const bool bVisible = chunk.m_Visible[i] != 0;

      plColor color = plColor::MakeZero();

      switch (iLayer)
      {
        case 1:
        {
          const float t = plMath::Max(fThreat * 0.7f, fDanger);
          if (t > 0.02f)
          {
            color = fDanger > fThreat ? dangerColor : threatColor;
            color.a = t * 0.7f;
          }
          break;
        }

        case 2:
          if (fThreat > 0.02f)
          {
            color = threatColor;
            color.a = fThreat * 0.7f;
          }
          break;

        case 3:
          if (chunk.m_bAnyData)
          {
            color = bVisible ? plColor(0.85f, 0.86f, 0.88f, 0.10f) : plColor(0.0f, 0.0f, 0.0f, 0.25f);
          }
          break;

        default:
          if (fDanger > 0.02f)
          {
            color = dangerColor;
            color.a = fDanger * 0.75f;
          }
          break;
      }

      if (color.a > 0.0f)
      {
        AddQuad(triangles, vPosition, fHalf, color);
      }
    }
  }

  if (!triangles.IsEmpty())
  {
    plDebugRenderer::DrawSolidTriangles(GetWorld(), triangles, plColor::White);
  }

  // the danger areas: a dashed circle and the time that is left
  plDynamicArray<plDebugRenderer::Line> lines;

  for (const plAiThreatDangerArea& area : pSnapshot->GetAreas())
  {
    constexpr plUInt32 uiSegments = 32;
    for (plUInt32 i = 0; i < uiSegments; i += 2)
    {
      const float a0 = plMath::Pi<float>() * 2.0f * i / uiSegments;
      const float a1 = plMath::Pi<float>() * 2.0f * (i + 1) / uiSegments;

      plDebugRenderer::Line& line = lines.ExpandAndGetRef();
      line.m_start = area.m_vPosition + plVec3(plMath::Cos(plAngle::MakeFromRadian(a0)), plMath::Sin(plAngle::MakeFromRadian(a0)), 0.15f) * area.m_fRadius;
      line.m_end = area.m_vPosition + plVec3(plMath::Cos(plAngle::MakeFromRadian(a1)), plMath::Sin(plAngle::MakeFromRadian(a1)), 0.15f) * area.m_fRadius;
      line.m_start.z = line.m_end.z = area.m_vPosition.z + 0.15f;
      line.m_startColor = line.m_endColor = dangerColor;
    }
  }

  // the enemy positions the team remembers: a ring, sized by how sure the agents are
  const plColor sourceColor = plColorGammaUB(0xdc, 0xc3, 0x6e);
  plDynamicArray<DebugSource> sources;
  GetDebugSources(sTeam, sources);

  for (const DebugSource& source : sources)
  {
    constexpr plUInt32 uiSegments = 16;
    const float fRadius = 0.4f + 0.6f * source.m_fConfidence;

    for (plUInt32 i = 0; i < uiSegments; ++i)
    {
      const float a0 = plMath::Pi<float>() * 2.0f * i / uiSegments;
      const float a1 = plMath::Pi<float>() * 2.0f * (i + 1) / uiSegments;

      plDebugRenderer::Line& line = lines.ExpandAndGetRef();
      line.m_start = source.m_vPosition + plVec3(plMath::Cos(plAngle::MakeFromRadian(a0)) * fRadius, plMath::Sin(plAngle::MakeFromRadian(a0)) * fRadius, 0.2f);
      line.m_end = source.m_vPosition + plVec3(plMath::Cos(plAngle::MakeFromRadian(a1)) * fRadius, plMath::Sin(plAngle::MakeFromRadian(a1)) * fRadius, 0.2f);
      line.m_startColor = sourceColor;
      line.m_endColor = sourceColor;
    }

    plStringBuilder sLabel;
    sLabel.SetFormat("known enemy {}", plArgF(source.m_fConfidence, 2));
    plDebugRenderer::Draw3DText(GetWorld(), sLabel, source.m_vPosition + plVec3(0, 0, 2.2f), sourceColor, 14);
  }

  if (!lines.IsEmpty())
  {
    plDebugRenderer::DrawLines(GetWorld(), lines, plColor::White);
  }

  for (const plAiThreatDangerArea& area : pSnapshot->GetAreas())
  {
    plStringBuilder sLabel;
    sLabel.SetFormat("danger {}", plArgF(area.m_fFade, 2));
    plDebugRenderer::Draw3DText(GetWorld(), sLabel, area.m_vPosition + plVec3(0, 0, 0.6f), dangerColor, 14);
  }

  // the stats box and the legend
  {
    plStringBuilder sStats;
    sStats.SetFormat("Threat map \xC2\xB7 team {}\nCells\t{} of {} m\nKnown enemies\t{}\nDanger areas\t{}\nRaycasts\t{} / frame", sTeam.GetView(), pSnapshot->GetChunks().GetCount() * uiCells, plArgF(pSnapshot->GetCellSize(), 0), sources.GetCount(), pSnapshot->GetAreas().GetCount(), m_Stats.m_uiRaycasts);
    plDebugRenderer::DrawInfoText(GetWorld(), plDebugTextPlacement::TopRight, "AiThreatMap", sStats);

    const char* szLegend = nullptr;
    switch (iLayer)
    {
      case 1:
        szLegend = "Threat (rose) and danger (red), per team";
        break;
      case 2:
        szLegend = "Threat: 0 to 1 with distance and sureness of the enemy position";
        break;
      case 3:
        szLegend = "Lit = a known enemy sees it";
        break;
      default:
        szLegend = "Danger areas fade with time";
        break;
    }

    plDebugRenderer::DrawInfoText(GetWorld(), plDebugTextPlacement::BottomRight, "AiThreatMapLegend", szLegend, plColorGammaUB(0x9a, 0xa0, 0xa8));
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Implementation_ThreatWorldModule);
