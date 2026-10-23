#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Utils/RcMath.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/FileSystem/FileWriter.h>
#include <RendererCore/Debug/DebugRenderer.h>

void DrawMeshTilePolygons(const dtMeshTile& meshTile, plDynamicArray<plDebugRenderer::Triangle>& out_triangles, plArrayPtr<plColor> areaColors);
void DrawMeshTileEdges(const dtMeshTile& meshTile, bool bOuterEdges, bool bInnerEdges, bool bInnerDetailEdges, plDynamicArray<plDebugRenderer::Line>& out_lines);

plAiNavMeshSector::plAiNavMeshSector()
{
  m_FlagRequested = 0;
  m_FlagInvalidate = 0;
  m_FlagBuilding = 0;
  m_FlagUsable = 0;
  m_FlagDirtyDuringBuild = 0;
  m_FlagLastBuildFailed = 0;
}

plAiNavMeshSector::~plAiNavMeshSector() = default;

plAiNavMesh::plAiNavMesh(const plAiNavmeshConfig& navmeshConfig)
{
  m_uiNumSectorsX = navmeshConfig.m_uiNumSectorsX;
  m_uiNumSectorsY = navmeshConfig.m_uiNumSectorsY;
  m_fSectorMetersXY = navmeshConfig.m_fSectorSize;
  m_fInvSectorMetersXY = 1.0f / navmeshConfig.m_fSectorSize;
  m_NavmeshConfig = navmeshConfig;

  m_pNavMesh = PL_DEFAULT_NEW(dtNavMesh);

  dtNavMeshParams np;
  np.tileWidth = navmeshConfig.m_fSectorSize;
  np.tileHeight = navmeshConfig.m_fSectorSize;
  np.orig[0] = -(navmeshConfig.m_uiNumSectorsX * 0.5f) * navmeshConfig.m_fSectorSize;
  np.orig[1] = 0.0f;
  np.orig[2] = -(navmeshConfig.m_uiNumSectorsY * 0.5f) * navmeshConfig.m_fSectorSize;
  np.maxTiles = navmeshConfig.m_uiNumSectorsX * navmeshConfig.m_uiNumSectorsY;
  np.maxPolys = 1 << 16;

  // fails when sector count and polygons per sector don't fit the poly reference bits (DT_POLYREF64)
  m_bValid = dtStatusSucceed(m_pNavMesh->init(&np));

  if (!m_bValid)
  {
    plLog::Error("Navmesh '{}' could not be created: {} x {} sectors with up to {} polygons each don't fit the navmesh polygon references.",
      navmeshConfig.m_sName, navmeshConfig.m_uiNumSectorsX, navmeshConfig.m_uiNumSectorsY, np.maxPolys);
  }
}

plAiNavMesh::~plAiNavMesh()
{
  PL_DEFAULT_DELETE(m_pNavMesh);
}

plVec2I32 plAiNavMesh::CalculateSectorCoord(float fPositionX, float fPositionY) const
{
  fPositionX *= m_fInvSectorMetersXY;
  fPositionY *= m_fInvSectorMetersXY;

  fPositionX += m_uiNumSectorsX * 0.5f;
  fPositionY += m_uiNumSectorsY * 0.5f;

  return plVec2I32((plInt32)plMath::Floor(fPositionX), (plInt32)plMath::Floor(fPositionY));
}

plVec2I32 plAiNavMesh::CalculateSectorCoord(SectorID sectorID) const
{
  PL_ASSERT_DEBUG(sectorID < m_uiNumSectorsX * m_uiNumSectorsY, "Invalid navmesh sector ID");

  return plVec2I32(sectorID % m_uiNumSectorsX, sectorID / m_uiNumSectorsX);
}

const plAiNavMeshSector* plAiNavMesh::GetSector(SectorID sectorID) const
{
  auto it = m_Sectors.Find(sectorID);
  if (it.IsValid())
    return &it.Value();

  return nullptr;
}

bool plAiNavMesh::RequestSector(SectorID sectorID)
{
  auto& sector = m_Sectors.FindOrAdd(sectorID).Value();

  if (sector.m_FlagUsable == 0)
  {
    if (sector.m_FlagRequested == 0)
    {
      sector.m_FlagRequested = 1;
      m_RequestedSectors.PushBack(sectorID);
    }

    return false;
  }

  return true;
}

