#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/NavigationConfig.h>
#include <AiPlugin/Tactical/Implementation/CoverGeneration.h>
#include <AiPlugin/Utils/RcMath.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <DetourNavMesh.h>
#include <Foundation/Containers/HashSet.h>
#include <Foundation/Containers/HashTable.h>

void plAiCoverGen::ExtractBoundaryEdges(const dtMeshTile& tile, plDynamicArray<plAiCoverEdge>& out_edges)
{
  if (tile.header == nullptr)
    return;

  for (int polyIdx = 0; polyIdx < tile.header->polyCount; ++polyIdx)
  {
    const dtPoly& poly = tile.polys[polyIdx];

    if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
      continue;

    if (poly.flags & plAiNavMeshPolyFlags::Blocked)
      continue;

    // centroid orients edge normals outward, independent of vertex winding
    plVec3 vCentroid = plVec3::MakeZero();

    for (plUInt8 v = 0; v < poly.vertCount; ++v)
    {
      vCentroid += plVec3(plRcPos(&tile.verts[poly.verts[v] * 3]));
    }

    vCentroid /= static_cast<float>(poly.vertCount);

    for (plUInt8 e = 0; e < poly.vertCount; ++e)
    {
      // 0 = no neighbor at all = outer wall edge; DT_EXT_LINK bits = tile seam (not a wall)
      if (poly.neis[e] != 0)
        continue;

      const plVec3 vStart = plVec3(plRcPos(&tile.verts[poly.verts[e] * 3]));
      const plVec3 vEnd = plVec3(plRcPos(&tile.verts[poly.verts[(e + 1) % poly.vertCount] * 3]));

      plVec3 vDir = vEnd - vStart;
      vDir.z = 0.0f;

      if (vDir.GetLengthSquared() < 0.01f)
        continue;

      plVec3 vOutward(-vDir.y, vDir.x, 0.0f);
      vOutward.Normalize();

      plVec3 vToMid = (vStart + vEnd) * 0.5f - vCentroid;
      vToMid.z = 0.0f;

      if (vOutward.Dot(vToMid) < 0.0f)
      {
        vOutward = -vOutward;
      }

      plAiCoverEdge& edge = out_edges.ExpandAndGetRef();
      edge.m_vStart = vStart;
      edge.m_vEnd = vEnd;
      edge.m_vOutwardDir = vOutward;
    }
  }
}

bool plAiCoverGen::ProbePoint(const plPhysicsWorldModuleInterface& physics, const plVec3& vStandPos, const plVec3& vWallDir, const ProbeConfig& cfg, plAiCoverPoint& out_point)
{
  const plPhysicsQueryParameters params(cfg.m_uiCollisionLayer, plPhysicsShapeType::Static);

  // find the real ground under the sample (on ramps/stairs the caller's Z can be badly off)
  plVec3 vGround = vStandPos;
  {
    constexpr float fGroundProbeUp = 1.0f;
    constexpr float fGroundProbeLen = 3.0f;

    // in low corridors the start may sit above a thin ceiling and snap to the floor above; the
    // crouch probe from that wrong Z then simply fails, same as it would have before the snap
    plPhysicsCastResult groundHit;
    if (physics.Raycast(groundHit, vStandPos + plVec3(0, 0, fGroundProbeUp), plVec3(0, 0, -1), fGroundProbeLen, params))
    {
      vGround.z = groundHit.m_vPosition.z;
    }
  }

  plPhysicsCastResult hit;

  // something must block fire at crouch height, otherwise this is a ledge, not cover
  if (!physics.Raycast(hit, vGround + plVec3(0, 0, cfg.m_fCrouchHeight), vWallDir, cfg.m_fRayDistance, params))
    return false;

  const bool bStandBlocked = physics.Raycast(hit, vGround + plVec3(0, 0, cfg.m_fStandHeight), vWallDir, cfg.m_fRayDistance, params);

  // wall-top ladder: bisect the boundary between the highest blocked and lowest open probe height.
  // Assumes the wall is monotonic between the anchors; a window/gap makes this report the gap
  // height, which is acceptable for a coarse 0.1m-quantized estimate.
  constexpr float fMaxWallTop = 2.25f;

  float fHighestBlocked = bStandBlocked ? cfg.m_fStandHeight : cfg.m_fCrouchHeight;
  float fLowestOpen = bStandBlocked ? fMaxWallTop : cfg.m_fStandHeight;

  if (bStandBlocked && physics.Raycast(hit, vGround + plVec3(0, 0, fMaxWallTop), vWallDir, cfg.m_fRayDistance, params))
  {
    fHighestBlocked = fMaxWallTop; // capped: at least this high
  }

  for (plUInt32 i = 0; i < 3 && (fLowestOpen - fHighestBlocked) > 0.1f; ++i)
  {
    const float fMid = (fLowestOpen + fHighestBlocked) * 0.5f;

    if (physics.Raycast(hit, vGround + plVec3(0, 0, fMid), vWallDir, cfg.m_fRayDistance, params))
    {
      fHighestBlocked = fMid;
    }
    else
    {
      fLowestOpen = fMid;
    }
  }

  const float fWallTop = (fHighestBlocked + fLowestOpen) * 0.5f;

  out_point.m_vPosition = vGround;
  out_point.m_vWallDir = vWallDir;
  out_point.m_Quality = bStandBlocked ? plAiCoverQuality::High : plAiCoverQuality::Low;
  out_point.m_uiClaimIndex = 0xFFFF;
  out_point.m_uiWallTopHeight = static_cast<plUInt8>(plMath::Clamp(plMath::Round(fWallTop * 10.0f), 1.0f, 255.0f));

  return true;
}

