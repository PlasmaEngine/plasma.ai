#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/VoxelGrid.h>
#include <Foundation/Math/Math.h>
#include <Foundation/Threading/AtomicUtils.h>
#include <RendererCore/Debug/DebugRenderer.h>

plVoxelGrid::plVoxelGrid() = default;
plVoxelGrid::~plVoxelGrid() = default;

void plVoxelGrid::Init(plUInt32 uiDimX, plUInt32 uiDimY, plUInt32 uiDimZ)
{
  m_uiDimX = plMath::Max(4u, uiDimX);
  m_uiDimY = plMath::Max(4u, uiDimY);
  m_uiDimZ = plMath::Max(4u, uiDimZ);

  m_uiBlocksX = (m_uiDimX + 3u) / 4u;
  m_uiBlocksY = (m_uiDimY + 3u) / 4u;
  m_uiBlocksZ = (m_uiDimZ + 3u) / 4u;

  // Round dims up to block boundaries
  m_uiDimX = m_uiBlocksX * 4u;
  m_uiDimY = m_uiBlocksY * 4u;
  m_uiDimZ = m_uiBlocksZ * 4u;

  m_Blocks.Clear();
  m_Blocks.SetCount(m_uiBlocksX * m_uiBlocksY * m_uiBlocksZ, 0ull);
  m_Clearance.Clear();
}

void plVoxelGrid::ClearData()
{
  for (plUInt32 i = 0; i < m_Blocks.GetCount(); ++i)
  {
    m_Blocks[i] = 0ull;
  }
}

void plVoxelGrid::SetWorldParameters(const plVec3& vCenter, float fVoxelSize)
{
  m_vCenter = vCenter;
  m_fVoxelSize = fVoxelSize;
  m_fInvVoxelSize = 1.0f / fVoxelSize;

  const plVec3 vHalfExtents(
    m_uiDimX * 0.5f * m_fVoxelSize,
    m_uiDimY * 0.5f * m_fVoxelSize,
    m_uiDimZ * 0.5f * m_fVoxelSize);

  m_vOrigin = m_vCenter - vHalfExtents;
}

bool plVoxelGrid::WorldToCoord(const plVec3& vWorldPos, plVec3I32& out_vCoord) const
{
  const plVec3 vLocal = (vWorldPos - m_vOrigin) * m_fInvVoxelSize;

  out_vCoord.x = (plInt32)plMath::Floor(vLocal.x);
  out_vCoord.y = (plInt32)plMath::Floor(vLocal.y);
  out_vCoord.z = (plInt32)plMath::Floor(vLocal.z);

  return IsCoordValid(out_vCoord);
}

plVec3 plVoxelGrid::CoordToWorld(const plVec3I32& vCoord) const
{
  return m_vOrigin + plVec3(
                       (vCoord.x + 0.5f) * m_fVoxelSize,
                       (vCoord.y + 0.5f) * m_fVoxelSize,
                       (vCoord.z + 0.5f) * m_fVoxelSize);
}

bool plVoxelGrid::IsCoordValid(const plVec3I32& vCoord) const
{
  return vCoord.x >= 0 && vCoord.y >= 0 && vCoord.z >= 0 &&
         (plUInt32)vCoord.x < m_uiDimX &&
         (plUInt32)vCoord.y < m_uiDimY &&
         (plUInt32)vCoord.z < m_uiDimZ;
}

plUInt32 plVoxelGrid::GetBlockIndex(plUInt32 uiBlockX, plUInt32 uiBlockY, plUInt32 uiBlockZ) const
{
  return uiBlockZ * (m_uiBlocksX * m_uiBlocksY) + uiBlockY * m_uiBlocksX + uiBlockX;
}

plUInt32 plVoxelGrid::GetBitIndex(plUInt32 uiLocalX, plUInt32 uiLocalY, plUInt32 uiLocalZ)
{
  return uiLocalZ * 16u + uiLocalY * 4u + uiLocalX;
}

