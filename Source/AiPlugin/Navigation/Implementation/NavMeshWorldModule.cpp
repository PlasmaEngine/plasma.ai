#include <AiPlugin/Navigation/Components/AiDestructionResponseComponent.h>
#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <AiPlugin/Tactical/TacticalWorldModule.h>
#include <Core/Interfaces/NavmeshGeoWorldModule.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Core/World/World.h>
#include <DetourNavMesh.h>
#include <Foundation/Configuration/CVar.h>
#include <RendererCore/Debug/DebugRenderer.h>

plCVarInt cvar_NavMeshVisualize("AI.Navmesh.Visualize", -1, plCVarFlags::None, "Visualize the n-th navmesh.");
plCVarInt cvar_NavMeshMaxConcurrentBuilds("AI.Navmesh.MaxConcurrentBuilds", 4, plCVarFlags::Default, "How many navmesh sectors may build in parallel. 1 = serial (old behavior).");
plCVarBool cvar_NavMeshShowBuildStats("AI.Navmesh.ShowBuildStats", false, plCVarFlags::Default, "Show navmesh generation statistics on screen.");
plCVarBool cvar_NavMeshVisualizeBlockers("AI.Navmesh.VisualizeBlockers", false, plCVarFlags::Default, "Visualize nav blocker volumes (red = blocking, yellow = avoid, grey = inactive) and dynamic-obstacle boxes (orange = carve, yellow = avoid).");
plCVarBool cvar_NavMeshVisualizeLinks("AI.Navmesh.VisualizeLinks", false, plCVarFlags::Default, "Visualize nav links (off-mesh connections), colored by link type.");
plCVarFloat cvar_NavMeshAvoidZoneCost("AI.Navmesh.AvoidZoneCost", 10.0f, plCVarFlags::Default, "Path cost multiplier for soft avoidance zones (obstacle/blocker mode 'Avoid'). 1 = no avoidance.");

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plAiNavLinkType, 1)
  PL_ENUM_CONSTANTS(plAiNavLinkType::Jump, plAiNavLinkType::Vault, plAiNavLinkType::Ladder, plAiNavLinkType::Drop, plAiNavLinkType::Door, plAiNavLinkType::Custom)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plAiNavObstacleMode, 1)
  PL_ENUM_CONSTANTS(plAiNavObstacleMode::Block, plAiNavObstacleMode::Avoid)
PL_END_STATIC_REFLECTED_ENUM;