namespace
{
  plUInt64 QuantizePos(const plVec3& v)
  {
    const plInt64 x = static_cast<plInt64>(plMath::Round(v.x * 20.0f)); // 5cm grid
    const plInt64 y = static_cast<plInt64>(plMath::Round(v.y * 20.0f));
    const plInt64 z = static_cast<plInt64>(plMath::Round(v.z * 10.0f)); // 10cm vertical
    return (static_cast<plUInt64>(x & 0x1FFFFF) << 42) | (static_cast<plUInt64>(y & 0x1FFFFF) << 21) | static_cast<plUInt64>(z & 0x1FFFFF);
  }

  /// Orders boundary edges into wall contours (edge end -> next edge start), so samples along one
  /// wall are consecutive and can be grouped into cover surfaces. Edges without a successor simply
  /// end their chain; out_chainStart marks the first edge of each chain.
  void BuildEdgeChains(const plDynamicArray<plAiCoverEdge>& edges, plDynamicArray<plUInt32>& out_order, plDynamicArray<bool>& out_chainStart)
  {
    out_order.Clear();
    out_order.Reserve(edges.GetCount());
    out_chainStart.Clear();
    out_chainStart.Reserve(edges.GetCount());

    plHashTable<plUInt64, plUInt32> startLookup; // quantized start pos -> edge index (first wins; T-junctions just break the chain)
    plHashSet<plUInt64> endKeys;

    for (plUInt32 i = 0; i < edges.GetCount(); ++i)
    {
      const plUInt64 uiKey = QuantizePos(edges[i].m_vStart);

      if (!startLookup.Contains(uiKey))
      {
        startLookup[uiKey] = i;
      }

      endKeys.Insert(QuantizePos(edges[i].m_vEnd));
    }

    plDynamicArray<bool> visited;
    visited.SetCount(edges.GetCount(), false);

    auto walkChain = [&](plUInt32 uiFirst) {
      plUInt32 uiEdge = uiFirst;
      bool bFirst = true;

      while (true)
      {
        visited[uiEdge] = true;
        out_order.PushBack(uiEdge);
        out_chainStart.PushBack(bFirst);
        bFirst = false;

        plUInt32 uiNext = plInvalidIndex;

        if (!startLookup.TryGetValue(QuantizePos(edges[uiEdge].m_vEnd), uiNext) || uiNext >= edges.GetCount() || visited[uiNext])
          break;

        uiEdge = uiNext;
      }
    };

    // open contours first (edges no other edge leads into), then the leftover closed loops
    for (plUInt32 i = 0; i < edges.GetCount(); ++i)
    {
      if (!visited[i] && !endKeys.Contains(QuantizePos(edges[i].m_vStart)))
      {
        walkChain(i);
      }
    }

    for (plUInt32 i = 0; i < edges.GetCount(); ++i)
    {
      if (!visited[i])
      {
        walkChain(i);
      }
    }
  }
} // namespace