void plVoxelGrid::SetVoxel(const plVec3I32& vCoord, bool bSolid)
{
  if (!IsCoordValid(vCoord))
    return;

  const plUInt32 uiX = (plUInt32)vCoord.x;
  const plUInt32 uiY = (plUInt32)vCoord.y;
  const plUInt32 uiZ = (plUInt32)vCoord.z;

  const plUInt32 uiBlockIdx = GetBlockIndex(uiX / 4u, uiY / 4u, uiZ / 4u);
  const plUInt32 uiBit = GetBitIndex(uiX % 4u, uiY % 4u, uiZ % 4u);
  const plUInt64 uiMask = 1ull << uiBit;

  if (bSolid)
  {
    m_Blocks[uiBlockIdx] |= uiMask;
  }
  else
  {
    m_Blocks[uiBlockIdx] &= ~uiMask;
  }
}

void plVoxelGrid::SetVoxelThreadSafe(const plVec3I32& vCoord)
{
  if (!IsCoordValid(vCoord))
    return;

  const plUInt32 uiX = (plUInt32)vCoord.x;
  const plUInt32 uiY = (plUInt32)vCoord.y;
  const plUInt32 uiZ = (plUInt32)vCoord.z;

  const plUInt32 uiBlockIdx = GetBlockIndex(uiX / 4u, uiY / 4u, uiZ / 4u);
  const plUInt32 uiBit = GetBitIndex(uiX % 4u, uiY % 4u, uiZ % 4u);
  const plUInt64 uiMask = 1ull << uiBit;

  // Atomic OR so concurrent writes to voxels sharing the same 64-bit block don't clobber each other.
  plAtomicUtils::Or(reinterpret_cast<plInt64&>(m_Blocks[uiBlockIdx]), (plInt64)uiMask);
}

bool plVoxelGrid::CheckVoxel(const plVec3I32& vCoord) const
{
  if (!IsCoordValid(vCoord))
    return false;

  const plUInt32 uiX = (plUInt32)vCoord.x;
  const plUInt32 uiY = (plUInt32)vCoord.y;
  const plUInt32 uiZ = (plUInt32)vCoord.z;

  const plUInt32 uiBlockIdx = GetBlockIndex(uiX / 4u, uiY / 4u, uiZ / 4u);
  const plUInt64 uiBlock = m_Blocks[uiBlockIdx];

  if (uiBlock == 0ull)
    return false;

  const plUInt32 uiBit = GetBitIndex(uiX % 4u, uiY % 4u, uiZ % 4u);
  const plUInt64 uiMask = 1ull << uiBit;

  return (uiBlock & uiMask) != 0ull;
}

bool plVoxelGrid::GetCoordRange(const plBoundingBox& box, plVec3I32& out_vMin, plVec3I32& out_vMax) const
{
  WorldToCoord(box.m_vMin, out_vMin);
  WorldToCoord(box.m_vMax, out_vMax);

  out_vMin.x = plMath::Max(out_vMin.x, 0);
  out_vMin.y = plMath::Max(out_vMin.y, 0);
  out_vMin.z = plMath::Max(out_vMin.z, 0);
  out_vMax.x = plMath::Min(out_vMax.x, (plInt32)m_uiDimX - 1);
  out_vMax.y = plMath::Min(out_vMax.y, (plInt32)m_uiDimY - 1);
  out_vMax.z = plMath::Min(out_vMax.z, (plInt32)m_uiDimZ - 1);

  return out_vMin.x <= out_vMax.x && out_vMin.y <= out_vMax.y && out_vMin.z <= out_vMax.z;
}

void plVoxelGrid::InjectBox(const plBoundingBox& box)
{
  plVec3I32 vMin, vMax;
  if (!GetCoordRange(box, vMin, vMax))
    return;

  for (plInt32 z = vMin.z; z <= vMax.z; ++z)
  {
    for (plInt32 y = vMin.y; y <= vMax.y; ++y)
    {
      for (plInt32 x = vMin.x; x <= vMax.x; ++x)
      {
        SetVoxel(plVec3I32(x, y, z), true);
      }
    }
  }
}