bool plAiNavMesh::RequestSector(const plVec2& vCenter, const plVec2& vHalfExtents)
{
  plVec2I32 coordMin = CalculateSectorCoord(vCenter.x - vHalfExtents.x, vCenter.y - vHalfExtents.y);
  plVec2I32 coordMax = CalculateSectorCoord(vCenter.x + vHalfExtents.x, vCenter.y + vHalfExtents.y);

  coordMin.x = plMath::Clamp<plInt32>(coordMin.x, 0, m_uiNumSectorsX - 1);
  coordMax.x = plMath::Clamp<plInt32>(coordMax.x, 0, m_uiNumSectorsX - 1);
  coordMin.y = plMath::Clamp<plInt32>(coordMin.y, 0, m_uiNumSectorsY - 1);
  coordMax.y = plMath::Clamp<plInt32>(coordMax.y, 0, m_uiNumSectorsY - 1);

  bool res = true;

  for (plInt32 y = coordMin.y; y <= coordMax.y; ++y)
  {
    for (plInt32 x = coordMin.x; x <= coordMax.x; ++x)
    {
      if (!RequestSector(CalculateSectorID(plVec2I32(x, y))))
      {
        res = false;
      }
    }
  }

  return res;
}

void plAiNavMesh::InvalidateSector(const plVec2& vCenter, const plVec2& vHalfExtents, bool bRebuildAsSoonAsPossible, plAiNavMeshRebuildReason reason)
{
  plVec2I32 coordMin = CalculateSectorCoord(vCenter.x - vHalfExtents.x, vCenter.y - vHalfExtents.y);
  plVec2I32 coordMax = CalculateSectorCoord(vCenter.x + vHalfExtents.x, vCenter.y + vHalfExtents.y);

  coordMin.x = plMath::Clamp<plInt32>(coordMin.x, 0, m_uiNumSectorsX - 1);
  coordMax.x = plMath::Clamp<plInt32>(coordMax.x, 0, m_uiNumSectorsX - 1);
  coordMin.y = plMath::Clamp<plInt32>(coordMin.y, 0, m_uiNumSectorsY - 1);
  coordMax.y = plMath::Clamp<plInt32>(coordMax.y, 0, m_uiNumSectorsY - 1);

  for (plInt32 y = coordMin.y; y <= coordMax.y; ++y)
  {
    for (plInt32 x = coordMin.x; x <= coordMax.x; ++x)
    {
      InvalidateSector(CalculateSectorID(plVec2I32(x, y)), bRebuildAsSoonAsPossible, reason);
    }
  }
}

void plAiNavMesh::InvalidateSector(SectorID sectorID, bool bRebuildAsSoonAsPossible, plAiNavMeshRebuildReason reason)
{
  auto it = m_Sectors.Find(sectorID);
  if (!it.IsValid())
    return;

  auto& sector = it.Value();

  // the running build may have sampled the geometry before this change
  if (sector.m_FlagBuilding == 1)
  {
    sector.m_FlagDirtyDuringBuild = 1;
    sector.m_PendingReason = reason;
    return;
  }

  // a sector that is only queued will sample the current geometry anyway
  if (sector.m_FlagInvalidate == 1 || sector.m_FlagUsable == 0)
    return;

  if (bRebuildAsSoonAsPossible)
  {
    sector.m_FlagInvalidate = 1;
    sector.m_PendingReason = reason;
    m_RequestedSectors.PushBack(sectorID);
  }
  else
  {
    sector.m_FlagRequested = 0;
    m_UnloadingSectors.PushBack(sectorID);
  }
}

plAiNavMeshSector& plAiNavMesh::BeginSectorBuild(SectorID sectorID)
{
  plAiNavMeshSector& sector = m_Sectors.FindOrAdd(sectorID).Value();
  sector.m_FlagBuilding = 1;
  sector.m_FlagDirtyDuringBuild = 0;
  sector.m_BuildReason = sector.m_PendingReason;
  return sector;
}