void plAiCoverBakeTask::Execute()
{
  m_Points.Clear();
  m_Surfaces.Clear();

  if (m_pPhysics == nullptr)
    return;

  plAiCoverGen::ProbeConfig cfg;
  cfg.m_fRayDistance = m_fInset + m_fProbeDistance;
  cfg.m_fCrouchHeight = m_fCrouchHeight;
  cfg.m_fStandHeight = m_fStandHeight;
  cfg.m_uiCollisionLayer = m_uiCollisionLayer;

  // pass 1 (no raycasts): count all samples, so decimation can spread the probe budget evenly
  // across the whole sector instead of exhausting it on whichever polys the tile enumerates first
  plHybridArray<plUInt32, 64> edgeSampleCounts;
  edgeSampleCounts.Reserve(m_Edges.GetCount());

  plUInt64 uiTotalSamples = 0;

  for (const plAiCoverEdge& edge : m_Edges)
  {
    const float fLength = (edge.m_vEnd - edge.m_vStart).GetAsVec2().GetLength();
    const plUInt32 uiSamples = (fLength < 0.1f) ? 0 : plMath::Max(1u, static_cast<plUInt32>(fLength / m_fSpacing));

    edgeSampleCounts.PushBack(uiSamples);
    uiTotalSamples += uiSamples;
  }

  if (uiTotalSamples == 0)
    return;

  // wall-contour order: consecutive accepted samples become cover surfaces
  plDynamicArray<plUInt32> edgeOrder;
  plDynamicArray<bool> chainStart;
  BuildEdgeChains(m_Edges, edgeOrder, chainStart);

  // pass 2: probe everything, or an exactly-m_uiMaxPoints-sized evenly strided subset
  const bool bDecimate = uiTotalSamples > m_uiMaxPoints;
  const float fEffectiveStride = bDecimate ? (m_fSpacing * static_cast<float>(uiTotalSamples) / static_cast<float>(m_uiMaxPoints)) : m_fSpacing;
  const float fSurfaceBreakDistSqr = plMath::Square(2.5f * fEffectiveStride);

  plUInt64 uiSampleIndex = 0;
  plInt32 iOpenSurface = -1; // surface currently being extended, -1 = none
  plVec3 vLastAccepted = plVec3::MakeZero();

  for (plUInt32 uiOrder = 0; uiOrder < edgeOrder.GetCount(); ++uiOrder)
  {
    if (chainStart[uiOrder])
    {
      iOpenSurface = -1; // a new wall contour never continues the previous surface
    }

    const plAiCoverEdge& edge = m_Edges[edgeOrder[uiOrder]];
    const plUInt32 uiSamples = edgeSampleCounts[edgeOrder[uiOrder]];

    for (plUInt32 s = 0; s < uiSamples; ++s, ++uiSampleIndex)
    {
      // integer decimation: keep sample i iff the scaled index advances - exactly m_uiMaxPoints
      // samples are kept, uniformly spread over the sector
      if (bDecimate && (uiSampleIndex * m_uiMaxPoints) / uiTotalSamples == ((uiSampleIndex + 1) * m_uiMaxPoints) / uiTotalSamples)
        continue;

      const float t = (s + 0.5f) / uiSamples;
      const plVec3 vOnEdge = plMath::Lerp(edge.m_vStart, edge.m_vEnd, t);
      const plVec3 vStandPos = vOnEdge - edge.m_vOutwardDir * m_fInset;

      plAiCoverPoint point;

      if (!plAiCoverGen::ProbePoint(*m_pPhysics, vStandPos, edge.m_vOutwardDir, cfg, point))
      {
        iOpenSurface = -1; // a probed gap (ledge, opening) splits the wall strip
        continue;
      }

      // a large position jump (decimation stride, skipped ledges) also splits the strip
      if (iOpenSurface >= 0)
      {
        if ((point.m_vPosition - vLastAccepted).GetAsVec2().GetLengthSquared() > fSurfaceBreakDistSqr || plMath::Abs(point.m_vPosition.z - vLastAccepted.z) > 0.6f)
        {
          iOpenSurface = -1;
        }
      }

      if (iOpenSurface < 0)
      {
        iOpenSurface = m_Surfaces.GetCount();
        plAiCoverSurface& surface = m_Surfaces.ExpandAndGetRef();
        surface.m_uiFirstPoint = static_cast<plUInt16>(m_Points.GetCount());
        surface.m_uiNumPoints = 0;
      }

      point.m_uiSurface = static_cast<plUInt16>(iOpenSurface);
      ++m_Surfaces[iOpenSurface].m_uiNumPoints;
      vLastAccepted = point.m_vPosition;

      m_Points.PushBack(point);

      if (m_Points.GetCount() >= m_uiMaxPoints)
        return; // safety cap; unreachable when decimating (kept samples == m_uiMaxPoints)
    }
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Implementation_CoverGeneration);