void plVoxelGrid::InjectSphere(const plVec3& vCenter, float fRadius)
{
  const plBoundingBox aabb = plBoundingBox::MakeFromCenterAndHalfExtents(vCenter, plVec3(fRadius));

  plVec3I32 vMin, vMax;
  WorldToCoord(aabb.m_vMin, vMin);
  WorldToCoord(aabb.m_vMax, vMax);

  vMin.x = plMath::Max(vMin.x, 0);
  vMin.y = plMath::Max(vMin.y, 0);
  vMin.z = plMath::Max(vMin.z, 0);
  vMax.x = plMath::Min(vMax.x, (plInt32)m_uiDimX - 1);
  vMax.y = plMath::Min(vMax.y, (plInt32)m_uiDimY - 1);
  vMax.z = plMath::Min(vMax.z, (plInt32)m_uiDimZ - 1);

  const float fRadiusSqr = fRadius * fRadius;

  for (plInt32 z = vMin.z; z <= vMax.z; ++z)
  {
    for (plInt32 y = vMin.y; y <= vMax.y; ++y)
    {
      for (plInt32 x = vMin.x; x <= vMax.x; ++x)
      {
        const plVec3 vVoxelCenter = CoordToWorld(plVec3I32(x, y, z));
        const float fDistSqr = (vVoxelCenter - vCenter).GetLengthSquared();

        if (fDistSqr <= fRadiusSqr)
        {
          SetVoxel(plVec3I32(x, y, z), true);
        }
      }
    }
  }
}

void plVoxelGrid::SubtractBox(const plBoundingBox& box)
{
  plVec3I32 vMin, vMax;
  if (!GetCoordRange(box, vMin, vMax))
    return;

  for (plInt32 z = vMin.z; z <= vMax.z; ++z)
  {
    for (plInt32 y = vMin.y; y <= vMax.y; ++y)
    {
      for (plInt32 x = vMin.x; x <= vMax.x; ++x)
      {
        SetVoxel(plVec3I32(x, y, z), false);
      }
    }
  }
}

bool plVoxelGrid::IsVisibleCoord(const plVec3I32& vStart, const plVec3I32& vGoal) const
{
  const plInt32 dx = vGoal.x - vStart.x;
  const plInt32 dy = vGoal.y - vStart.y;
  const plInt32 dz = vGoal.z - vStart.z;

  const plInt32 iSteps = plMath::Max(plMath::Abs(dx), plMath::Max(plMath::Abs(dy), plMath::Abs(dz)));

  if (iSteps == 0)
    return true;

  const float fInvSteps = 1.0f / (float)iSteps;
  const float fXIncr = (float)dx * fInvSteps;
  const float fYIncr = (float)dy * fInvSteps;
  const float fZIncr = (float)dz * fInvSteps;

  float fX = (float)vStart.x;
  float fY = (float)vStart.y;
  float fZ = (float)vStart.z;

  plVec3I32 vPrevious = vStart;

  for (plInt32 i = 0; i <= iSteps; ++i)
  {
    const plVec3I32 vCoord(
      (plInt32)plMath::Round(fX),
      (plInt32)plMath::Round(fY),
      (plInt32)plMath::Round(fZ));

    if (CheckVoxel(vCoord))
      return false;

    // a diagonal step passes the voxels on its sides: they must be free as well
    const plVec3I32 vStep = vCoord - vPrevious;
    if (plMath::Abs(vStep.x) + plMath::Abs(vStep.y) + plMath::Abs(vStep.z) > 1)
    {
      for (plInt32 iMask = 1; iMask < 7; ++iMask)
      {
        const plVec3I32 vSide(vPrevious.x + ((iMask & 1) ? vStep.x : 0), vPrevious.y + ((iMask & 2) ? vStep.y : 0), vPrevious.z + ((iMask & 4) ? vStep.z : 0));

        if (vSide != vCoord && CheckVoxel(vSide))
          return false;
      }
    }

    if (vCoord == vGoal)
      return true;

    vPrevious = vCoord;
    fX += fXIncr;
    fY += fYIncr;
    fZ += fZIncr;
  }

  return true;
}

void plVoxelGrid::BuildClearance()
{
  if (m_Blocks.IsEmpty())
    return;

  m_Clearance.SetCountUninitialized(m_uiDimX * m_uiDimY * m_uiDimZ);
  ComputeClearance(plVec3I32(0, 0, 0), plVec3I32(m_uiDimX - 1, m_uiDimY - 1, m_uiDimZ - 1));
}

