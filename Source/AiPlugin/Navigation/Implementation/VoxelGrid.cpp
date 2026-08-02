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

void plVoxelGrid::InjectBox(const plBoundingBox& box)
{
  plVec3I32 vMin, vMax;
  WorldToCoord(box.m_vMin, vMin);
  WorldToCoord(box.m_vMax, vMax);

  // Clamp to valid range
  vMin.x = plMath::Max(vMin.x, 0);
  vMin.y = plMath::Max(vMin.y, 0);
  vMin.z = plMath::Max(vMin.z, 0);
  vMax.x = plMath::Min(vMax.x, (plInt32)m_uiDimX - 1);
  vMax.y = plMath::Min(vMax.y, (plInt32)m_uiDimY - 1);
  vMax.z = plMath::Min(vMax.z, (plInt32)m_uiDimZ - 1);

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
  WorldToCoord(box.m_vMin, vMin);
  WorldToCoord(box.m_vMax, vMax);

  vMin.x = plMath::Max(vMin.x, 0);
  vMin.y = plMath::Max(vMin.y, 0);
  vMin.z = plMath::Max(vMin.z, 0);
  vMax.x = plMath::Min(vMax.x, (plInt32)m_uiDimX - 1);
  vMax.y = plMath::Min(vMax.y, (plInt32)m_uiDimY - 1);
  vMax.z = plMath::Min(vMax.z, (plInt32)m_uiDimZ - 1);

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

  for (plInt32 i = 0; i < iSteps; ++i)
  {
    const plVec3I32 vCoord(
      (plInt32)plMath::Round(fX),
      (plInt32)plMath::Round(fY),
      (plInt32)plMath::Round(fZ));

    if (vCoord.x == vGoal.x && vCoord.y == vGoal.y && vCoord.z == vGoal.z)
      return true;

    if (CheckVoxel(vCoord))
      return false;

    fX += fXIncr;
    fY += fYIncr;
    fZ += fZIncr;
  }

  return true;
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