void plAiNavMesh::GetSectorStatus(plDynamicArray<plAiNavMeshSectorStatus>& out_status) const
{
  out_status.Clear();

  for (auto it = m_Sectors.GetIterator(); it.IsValid(); ++it)
  {
    const plAiNavMeshSector& sector = it.Value();

    if (sector.m_FlagRequested == 0 && sector.m_FlagUsable == 0 && sector.m_FlagBuilding == 0)
      continue; // never requested, or unloaded

    plAiNavMeshSectorStatus& status = out_status.ExpandAndGetRef();
    status.m_uiSectorID = it.Key();
    status.m_vCoord = CalculateSectorCoord(it.Key());
    status.m_LastReason = sector.m_LastReason;
    status.m_uiPolyCount = sector.m_uiPolyCount;
    status.m_uiBuildCount = sector.m_uiBuildCount;
    status.m_LastBuildDuration = sector.m_LastBuildDuration;

    if (sector.m_FlagUsable == 1)
    {
      const bool bRebuildPending = sector.m_FlagBuilding == 1 || sector.m_FlagInvalidate == 1;
      status.m_State = bRebuildPending ? plAiNavMeshSectorState::Rebuilding : (sector.m_FlagLastBuildFailed ? plAiNavMeshSectorState::Failed : plAiNavMeshSectorState::Ready);
    }
    else
    {
      status.m_State = (sector.m_FlagBuilding == 1) ? plAiNavMeshSectorState::Building : plAiNavMeshSectorState::Queued;
    }
  }
}

void plAiNavMesh::FinalizeSectorUpdates(plDynamicArray<SectorID>* out_pChangedSectors /*= nullptr*/)
{
  PL_LOCK(m_Mutex);

  const bool bAnyChange = !m_UpdatingSectors.IsEmpty() || !m_UnloadingSectors.IsEmpty();

  for (auto sectorID : m_UpdatingSectors)
  {
    const auto coord = CalculateSectorCoord(sectorID);

    auto& sector = m_Sectors[sectorID];

    PL_ASSERT_DEV(sector.m_FlagBuilding == 1, "Invalid sector update state");

    if (out_pChangedSectors != nullptr)
    {
      out_pChangedSectors->PushBack(sectorID);
    }

    if (!sector.m_NavmeshDataCur.IsEmpty())
    {
      sector.m_FlagUsable = 0;

      const auto res = m_pNavMesh->removeTile(sector.m_TileRef, nullptr, nullptr);

      if (res != DT_SUCCESS)
      {
        plLog::Error("NavMesh removeTile error: {}", res);
      }
    }

    sector.m_NavmeshDataCur.Swap(sector.m_NavmeshDataNew);
    sector.m_NavmeshDataNew.Clear();
    sector.m_NavmeshDataNew.Compact();

    if (!sector.m_NavmeshDataCur.IsEmpty())
    {
      auto res = m_pNavMesh->addTile(sector.m_NavmeshDataCur.GetData(), (int)sector.m_NavmeshDataCur.GetCount(), 0, 0, &sector.m_TileRef);

      if (res == DT_SUCCESS)
      {
        plLog::Success("Loaded navmesh tile {}|{}", coord.x, coord.y);
        sector.m_FlagUsable = 1;
      }
      else
      {
        plLog::Error("NavMesh addTile error: {}", res);
      }
    }
    else
    {
      plLog::Success("Loaded empty navmesh tile {}|{}", coord.x, coord.y);
      sector.m_FlagUsable = 1;
    }

    sector.m_FlagInvalidate = 0;
    sector.m_FlagBuilding = 0;
    // sector.m_FlagRequested = 0; // do not reset the requested flag

    sector.m_uiPolyCount = sector.m_uiBuildPolyCount;
    sector.m_LastBuildDuration = sector.m_BuildDuration;
    sector.m_FlagLastBuildFailed = sector.m_bBuildFailed ? 1 : 0;
    sector.m_LastReason = sector.m_BuildReason;
    ++sector.m_uiBuildCount;

    if (sector.m_FlagDirtyDuringBuild == 1)
    {
      // invalidated while building: rebuild with the current geometry (the reason was set by InvalidateSector)
      sector.m_FlagDirtyDuringBuild = 0;
      sector.m_FlagInvalidate = 1;
      m_RequestedSectors.PushBack(sectorID);
    }
  }

  m_UpdatingSectors.Clear();

  for (auto sectorID : m_UnloadingSectors)
  {
    auto& sector = m_Sectors[sectorID];

    // Sector has been requested since then, don't unload it.
    if (sector.m_FlagRequested == 1)
      continue;

    if (out_pChangedSectors != nullptr)
    {
      out_pChangedSectors->PushBack(sectorID);
    }

    if (!sector.m_NavmeshDataCur.IsEmpty())
    {
      const auto res = m_pNavMesh->removeTile(sector.m_TileRef, nullptr, nullptr);

      if (res != DT_SUCCESS)
      {
        plLog::Error("NavMesh removeTile error: {}", res);
      }

      sector.m_NavmeshDataCur.Clear();
      sector.m_NavmeshDataCur.Compact();
    }

    sector.m_FlagRequested = 0;
    sector.m_FlagInvalidate = 0;
    sector.m_FlagUsable = 0;
    sector.m_PendingReason = plAiNavMeshRebuildReason::FirstBuild;
  }

  m_UnloadingSectors.Clear();

  if (bAnyChange)
  {
    // path corridors compare this to detect that their cached polygon references may be stale
    m_uiNavMeshGeneration.Increment();
    DropStalePatchedAreas();
  }
}