void plVoxelGrid::UpdateClearance(const plBoundingBox& changedRegion)
{
  if (m_Clearance.IsEmpty())
    return;

  plVec3I32 vMin, vMax;
  if (!GetCoordRange(changedRegion, vMin, vMax))
    return;

  // a changed voxel affects the clearance of every voxel up to MaxClearance away
  const plInt32 iReach = MaxClearance;
  vMin = plVec3I32(plMath::Max(vMin.x - iReach, 0), plMath::Max(vMin.y - iReach, 0), plMath::Max(vMin.z - iReach, 0));
  vMax = plVec3I32(plMath::Min<plInt32>(vMax.x + iReach, m_uiDimX - 1), plMath::Min<plInt32>(vMax.y + iReach, m_uiDimY - 1), plMath::Min<plInt32>(vMax.z + iReach, m_uiDimZ - 1));

  ComputeClearance(vMin, vMax);
}

plUInt8 plVoxelGrid::GetClearance(const plVec3I32& vCoord) const
{
  if (m_Clearance.IsEmpty() || !IsCoordValid(vCoord))
    return MaxClearance;

  return m_Clearance[GetVoxelIndex(vCoord)];
}

void plVoxelGrid::ComputeClearance(const plVec3I32& vMin, const plVec3I32& vMax)
{
  // The distance to the nearest occupied voxel along any axis or diagonal is separable: a 1D distance pass
  // per axis, each combining with max(). The working box extends MaxClearance beyond [vMin, vMax], so
  // occupied voxels just outside still count; voxels outside the grid count as free.
  const plInt32 iReach = MaxClearance;
  const plVec3I32 vWorkMin(plMath::Max(vMin.x - iReach, 0), plMath::Max(vMin.y - iReach, 0), plMath::Max(vMin.z - iReach, 0));
  const plVec3I32 vWorkMax(plMath::Min<plInt32>(vMax.x + iReach, m_uiDimX - 1), plMath::Min<plInt32>(vMax.y + iReach, m_uiDimY - 1), plMath::Min<plInt32>(vMax.z + iReach, m_uiDimZ - 1));
  const plVec3I32 vSize = vWorkMax - vWorkMin + plVec3I32(1, 1, 1);

  auto WorkIndex = [&](plInt32 x, plInt32 y, plInt32 z) { return ((plUInt32)z * vSize.y + (plUInt32)y) * vSize.x + (plUInt32)x; };

  plDynamicArray<plUInt8> work;
  work.SetCountUninitialized(vSize.x * vSize.y * vSize.z);

  for (plInt32 z = 0; z < vSize.z; ++z)
    for (plInt32 y = 0; y < vSize.y; ++y)
      for (plInt32 x = 0; x < vSize.x; ++x)
        work[WorkIndex(x, y, z)] = CheckVoxel(vWorkMin + plVec3I32(x, y, z)) ? 0 : MaxClearance;

  plDynamicArray<plUInt8> line;

  // one pass along an axis: d(i) = min over |k| <= MaxClearance of max(|k|, d(i + k))
  auto Pass = [&](plInt32 iAxis) {
    const plInt32 iLength = (iAxis == 0) ? vSize.x : ((iAxis == 1) ? vSize.y : vSize.z);
    const plInt32 iOtherA = (iAxis == 0) ? vSize.y : vSize.x;
    const plInt32 iOtherB = (iAxis == 2) ? vSize.y : vSize.z;
    line.SetCountUninitialized(iLength);

    for (plInt32 b = 0; b < iOtherB; ++b)
    {
      for (plInt32 a = 0; a < iOtherA; ++a)
      {
        auto Index = [&](plInt32 i) {
          if (iAxis == 0)
            return WorkIndex(i, a, b);
          if (iAxis == 1)
            return WorkIndex(a, i, b);
          return WorkIndex(a, b, i);
        };

        for (plInt32 i = 0; i < iLength; ++i)
        {
          line[i] = work[Index(i)];
        }

        for (plInt32 i = 0; i < iLength; ++i)
        {
          plUInt8 uiBest = line[i];

          for (plInt32 k = 1; k < uiBest; ++k)
          {
            const plUInt8 uiLeft = (i - k >= 0) ? line[i - k] : MaxClearance;
            const plUInt8 uiRight = (i + k < iLength) ? line[i + k] : MaxClearance;
            uiBest = plMath::Min<plUInt8>(uiBest, plMath::Max<plUInt8>(static_cast<plUInt8>(k), plMath::Min(uiLeft, uiRight)));
          }

          work[Index(i)] = uiBest;
        }
      }
    }
  };

  Pass(0);
  Pass(1);
  Pass(2);

  for (plInt32 z = vMin.z; z <= vMax.z; ++z)
    for (plInt32 y = vMin.y; y <= vMax.y; ++y)
      for (plInt32 x = vMin.x; x <= vMax.x; ++x)
        m_Clearance[GetVoxelIndex(plVec3I32(x, y, z))] = work[WorkIndex(x - vWorkMin.x, y - vWorkMin.y, z - vWorkMin.z)];
}