PL_IMPLEMENT_WORLD_MODULE(plAiNavMeshWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiNavMeshWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiNavMeshWorldModule::plAiNavMeshWorldModule(plWorld* pWorld)
  : plWorldModule(pWorld)
{
  m_Config.Load().IgnoreResult();

  {
    // add a default filter
    auto& cfg = m_Config.m_PathSearchConfigs.ExpandAndGetRef();
  }

  m_fAppliedAvoidZoneCost = plMath::Max(1.0f, cvar_NavMeshAvoidZoneCost.GetValue());

  for (const auto& cfg : m_Config.m_PathSearchConfigs)
  {
    auto& filter = m_PathSearchFilters[cfg.m_sName];

    plUInt64 groundMask = 0;
    for (plUInt32 gt = 0; gt < plAiNumGroundTypes; ++gt)
    {
      if (cfg.m_bGroundTypeAllowed[gt])
        groundMask |= (1ull << gt);

      filter.setAreaCost((int)gt, cfg.m_fGroundTypeCost[gt]);
    }

    // soft avoidance zones are always traversable, but at a steep cost penalty
    groundMask |= (1ull << plAiNavMeshAvoidZoneAreaID);
    filter.setAreaCost(plAiNavMeshAvoidZoneAreaID, m_fAppliedAvoidZoneCost);

    filter.setIncludeAreaBits(groundMask);

    // runtime poly flags: walk normal polys and nav links, never enter runtime-blocked polys (doors etc.)
    filter.setIncludeFlags(plAiNavMeshPolyFlags::Walkable | plAiNavMeshPolyFlags::OffMeshLink);
    filter.setExcludeFlags(plAiNavMeshPolyFlags::Blocked);
  }

  if (m_Config.m_NavmeshConfigs.IsEmpty())
  {
    // insert a default navmesh config
    m_Config.m_NavmeshConfigs.ExpandAndGetRef();
  }
}

plAiNavMeshWorldModule::~plAiNavMeshWorldModule()
{
  for (const auto& cfg : m_Config.m_NavmeshConfigs)
  {
    PL_DEFAULT_DELETE(m_WorldNavMeshes[cfg.m_sName]);
  }
}

void plAiNavMeshWorldModule::Initialize()
{
  SUPER::Initialize();

  {
    auto updateDesc = PL_CREATE_MODULE_UPDATE_FUNCTION_DESC(plAiNavMeshWorldModule::Update, this);
    updateDesc.m_Phase = plWorldModule::UpdateFunctionDesc::Phase::PostTransform;
    updateDesc.m_bOnlyUpdateWhenSimulating = true;

    RegisterUpdateFunction(updateDesc);
  }

  m_WorldNavMeshes.Clear();

  for (const auto& cfg : m_Config.m_NavmeshConfigs)
  {
    // TODO: make tile size etc configurable
    m_WorldNavMeshes[cfg.m_sName] = PL_DEFAULT_NEW(plAiNavMesh, cfg);
  }

}

void plAiNavMeshWorldModule::Deinitialize()
{
  for (auto& slot : m_BuildSlots)
  {
    if (slot.m_bInFlight)
    {
      plTaskSystem::CancelGroup(slot.m_TaskID).IgnoreResult();
      plTaskSystem::WaitForGroup(slot.m_TaskID);
    }

    slot.m_pTask = nullptr;
  }

  m_BuildSlots.Clear();
}

plAiNavMesh* plAiNavMeshWorldModule::GetNavMesh(plStringView sName)
{
  auto it = m_WorldNavMeshes.Find(sName);
  if (it.IsValid())
    return it.Value();

  return nullptr;
}

const plAiNavMesh* plAiNavMeshWorldModule::GetNavMesh(plStringView sName) const
{
  auto it = m_WorldNavMeshes.Find(sName);
  if (it.IsValid())
    return it.Value();

  return nullptr;
}

bool plAiNavMeshWorldModule::RequestRegion(plStringView sNavmesh, const plBoundingBox& worldBox)
{
  const plVec2 vCenter = worldBox.GetCenter().GetAsVec2();
  const plVec2 vHalfExtents = worldBox.GetHalfExtents().GetAsVec2();

  if (!sNavmesh.IsEmpty())
  {
    plAiNavMesh* pNavMesh = GetNavMesh(sNavmesh);

    if (pNavMesh == nullptr)
      return false;

    return pNavMesh->RequestSector(vCenter, vHalfExtents);
  }

  bool bAllReady = true;

  for (auto& nm : m_WorldNavMeshes)
  {
    // no short-circuit: every navmesh must receive the request side effect
    bAllReady &= nm.Value()->RequestSector(vCenter, vHalfExtents);
  }

  return bAllReady;
}

bool plAiNavMeshWorldModule::IsSectorBuildInFlight(const plAiNavMesh* pNavMesh, plAiNavMesh::SectorID sectorID) const
{
  for (const auto& slot : m_BuildSlots)
  {
    if (slot.m_bInFlight && slot.m_pTask->m_pWorldNavMesh == pNavMesh && slot.m_pTask->m_SectorID == sectorID)
      return true;
  }

  return false;
}

void plAiNavMeshWorldModule::Update(const UpdateContext& ctxt)
{
  // Defer the reverse dependency until this module is registered in the world.
  // Tactical::Initialize creates its navmesh dependency, so creating tactical
  // during our Initialize would recursively construct both modules forever.
  // A newly created tactical module catches up through QueueInitialBakes.
  GetWorld()->GetOrCreateModule<plAiTacticalWorldModule>();

  // Publish an empty current-frame change set even when no geometry provider
  // exists. Consumers must never observe last frame's changes on that path.
  m_uiChangedSectorsUpdateCounter = GetWorld()->GetUpdateCounter();
  for (auto it = m_LastChangedSectors.GetIterator(); it.IsValid(); ++it)
    it.Value().Clear();

  auto pNavGeo = GetWorld()->GetOrCreateModule<plNavmeshGeoWorldModuleInterface>();
  if (pNavGeo == nullptr)
    return;

  if (!m_bReadyToGenerate)
  {
    // This runs in the PostTransform phase: if a physics module exists, its simulation start
    // (PreAsync, earlier this frame) has already flushed all initially queued static bodies,
    // so the collision geometry that navmesh generation samples is complete.
    // Without a physics module, the navgeo implementation is its own source and ready immediately.
    m_bReadyToGenerate = true;
  }

  // make sure a destructible-change listener exists so destroyed walls dirty the navmesh
  EnsureDestructionResponder();

  // keep the avoid-zone cost in sync with the cvar (filters are shared by all live path searches)
  {
    const float fAvoidCost = plMath::Max(1.0f, cvar_NavMeshAvoidZoneCost.GetValue());

    if (fAvoidCost != m_fAppliedAvoidZoneCost)
    {
      m_fAppliedAvoidZoneCost = fAvoidCost;

      for (auto it = m_PathSearchFilters.GetIterator(); it.IsValid(); ++it)
      {
        it.Value().setAreaCost(plAiNavMeshAvoidZoneAreaID, fAvoidCost);
      }
    }
  }

  // finalize finished sector builds (tile add/remove) per navmesh, then re-apply nav blockers:
  // rebuilt tiles come back with plain 'Walkable' flags, so blocked regions must be re-stamped.
  // The changed-sector lists stay valid for the rest of the frame (GetChangedSectorsThisFrame).
  for (auto& nm : m_WorldNavMeshes)
  {
    plDynamicArray<plAiNavMesh::SectorID>& changedSectors = m_LastChangedSectors[nm.Value()];
    nm.Value()->FinalizeSectorUpdates(&changedSectors);
    ApplyNavBlockers(*nm.Value(), changedSectors);
  }

  m_bAnyBlockerDirty = false;

  // free the slots of removed blockers, now that every navmesh had its area cleared
  for (plUInt32 uiPatch = 0; uiPatch < m_FlagPatches.GetCount(); ++uiPatch)
  {
    FlagPatch& patch = m_FlagPatches[uiPatch];

    if (patch.m_bInUse && patch.m_bPendingRemoval)
    {
      patch.m_bInUse = false;
      patch.m_bPendingRemoval = false;
      m_FreeFlagPatches.PushBack(uiPatch);
    }

    if (patch.m_bInUse)
    {
      patch.m_bDirty = false;
      patch.m_ClearBox = patch.m_WorldBox; // the pre-move region has been released on all navmeshes
    }
  }

  if (cvar_NavMeshVisualizeBlockers)
  {
    for (const auto& patch : m_FlagPatches)
    {
      if (!patch.m_bInUse)
        continue;

      const plColor activeColor = (patch.m_Mode == plAiNavObstacleMode::Avoid) ? plColor::Yellow : plColor::Red;
      plDebugRenderer::DrawLineBox(GetWorld(), patch.m_WorldBox, patch.m_bActive ? activeColor : plColor::Grey, plTransform::MakeIdentity());
    }

    for (const auto& carve : m_ObstacleCarves)
    {
      if (!carve.m_bInUse)
        continue;

      const plColor color = (carve.m_Mode == plAiNavObstacleMode::Avoid) ? plColor::Yellow : plColor::Orange;
      plDebugRenderer::DrawLineBox(GetWorld(), carve.m_WorldBox, color, plTransform::MakeIdentity());
    }
  }

  if (cvar_NavMeshVisualizeLinks)
  {
    plDynamicArray<plDebugRenderer::Line> linkLines;

    for (const auto& link : m_NavLinks)
    {
      if (!link.m_bInUse)
        continue;

      plColor color = plColor::White;
      switch (link.m_Data.m_Type)
      {
        case plAiNavLinkType::Jump:   color = plColor::Orange; break;
        case plAiNavLinkType::Vault:  color = plColor::Yellow; break;
        case plAiNavLinkType::Ladder: color = plColor::CornflowerBlue; break;
        case plAiNavLinkType::Drop:   color = plColor::OrangeRed; break;
        case plAiNavLinkType::Door:   color = plColor::LawnGreen; break;
        case plAiNavLinkType::Custom: color = plColor::Violet; break;
      }

      // parabolic arc from start to end
      const plVec3 vStart = link.m_Data.m_vStart;
      const plVec3 vEnd = link.m_Data.m_vEnd;
      const float fArcHeight = plMath::Clamp((vEnd - vStart).GetLength() * 0.3f, 0.3f, 1.2f);

      plVec3 vPrev = vStart;
      constexpr plUInt32 uiSegments = 8;

      for (plUInt32 i = 1; i <= uiSegments; ++i)
      {
        const float t = static_cast<float>(i) / uiSegments;
        plVec3 vPoint = plMath::Lerp(vStart, vEnd, t);
        vPoint.z += 4.0f * fArcHeight * t * (1.0f - t);

        auto& line = linkLines.ExpandAndGetRef();
        line.m_start = vPrev;
        line.m_end = vPoint;
        line.m_startColor = line.m_endColor = color;
        vPrev = vPoint;
      }
    }

    if (!linkLines.IsEmpty())
    {
      plDebugRenderer::DrawLines(GetWorld(), linkLines, plColor::White);
    }
  }

  if (cvar_NavMeshVisualize >= 0)
  {
    plInt32 i = cvar_NavMeshVisualize;
    for (auto it = m_WorldNavMeshes.GetIterator(); it.IsValid(); ++it)
    {
      if (i-- == 0)
      {
        it.Value()->DebugDraw(GetWorld(), m_Config);
        break;
      }
    }
  }

  // reclaim finished build slots
  plUInt32 uiInFlight = 0;

  for (auto& slot : m_BuildSlots)
  {
    if (slot.m_bInFlight && plTaskSystem::IsTaskGroupFinished(slot.m_TaskID))
    {
      slot.m_bInFlight = false;
    }

    if (slot.m_bInFlight)
    {
      ++uiInFlight;
    }
  }

  const plUInt32 uiMaxBuilds = static_cast<plUInt32>(plMath::Clamp<plInt32>(cvar_NavMeshMaxConcurrentBuilds, 1, 16));

  // dispatch new builds, round-robin across the navmeshes so no navmesh starves the others
  bool bAnyDispatched = true;

  while (bAnyDispatched && uiInFlight < uiMaxBuilds)
  {
    bAnyDispatched = false;

    for (auto& nm : m_WorldNavMeshes)
    {
      if (uiInFlight >= uiMaxBuilds)
        break;

      plAiNavMesh* pNavMesh = nm.Value();

      const plAiNavMesh::SectorID sectorID = pNavMesh->RetrieveRequestedSector();
      if (sectorID == plInvalidIndex)
        continue;

      if (IsSectorBuildInFlight(pNavMesh, sectorID))
      {
        // this sector is being built right now (e.g. it was invalidated again mid-build) -
        // defer it, so the re-request is honored once the current build finished
        pNavMesh->RequeueSector(sectorID);
        continue;
      }

      // find or create a free slot
      BuildSlot* pSlot = nullptr;

      for (auto& slot : m_BuildSlots)
      {
        if (!slot.m_bInFlight)
        {
          pSlot = &slot;
          break;
        }
      }

      if (pSlot == nullptr)
      {
        pSlot = &m_BuildSlots.ExpandAndGetRef();
        pSlot->m_pTask = PL_DEFAULT_NEW(plNavMeshSectorGenerationTask);
        pSlot->m_pTask->ConfigureTask("Generate Navmesh Sector", plTaskNesting::Maybe);
      }

      pSlot->m_pTask->m_pWorldNavMesh = pNavMesh;
      pSlot->m_pTask->m_SectorID = sectorID;
      pSlot->m_pTask->m_pNavGeo = pNavGeo;

      // snapshot the nav links, obstacle boxes and blocker zones for this sector
      // (main thread; tasks never touch the registries)
      pSlot->m_pTask->m_Links.Clear();
      CollectNavLinksForSector(*pNavMesh, sectorID, pSlot->m_pTask->m_Links);
      pSlot->m_pTask->m_CarveBoxes.Clear();
      pSlot->m_pTask->m_AvoidZones.Clear();
      CollectObstacleCarvesForSector(*pNavMesh, sectorID, pSlot->m_pTask->m_CarveBoxes, pSlot->m_pTask->m_AvoidZones);
      pSlot->m_pTask->m_BlockerZones.Clear();
      CollectBlockerZonesForSector(*pNavMesh, sectorID, pSlot->m_pTask->m_BlockerZones);

      pSlot->m_TaskID = plTaskSystem::StartSingleTask(pSlot->m_pTask, plTaskPriority::LongRunning);
      pSlot->m_bInFlight = true;

      ++uiInFlight;
      bAnyDispatched = true;
    }
  }

  if (cvar_NavMeshShowBuildStats)
  {
    plStringBuilder sStats;
    sStats.AppendFormat("Navmesh builds: {} in flight (max {})\n", uiInFlight, uiMaxBuilds);

    for (auto it = m_WorldNavMeshes.GetIterator(); it.IsValid(); ++it)
    {
      sStats.AppendFormat("'{}': {} queued, generation {}\n", it.Key(), it.Value()->GetNumRequestedSectors(), it.Value()->GetNavMeshGeneration());
    }

    plDebugRenderer::DrawInfoText(GetWorld(), plDebugTextPlacement::TopLeft, "AiNavmeshStats", sStats);
  }
}

plAiNavMeshWorldModule::BlockerID plAiNavMeshWorldModule::RegisterNavBlocker(const plBoundingBox& worldBox, bool bActive, plEnum<plAiNavObstacleMode> mode)
{
  BlockerID id = InvalidBlockerID;

  if (!m_FreeFlagPatches.IsEmpty())
  {
    id = m_FreeFlagPatches.PeekBack();
    m_FreeFlagPatches.PopBack();
  }
  else
  {
    id = m_FlagPatches.GetCount();
    m_FlagPatches.ExpandAndGetRef();
  }

  FlagPatch& patch = m_FlagPatches[id];
  patch.m_WorldBox = worldBox;
  patch.m_ClearBox = worldBox;
  patch.m_Mode = mode;
  patch.m_bInUse = true;
  patch.m_bActive = bActive;
  patch.m_bDirty = true;
  patch.m_bPendingRemoval = false;

  m_bAnyBlockerDirty = true;

  // rebuild the overlapping sectors so the build splits dedicated polygons out of this volume -
  // the flag stamp only patches polys centered inside the box, which requires that split
  InvalidateSectorsForBox(worldBox);

  return id;
}

void plAiNavMeshWorldModule::SetNavBlockerActive(BlockerID id, bool bActive)
{
  if (id >= m_FlagPatches.GetCount() || !m_FlagPatches[id].m_bInUse)
    return;

  FlagPatch& patch = m_FlagPatches[id];

  if (patch.m_bActive != bActive)
  {
    patch.m_bActive = bActive;
    patch.m_bDirty = true;
    m_bAnyBlockerDirty = true;
  }
}

void plAiNavMeshWorldModule::UpdateNavBlockerBounds(BlockerID id, const plBoundingBox& worldBox)
{
  if (id >= m_FlagPatches.GetCount() || !m_FlagPatches[id].m_bInUse)
    return;

  FlagPatch& patch = m_FlagPatches[id];

  // the clear pass must cover both the old and the new position, so the old area gets released
  patch.m_ClearBox.ExpandToInclude(worldBox);
  patch.m_WorldBox = worldBox;
  patch.m_bDirty = true;
  m_bAnyBlockerDirty = true;

  // re-split the polys at the old AND new position (the clear box covers both)
  InvalidateSectorsForBox(patch.m_ClearBox);
}

void plAiNavMeshWorldModule::UnregisterNavBlocker(BlockerID id)
{
  if (id >= m_FlagPatches.GetCount() || !m_FlagPatches[id].m_bInUse)
    return;

  FlagPatch& patch = m_FlagPatches[id];
  patch.m_bActive = false;
  patch.m_bDirty = true;
  patch.m_bPendingRemoval = true; // area cleared during the next update, then the slot is freed
  m_bAnyBlockerDirty = true;

  // rebuild without this volume, so the poly split heals
  InvalidateSectorsForBox(patch.m_WorldBox);
}

void plAiNavMeshWorldModule::ApplyNavBlockers(plAiNavMesh& ref_navMesh, plArrayPtr<const plAiNavMesh::SectorID> changedSectors)
{
  if (m_FlagPatches.IsEmpty())
    return;

  // collect which patches need (re)application on this navmesh:
  // - patches marked dirty (toggled, moved, added, removed)
  // - active patches intersecting a sector whose tile was just swapped (fresh tiles lose the flags)
  plHybridArray<plUInt32, 32> patchesToApply;

  for (plUInt32 uiPatch = 0; uiPatch < m_FlagPatches.GetCount(); ++uiPatch)
  {
    const FlagPatch& patch = m_FlagPatches[uiPatch];

    if (!patch.m_bInUse)
      continue;

    bool bApply = patch.m_bDirty;

    if (!bApply && patch.m_bActive)
    {
      for (plAiNavMesh::SectorID sectorID : changedSectors)
      {
        const plBoundingBox sectorBounds = ref_navMesh.GetSectorBounds(ref_navMesh.CalculateSectorCoord(sectorID), -10000.0f, 10000.0f);

        if (sectorBounds.Overlaps(patch.m_WorldBox))
        {
          bApply = true;
          break;
        }
      }
    }

    if (bApply)
    {
      patchesToApply.PushBack(uiPatch);
    }
  }

  if (patchesToApply.IsEmpty())
    return;

  // expand: any active patch overlapping a patch in the set must be re-stamped as well,
  // otherwise clearing one region wipes the overlap of its neighbor
  bool bGrew = true;

  while (bGrew)
  {
    bGrew = false;

    for (plUInt32 uiPatch = 0; uiPatch < m_FlagPatches.GetCount(); ++uiPatch)
    {
      const FlagPatch& patch = m_FlagPatches[uiPatch];

      if (!patch.m_bInUse || !patch.m_bActive || patchesToApply.Contains(uiPatch))
        continue;

      for (plUInt32 uiOther : patchesToApply)
      {
        if (patch.m_WorldBox.Overlaps(m_FlagPatches[uiOther].m_ClearBox))
        {
          patchesToApply.PushBack(uiPatch);
          bGrew = true;
          break;
        }
      }
    }
  }

  // pass 1: reset all affected regions (incl. the pre-move area of moved patches):
  // clear the Blocked flag and restore avoid-zone polys to the default ground area.
  // (blocker zone polys are always baked as default ground, so area 1 is the correct baseline)
  for (plUInt32 uiPatch : patchesToApply)
  {
    const FlagPatch& patch = m_FlagPatches[uiPatch];
    const plUInt8 uiResetArea = (patch.m_Mode == plAiNavObstacleMode::Avoid) ? 1 : 0xFF;

    ref_navMesh.ApplyFlagPatch(patch.m_ClearBox, 0, plAiNavMeshPolyFlags::Blocked, uiResetArea);
  }

  // pass 2: re-stamp all ACTIVE patches in the set
  for (plUInt32 uiPatch : patchesToApply)
  {
    const FlagPatch& patch = m_FlagPatches[uiPatch];

    if (!patch.m_bActive)
      continue;

    if (patch.m_Mode == plAiNavObstacleMode::Avoid)
    {
      // soft avoidance: swap the polys into the high-cost area, leave them walkable
      ref_navMesh.ApplyFlagPatch(patch.m_WorldBox, 0, 0, plAiNavMeshAvoidZoneAreaID);
    }
    else
    {
      ref_navMesh.ApplyFlagPatch(patch.m_WorldBox, plAiNavMeshPolyFlags::Blocked, 0);
    }
  }

  // corridors crossing a toggled region must re-validate against the filters
  ref_navMesh.BumpNavMeshGeneration();
}

void plAiNavMeshWorldModule::EnsureDestructionResponder()
{
  if (!m_hDestructionResponder.IsInvalidated() || m_WorldNavMeshes.IsEmpty())
    return;

  plGameObjectDesc gd;
  gd.m_sName.Assign("AiDestructionResponder");
  gd.m_bDynamic = true;

  plGameObject* pObject = nullptr;
  m_hDestructionResponder = GetWorld()->CreateObject(gd, pObject);

  plAiDestructionResponseComponent* pComponent = nullptr;
  plAiDestructionResponseComponent::CreateComponent(pObject, pComponent);
}

void plAiNavMeshWorldModule::InvalidateSectorsForBox(const plBoundingBox& worldBox)
{
  for (auto& nm : m_WorldNavMeshes)
  {
    nm.Value()->InvalidateSector(worldBox.GetCenter().GetAsVec2(), worldBox.GetHalfExtents().GetAsVec2(), true);
  }
}

void plAiNavMeshWorldModule::InvalidateSectorsAtLinkEndpoints(const plAiNavLinkData& linkData)
{
  for (auto& nm : m_WorldNavMeshes)
  {
    plAiNavMesh* pNavMesh = nm.Value();

    pNavMesh->InvalidateSector(linkData.m_vStart.GetAsVec2(), plVec2(linkData.m_fRadius), true);
    pNavMesh->InvalidateSector(linkData.m_vEnd.GetAsVec2(), plVec2(linkData.m_fRadius), true);
  }
}

plAiNavMeshWorldModule::NavLinkID plAiNavMeshWorldModule::RegisterNavLink(const plAiNavLinkData& linkData, const plComponentHandle& hComponent)
{
  NavLinkID id = InvalidNavLinkID;

  if (!m_FreeNavLinks.IsEmpty())
  {
    id = m_FreeNavLinks.PeekBack();
    m_FreeNavLinks.PopBack();
  }
  else
  {
    id = m_NavLinks.GetCount();
    m_NavLinks.ExpandAndGetRef();
  }

  NavLink& link = m_NavLinks[id];
  link.m_Data = linkData;
  link.m_Data.m_uiUserID = id; // resolves back to this registry entry at traversal time
  link.m_hComponent = hComponent;
  link.m_bInUse = true;

  InvalidateSectorsAtLinkEndpoints(link.m_Data);
  return id;
}

void plAiNavMeshWorldModule::UnregisterNavLink(NavLinkID id)
{
  if (id >= m_NavLinks.GetCount() || !m_NavLinks[id].m_bInUse)
    return;

  NavLink& link = m_NavLinks[id];
  link.m_bInUse = false;

  InvalidateSectorsAtLinkEndpoints(link.m_Data); // rebake without this link
  m_FreeNavLinks.PushBack(id);
}

plComponentHandle plAiNavMeshWorldModule::ResolveNavLink(plUInt32 uiUserID) const
{
  if (uiUserID < m_NavLinks.GetCount() && m_NavLinks[uiUserID].m_bInUse)
    return m_NavLinks[uiUserID].m_hComponent;

  return plComponentHandle();
}

plEnum<plAiNavLinkType> plAiNavMeshWorldModule::GetNavLinkType(plUInt32 uiUserID) const
{
  if (uiUserID < m_NavLinks.GetCount() && m_NavLinks[uiUserID].m_bInUse)
    return m_NavLinks[uiUserID].m_Data.m_Type;

  return plAiNavLinkType::Jump;
}

void plAiNavMeshWorldModule::CollectNavLinksForSector(const plAiNavMesh& navMesh, plAiNavMesh::SectorID sectorID, plDynamicArray<plAiNavLinkData>& out_links) const
{
  if (m_NavLinks.IsEmpty())
    return;

  // Detour stores an off-mesh connection in the tile that owns its START vertex;
  // connectExtOffMeshLinks stitches the end into neighboring tiles automatically
  const plBoundingBox sectorBounds = navMesh.GetSectorBounds(navMesh.CalculateSectorCoord(sectorID));

  for (const NavLink& link : m_NavLinks)
  {
    if (!link.m_bInUse)
      continue;

    const plVec2 vStart = link.m_Data.m_vStart.GetAsVec2();

    if (vStart.x >= sectorBounds.m_vMin.x && vStart.x < sectorBounds.m_vMax.x &&
        vStart.y >= sectorBounds.m_vMin.y && vStart.y < sectorBounds.m_vMax.y)
    {
      out_links.PushBack(link.m_Data);
    }
  }
}

plArrayPtr<const plAiNavMesh::SectorID> plAiNavMeshWorldModule::GetChangedSectorsThisFrame(const plAiNavMesh* pNavMesh) const
{
  PL_ASSERT_DEV(m_uiChangedSectorsUpdateCounter == GetWorld()->GetUpdateCounter(),
    "GetChangedSectorsThisFrame is only valid after the navmesh module updated this frame - order your update function after 'plAiNavMeshWorldModule::Update' via m_DependsOn.");

  auto it = m_LastChangedSectors.Find(pNavMesh);

  if (it.IsValid())
    return it.Value().GetArrayPtr();

  return {};
}

plAiNavMeshWorldModule::ObstacleCarveID plAiNavMeshWorldModule::RegisterObstacleCarve(const plBoundingBox& worldBox, plEnum<plAiNavObstacleMode> mode)
{
  ObstacleCarveID id = InvalidObstacleCarveID;

  if (!m_FreeObstacleCarves.IsEmpty())
  {
    id = m_FreeObstacleCarves.PeekBack();
    m_FreeObstacleCarves.PopBack();
  }
  else
  {
    id = m_ObstacleCarves.GetCount();
    m_ObstacleCarves.ExpandAndGetRef();
  }

  ObstacleCarve& carve = m_ObstacleCarves[id];
  carve.m_WorldBox = worldBox;
  carve.m_Mode = mode;
  carve.m_bInUse = true;

  return id;
}

void plAiNavMeshWorldModule::UpdateObstacleCarve(ObstacleCarveID id, const plBoundingBox& worldBox)
{
  if (id >= m_ObstacleCarves.GetCount() || !m_ObstacleCarves[id].m_bInUse)
    return;

  m_ObstacleCarves[id].m_WorldBox = worldBox;
}

void plAiNavMeshWorldModule::UnregisterObstacleCarve(ObstacleCarveID id)
{
  if (id >= m_ObstacleCarves.GetCount() || !m_ObstacleCarves[id].m_bInUse)
    return;

  m_ObstacleCarves[id].m_bInUse = false;
  m_FreeObstacleCarves.PushBack(id);
}

void plAiNavMeshWorldModule::CollectBlockerZonesForSector(const plAiNavMesh& navMesh, plAiNavMesh::SectorID sectorID, plDynamicArray<plBoundingBox>& out_boxes) const
{
  if (m_FlagPatches.IsEmpty())
    return;

  // 2m margin covers the tile's rasterization border, so splits stay consistent across seams
  plBoundingBox sectorBounds = navMesh.GetSectorBounds(navMesh.CalculateSectorCoord(sectorID), -10000.0f, 10000.0f);
  sectorBounds.Grow(plVec3(2.0f, 2.0f, 0.0f));

  // include INACTIVE blockers: the split must already exist when they get toggled on,
  // otherwise the first toggle wouldn't be instant
  for (const FlagPatch& patch : m_FlagPatches)
  {
    if (patch.m_bInUse && !patch.m_bPendingRemoval && sectorBounds.Overlaps(patch.m_WorldBox))
    {
      out_boxes.PushBack(patch.m_WorldBox);
    }
  }
}

void plAiNavMeshWorldModule::CollectObstacleCarvesForSector(const plAiNavMesh& navMesh, plAiNavMesh::SectorID sectorID, plDynamicArray<plBoundingBox>& out_carveBoxes, plDynamicArray<plBoundingBox>& out_avoidZones) const
{
  if (m_ObstacleCarves.IsEmpty())
    return;

  // include the tile's rasterization border: a carve just outside the sector still erodes into it,
  // the same way static geometry in the border does (2m safely covers agent radius + 3 cells)
  plBoundingBox sectorBounds = navMesh.GetSectorBounds(navMesh.CalculateSectorCoord(sectorID), -10000.0f, 10000.0f);
  sectorBounds.Grow(plVec3(2.0f, 2.0f, 0.0f));

  for (const ObstacleCarve& carve : m_ObstacleCarves)
  {
    if (carve.m_bInUse && sectorBounds.Overlaps(carve.m_WorldBox))
    {
      if (carve.m_Mode == plAiNavObstacleMode::Avoid)
      {
        out_avoidZones.PushBack(carve.m_WorldBox);
      }
      else
      {
        out_carveBoxes.PushBack(carve.m_WorldBox);
      }
    }
  }
}

const dtQueryFilter& plAiNavMeshWorldModule::GetPathSearchFilter(plStringView sName) const
{
  auto it = m_PathSearchFilters.Find(sName);
  if (it.IsValid())
    return it.Value();

  it = m_PathSearchFilters.Find("");
  plLog::Warning("Ai Path Search Filter '{}' does not exist.", sName);
  return it.Value();
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Implementation_NavMeshWorldModule);