void plAiNavMesh::DropStalePatchedAreas()
{
  // polygons of a rebuilt tile carry its fresh bake (patches are stamped again); unloaded ones are gone
  for (auto it = m_PatchedAreas.GetIterator(); it.IsValid();)
  {
    if (!m_pNavMesh->isValidPolyRef(it.Key()))
      it = m_PatchedAreas.Remove(it);
    else
      ++it;
  }
}

plAiNavMesh::SectorID plAiNavMesh::RetrieveRequestedSector()
{
  plUInt32 uiBestIndex = plInvalidIndex;

  for (plUInt32 i = 0; i < m_RequestedSectors.GetCount(); ++i)
  {
    const plAiNavMeshSector* pSector = GetSector(m_RequestedSectors[i]);

    if (pSector != nullptr && pSector->m_FlagBuilding == 1)
    {
      // the previous build of this sector has not been finalized yet - starting another build
      // now would collide with it; keep it queued and look at the next entry
      continue;
    }

    if (pSector != nullptr && pSector->m_FlagInvalidate == 1 && pSector->m_FlagUsable == 1)
    {
      // rebuilding a tile that is currently in use beats first-time builds:
      // agents may be standing on / pathing through outdated data
      uiBestIndex = i;
      break;
    }

    if (uiBestIndex == plInvalidIndex)
    {
      uiBestIndex = i; // first plain entry = FIFO fallback
    }
  }

  if (uiBestIndex == plInvalidIndex)
    return plInvalidIndex;

  const SectorID id = m_RequestedSectors[uiBestIndex];
  m_RequestedSectors.RemoveAtAndCopy(uiBestIndex);

  return id;
}

void plAiNavMesh::RequeueSector(SectorID sectorID)
{
  m_RequestedSectors.PushBack(sectorID);
}