bool plVoxelGrid::IsVisible(const plVec3& vObserver, const plVec3& vSubject) const
{
  plVec3I32 vStart, vGoal;

  if (!WorldToCoord(vObserver, vStart))
    return false;

  if (!WorldToCoord(vSubject, vGoal))
    return false;

  return IsVisibleCoord(vStart, vGoal);
}

plBoundingBox plVoxelGrid::GetAABB() const
{
  const plVec3 vHalfExtents(
    m_uiDimX * 0.5f * m_fVoxelSize,
    m_uiDimY * 0.5f * m_fVoxelSize,
    m_uiDimZ * 0.5f * m_fVoxelSize);

  return plBoundingBox::MakeFromCenterAndHalfExtents(m_vCenter, vHalfExtents);
}

plUInt64 plVoxelGrid::GetMemoryUsage() const
{
  return m_Blocks.GetCount() * sizeof(plUInt64);
}

void plVoxelGrid::DebugDraw(const plDebugRendererContext& context, const plColor& color) const
{
  if (m_Blocks.IsEmpty())
    return;

  // Shrink boxes slightly so individual voxels are visually distinguishable
  const float fHalf = m_fVoxelSize * 0.45f;
  const plVec3 vHalfExtents(fHalf, fHalf, fHalf);

  for (plUInt32 bz = 0; bz < m_uiBlocksZ; ++bz)
  {
    for (plUInt32 by = 0; by < m_uiBlocksY; ++by)
    {
      for (plUInt32 bx = 0; bx < m_uiBlocksX; ++bx)
      {
        const plUInt32 uiBlockIdx = GetBlockIndex(bx, by, bz);
        plUInt64 uiBlock = m_Blocks[uiBlockIdx];

        if (uiBlock == 0ull)
          continue;

        while (uiBlock != 0ull)
        {
          // Find lowest set bit
          plUInt32 uiBit = 0;
          plUInt64 uiTemp = uiBlock;
          while ((uiTemp & 1ull) == 0ull)
          {
            uiTemp >>= 1;
            ++uiBit;
          }
          uiBlock ^= (1ull << uiBit);

          const plUInt32 lx = uiBit % 4u;
          const plUInt32 ly = (uiBit / 4u) % 4u;
          const plUInt32 lz = uiBit / 16u;

          const plVec3I32 vGlobalCoord(
            (plInt32)(bx * 4u + lx),
            (plInt32)(by * 4u + ly),
            (plInt32)(bz * 4u + lz));

          // Only draw surface voxels (at least one free neighbor)
          bool bIsSurface = false;
          const plVec3I32 vNeighbors[] = {
            {vGlobalCoord.x - 1, vGlobalCoord.y, vGlobalCoord.z},
            {vGlobalCoord.x + 1, vGlobalCoord.y, vGlobalCoord.z},
            {vGlobalCoord.x, vGlobalCoord.y - 1, vGlobalCoord.z},
            {vGlobalCoord.x, vGlobalCoord.y + 1, vGlobalCoord.z},
            {vGlobalCoord.x, vGlobalCoord.y, vGlobalCoord.z - 1},
            {vGlobalCoord.x, vGlobalCoord.y, vGlobalCoord.z + 1},
          };

          for (const auto& vNeighbor : vNeighbors)
          {
            if (!IsCoordValid(vNeighbor) || !CheckVoxel(vNeighbor))
            {
              bIsSurface = true;
              break;
            }
          }

          if (bIsSurface)
          {
            const plVec3 vWorldPos = CoordToWorld(vGlobalCoord);
            const plBoundingBox voxelBox = plBoundingBox::MakeFromCenterAndHalfExtents(vWorldPos, vHalfExtents);
            plDebugRenderer::DrawLineBox(context, voxelBox, color);
          }
        }
      }
    }
  }
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Implementation_VoxelGrid);