plUInt32 plAiNavMesh::ApplyFlagPatch(const plBoundingBox& worldBox, plUInt16 uiSetFlags, plUInt16 uiClearFlags, plUInt8 uiSetArea /*= 0xFF*/)
{
  if (!m_bFlagPatchQueryInitialized)
  {
    if (dtStatusFailed(m_FlagPatchQuery.init(m_pNavMesh, 64)))
      return 0;

    m_bFlagPatchQueryInitialized = true;
  }

  // a filter that passes EVERYTHING, so already-blocked polys can be found and un-blocked
  dtQueryFilter permissiveFilter;
  permissiveFilter.setIncludeFlags(0xFFFF);
  permissiveFilter.setExcludeFlags(0);
  permissiveFilter.setIncludeAreaBits(~0ull);

  constexpr plUInt32 uiMaxPolys = 512;
  dtPolyRef polyRefs[uiMaxPolys];
  int iNumPolys = 0;

  const plRcPos center = worldBox.GetCenter();
  const plRcPos halfExtents = worldBox.GetHalfExtents();

  if (dtStatusFailed(m_FlagPatchQuery.queryPolygons(center, halfExtents, &permissiveFilter, polyRefs, &iNumPolys, uiMaxPolys)))
    return 0;

  if (iNumPolys == static_cast<int>(uiMaxPolys))
  {
    plLog::Warning("AI nav blocker volume overlaps more than {} polygons - some may not get patched. Use smaller blocker volumes.", uiMaxPolys);
  }

  // queryPolygons returns every poly whose bounding volume TOUCHES the box - on open ground a
  // single floor poly can span most of a sector, and flagging it would block far more than the
  // box (up to the whole level). Only patch polys whose centroid lies inside the box; sector
  // builds split dedicated polygons out of every blocker volume, so the intended polys pass.
  plBoundingBox containTest = worldBox;
  const float fGrowXY = plMath::Max(2.0f * m_NavmeshConfig.m_fCellSize, 0.2f);
  containTest.Grow(plVec3(fGrowXY, fGrowXY, 0.5f)); // Z margin: poly verts sit at span tops

  plUInt32 uiNumPatched = 0;

  for (int i = 0; i < iNumPolys; ++i)
  {
    const dtMeshTile* pTile = nullptr;
    const dtPoly* pPoly = nullptr;

    if (dtStatusFailed(m_pNavMesh->getTileAndPolyByRef(polyRefs[i], &pTile, &pPoly)))
      continue;

    if (pPoly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
      continue;

    plVec3 vCentroid = plVec3::MakeZero();

    for (plUInt8 v = 0; v < pPoly->vertCount; ++v)
    {
      vCentroid += plVec3(plRcPos(&pTile->verts[pPoly->verts[v] * 3]));
    }

    vCentroid /= static_cast<float>(pPoly->vertCount);

    if (!containTest.Contains(vCentroid))
      continue;

    unsigned short uiFlags = 0;

    if (dtStatusFailed(m_pNavMesh->getPolyFlags(polyRefs[i], &uiFlags)))
      continue;

    m_pNavMesh->setPolyFlags(polyRefs[i], (uiFlags | uiSetFlags) & ~uiClearFlags);

    if (uiSetArea == RestoreBakedArea)
    {
      plUInt8 uiBaked = 0;
      if (m_PatchedAreas.Remove(polyRefs[i], &uiBaked))
      {
        m_pNavMesh->setPolyArea(polyRefs[i], uiBaked);
      }
    }
    else if (uiSetArea != 0xFF)
    {
      // remember the baked area (a ground type) the first time a patch replaces it
      unsigned char uiCurrent = 0;
      if (!m_PatchedAreas.Contains(polyRefs[i]) && dtStatusSucceed(m_pNavMesh->getPolyArea(polyRefs[i], &uiCurrent)))
      {
        m_PatchedAreas.Insert(polyRefs[i], uiCurrent);
      }

      m_pNavMesh->setPolyArea(polyRefs[i], uiSetArea);
    }

    ++uiNumPatched;
  }

  if (uiSetFlags != 0 || uiClearFlags != 0)
  {
    uiNumPatched += PatchNavLinks(containTest, uiSetFlags, uiClearFlags);
  }

  return uiNumPatched;
}

plUInt32 plAiNavMesh::PatchNavLinks(const plBoundingBox& containTest, plUInt16 uiSetFlags, plUInt16 uiClearFlags)
{
  // the polygon query never returns off-mesh connections: visit the links of the tiles under the box. A blocker on
  // either end of a link closes it (a door in front of a ladder); the link's area stays as it is.
  int iMinX = 0, iMinY = 0, iMaxX = 0, iMaxY = 0;
  const plRcPos vMin = containTest.m_vMin;
  const plRcPos vMax = containTest.m_vMax;
  m_pNavMesh->calcTileLoc(vMin, &iMinX, &iMinY);
  m_pNavMesh->calcTileLoc(vMax, &iMaxX, &iMaxY);

  plUInt32 uiNumPatched = 0;
  constexpr int iMaxTilesPerCell = 4;
  const dtMeshTile* tiles[iMaxTilesPerCell];

  for (int y = plMath::Min(iMinY, iMaxY); y <= plMath::Max(iMinY, iMaxY); ++y)
  {
    for (int x = plMath::Min(iMinX, iMaxX); x <= plMath::Max(iMinX, iMaxX); ++x)
    {
      const int iNumTiles = m_pNavMesh->getTilesAt(x, y, tiles, iMaxTilesPerCell);

      for (int t = 0; t < iNumTiles; ++t)
      {
        const dtMeshTile* pTile = tiles[t];
        const dtPolyRef base = m_pNavMesh->getPolyRefBase(pTile);

        for (int c = 0; c < pTile->header->offMeshConCount; ++c)
        {
          const dtOffMeshConnection& link = pTile->offMeshCons[c];
          const plVec3 vStart = plVec3(plRcPos(&link.pos[0]));
          const plVec3 vEnd = plVec3(plRcPos(&link.pos[3]));

          if (!containTest.Contains(vStart) && !containTest.Contains(vEnd))
            continue;

          const dtPolyRef linkRef = base | static_cast<dtPolyRef>(link.poly);

          unsigned short uiFlags = 0;
          if (dtStatusFailed(m_pNavMesh->getPolyFlags(linkRef, &uiFlags)))
            continue;

          m_pNavMesh->setPolyFlags(linkRef, (uiFlags | uiSetFlags) & ~uiClearFlags);
          ++uiNumPatched;
        }
      }
    }
  }

  return uiNumPatched;
}

plVec2 plAiNavMesh::GetSectorPositionOffset(plVec2I32 vCoord) const
{
  return plVec2((vCoord.x - m_uiNumSectorsX * 0.5f) * m_fSectorMetersXY, (vCoord.y - m_uiNumSectorsY * 0.5f) * m_fSectorMetersXY);
}

plBoundingBox plAiNavMesh::GetSectorBounds(plVec2I32 vCoord, float fMinZ /*= 0.0f*/, float fMaxZ /*= 1.0f*/) const
{
  const plVec3 min = GetSectorPositionOffset(vCoord).GetAsVec3(fMinZ);
  const plVec3 max = min + plVec3(m_fSectorMetersXY, m_fSectorMetersXY, fMaxZ - fMinZ);

  return plBoundingBox::MakeFromMinMax(min, max);
}

void plAiNavMesh::DebugDraw(plDebugRendererContext context, const plAiNavigationConfig& config)
{
  const auto& dtm = *GetDetourNavMesh();

  for (int i = 0; i < dtm.getMaxTiles(); ++i)
  {
    DebugDrawSector(context, config, i);
  }
}

void plAiNavMesh::DebugDrawSector(plDebugRendererContext context, const plAiNavigationConfig& config, int iTileIdx)
{
  const auto& mesh = *GetDetourNavMesh();

  const dtMeshTile* pTile = mesh.getTile(iTileIdx);

  if (pTile == nullptr || pTile->header == nullptr)
    return;

  // full DT_MAX_AREAS range: reserved ids above the ground types (avoid zones) must not read OOB
  plColor areaColors[DT_MAX_AREAS];
  for (plUInt32 i = 0; i < DT_MAX_AREAS; ++i)
  {
    areaColors[i] = (i < plAiNumGroundTypes) ? plColor(config.m_GroundTypes[i].m_Color) : plColor::DimGrey;
  }

  areaColors[plAiNavMeshAvoidZoneAreaID] = plColor::Orange; // soft avoidance zones

  {
    plDynamicArray<plDebugRenderer::Triangle> triangles;
    triangles.Reserve(pTile->header->polyCount * 2);

    DrawMeshTilePolygons(*pTile, triangles, areaColors);
    plDebugRenderer::DrawSolidTriangles(context, triangles, plColor::White);
  }

  {
    plDynamicArray<plDebugRenderer::Line> lines;
    lines.Reserve(pTile->header->polyCount * 10);
    DrawMeshTileEdges(*pTile, true, true, false, lines);
    plDebugRenderer::DrawLines(context, lines, plColor::White);
  }
}
