#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/VoxelGrid.h>
#include <AiPlugin/Navigation/VoxelNavigation.h>
#include <Foundation/Containers/HashTable.h>
#include <Foundation/Math/Math.h>
#include <RendererCore/Debug/DebugRenderer.h>

plAiVoxelNavigation::plAiVoxelNavigation() = default;
plAiVoxelNavigation::~plAiVoxelNavigation() = default;

void plAiVoxelNavigation::SetVoxelGrid(const plVoxelGrid* pGrid)
{
  m_pGrid = pGrid;
}

namespace
{
  struct AStarNode
  {
    PL_DECLARE_POD_TYPE();

    float fGCost = plMath::MaxValue<float>();
    float fFCost = plMath::MaxValue<float>();
    plUInt32 uiParent = plInvalidIndex;
    bool bClosed = false;
  };

  PL_FORCE_INLINE plUInt32 PackCoord(const plVec3I32& vCoord, plUInt32 uiDimX, plUInt32 uiDimY)
  {
    return (plUInt32)vCoord.z * uiDimX * uiDimY + (plUInt32)vCoord.y * uiDimX + (plUInt32)vCoord.x;
  }

  PL_FORCE_INLINE plVec3I32 UnpackCoord(plUInt32 uiIndex, plUInt32 uiDimX, plUInt32 uiDimY)
  {
    const plInt32 z = (plInt32)(uiIndex / (uiDimX * uiDimY));
    const plUInt32 uiRemaining = uiIndex - (plUInt32)z * uiDimX * uiDimY;
    const plInt32 y = (plInt32)(uiRemaining / uiDimX);
    const plInt32 x = (plInt32)(uiRemaining % uiDimX);
    return plVec3I32(x, y, z);
  }

  float OctileHeuristic(const plVec3I32& vA, const plVec3I32& vB)
  {
    const float dx = (float)plMath::Abs(vA.x - vB.x);
    const float dy = (float)plMath::Abs(vA.y - vB.y);
    const float dz = (float)plMath::Abs(vA.z - vB.z);

    // Sort so that d1 <= d2 <= d3
    float d1 = plMath::Min(dx, plMath::Min(dy, dz));
    float d3 = plMath::Max(dx, plMath::Max(dy, dz));
    float d2 = dx + dy + dz - d1 - d3;

    // d1 space-diagonal steps (cost sqrt(3)), then (d2 - d1) face-diagonal steps (sqrt(2)), then (d3 - d2) axis steps (1)
    static const float fSqrt2 = plMath::Sqrt(2.0f);
    static const float fSqrt3 = plMath::Sqrt(3.0f);

    return d1 * fSqrt3 + (d2 - d1) * fSqrt2 + (d3 - d2);
  }

  // Minimal binary min-heap for A* open set
  struct OpenSetEntry
  {
    PL_DECLARE_POD_TYPE();

    plUInt32 uiIndex;
    float fFCost;
  };

  void HeapPush(plDynamicArray<OpenSetEntry>& heap, plUInt32 uiIndex, float fFCost)
  {
    OpenSetEntry entry;
    entry.uiIndex = uiIndex;
    entry.fFCost = fFCost;
    heap.PushBack(entry);

    // Sift up
    plUInt32 i = heap.GetCount() - 1;
    while (i > 0)
    {
      const plUInt32 uiParent = (i - 1) / 2;
      if (heap[uiParent].fFCost > heap[i].fFCost)
      {
        plMath::Swap(heap[uiParent], heap[i]);
        i = uiParent;
      }
      else
      {
        break;
      }
    }
  }

  OpenSetEntry HeapPop(plDynamicArray<OpenSetEntry>& heap)
  {
    OpenSetEntry top = heap[0];
    heap[0] = heap[heap.GetCount() - 1];
    heap.PopBack();

    // Sift down
    plUInt32 i = 0;
    const plUInt32 uiCount = heap.GetCount();
    while (true)
    {
      plUInt32 uiSmallest = i;
      const plUInt32 uiLeft = 2 * i + 1;
      const plUInt32 uiRight = 2 * i + 2;

      if (uiLeft < uiCount && heap[uiLeft].fFCost < heap[uiSmallest].fFCost)
        uiSmallest = uiLeft;
      if (uiRight < uiCount && heap[uiRight].fFCost < heap[uiSmallest].fFCost)
        uiSmallest = uiRight;

      if (uiSmallest != i)
      {
        plMath::Swap(heap[i], heap[uiSmallest]);
        i = uiSmallest;
      }
      else
      {
        break;
      }
    }

    return top;
  }
} // namespace

plAiVoxelNavigation::State plAiVoxelNavigation::FindPath(const plVec3& vStart, const plVec3& vTarget, plUInt32 uiMaxIterations)
{
  m_Waypoints.Clear();
  m_uiCurrentWaypoint = 0;

  if (m_pGrid == nullptr || !m_pGrid->IsInitialized())
  {
    m_State = State::NoPathFound;
    return m_State;
  }

  plVec3I32 vStartCoord, vTargetCoord;

  if (!m_pGrid->WorldToCoord(vStart, vStartCoord) || m_pGrid->CheckVoxel(vStartCoord))
  {
    m_State = State::InvalidStartPosition;
    return m_State;
  }

  if (!m_pGrid->WorldToCoord(vTarget, vTargetCoord) || m_pGrid->CheckVoxel(vTargetCoord))
  {
    m_State = State::InvalidTargetPosition;
    return m_State;
  }

  const plUInt32 uiDimX = m_pGrid->GetDimX();
  const plUInt32 uiDimY = m_pGrid->GetDimY();

  const plUInt32 uiStartPacked = PackCoord(vStartCoord, uiDimX, uiDimY);
  const plUInt32 uiTargetPacked = PackCoord(vTargetCoord, uiDimX, uiDimY);

  plHashTable<plUInt32, AStarNode> nodes;
  plDynamicArray<OpenSetEntry> openSet;

  // Initialize start node
  {
    AStarNode startNode;
    startNode.fGCost = 0.0f;
    startNode.fFCost = OctileHeuristic(vStartCoord, vTargetCoord);
    startNode.uiParent = plInvalidIndex;
    nodes.Insert(uiStartPacked, startNode);
    HeapPush(openSet, uiStartPacked, startNode.fFCost);
  }

  static const float fSqrt2 = plMath::Sqrt(2.0f);
  static const float fSqrt3 = plMath::Sqrt(3.0f);

  // 26-connected neighbor offsets and costs
  struct Neighbor
  {
    plInt32 dx, dy, dz;
    float fCost;
  };

  Neighbor neighbors[26];
  plUInt32 uiNeighborCount = 0;
  for (plInt32 dz = -1; dz <= 1; ++dz)
  {
    for (plInt32 dy = -1; dy <= 1; ++dy)
    {
      for (plInt32 dx = -1; dx <= 1; ++dx)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;

        const plInt32 iManhattan = plMath::Abs(dx) + plMath::Abs(dy) + plMath::Abs(dz);
        float fCost;
        if (iManhattan == 1)
          fCost = 1.0f;
        else if (iManhattan == 2)
          fCost = fSqrt2;
        else
          fCost = fSqrt3;

        Neighbor& n = neighbors[uiNeighborCount++];
        n.dx = dx;
        n.dy = dy;
        n.dz = dz;
        n.fCost = fCost;
      }
    }
  }

  plUInt32 uiIterations = 0;
  bool bFound = false;

  while (!openSet.IsEmpty() && uiIterations < uiMaxIterations)
  {
    ++uiIterations;

    const OpenSetEntry current = HeapPop(openSet);

    AStarNode* pCurrentNode = nullptr;
    nodes.TryGetValue(current.uiIndex, pCurrentNode);

    if (pCurrentNode == nullptr || pCurrentNode->bClosed)
      continue;

    pCurrentNode->bClosed = true;

    if (current.uiIndex == uiTargetPacked)
    {
      bFound = true;
      break;
    }

    const plVec3I32 vCurrentCoord = UnpackCoord(current.uiIndex, uiDimX, uiDimY);
    const float fCurrentG = pCurrentNode->fGCost;

    for (plUInt32 n = 0; n < uiNeighborCount; ++n)
    {
      const plVec3I32 vNeighborCoord(
        vCurrentCoord.x + neighbors[n].dx,
        vCurrentCoord.y + neighbors[n].dy,
        vCurrentCoord.z + neighbors[n].dz);

      if (!m_pGrid->IsCoordValid(vNeighborCoord))
        continue;

      if (m_pGrid->CheckVoxel(vNeighborCoord))
        continue;

      const plUInt32 uiNeighborPacked = PackCoord(vNeighborCoord, uiDimX, uiDimY);
      const float fTentativeG = fCurrentG + neighbors[n].fCost;

      AStarNode* pNeighborNode = nullptr;
      if (!nodes.TryGetValue(uiNeighborPacked, pNeighborNode))
      {
        AStarNode newNode;
        newNode.fGCost = fTentativeG;
        newNode.fFCost = fTentativeG + OctileHeuristic(vNeighborCoord, vTargetCoord);
        newNode.uiParent = current.uiIndex;
        nodes.Insert(uiNeighborPacked, newNode);
        HeapPush(openSet, uiNeighborPacked, newNode.fFCost);
      }
      else if (!pNeighborNode->bClosed && fTentativeG < pNeighborNode->fGCost)
      {
        pNeighborNode->fGCost = fTentativeG;
        pNeighborNode->fFCost = fTentativeG + OctileHeuristic(vNeighborCoord, vTargetCoord);
        pNeighborNode->uiParent = current.uiIndex;
        // Re-insert into open set (lazy deletion handles stale entries)
        HeapPush(openSet, uiNeighborPacked, pNeighborNode->fFCost);
      }
    }
  }

  if (!bFound)
  {
    m_State = State::NoPathFound;
    return m_State;
  }

  // Back-trace path
  plDynamicArray<plVec3> rawPath;
  plUInt32 uiCurrent = uiTargetPacked;

  while (uiCurrent != plInvalidIndex)
  {
    const plVec3I32 vCoord = UnpackCoord(uiCurrent, uiDimX, uiDimY);
    rawPath.PushBack(m_pGrid->CoordToWorld(vCoord));

    AStarNode* pNode = nullptr;
    if (nodes.TryGetValue(uiCurrent, pNode))
    {
      uiCurrent = pNode->uiParent;
    }
    else
    {
      break;
    }
  }

  // Reverse to get start-to-target order
  for (plUInt32 i = 0; i < rawPath.GetCount() / 2; ++i)
  {
    plMath::Swap(rawPath[i], rawPath[rawPath.GetCount() - 1 - i]);
  }

  // Smooth the path using line-of-sight string-pulling
  SmoothPath(rawPath);

  m_Waypoints = std::move(rawPath);
  m_uiCurrentWaypoint = 0;
  m_State = State::PathFound;
  return m_State;
}

void plAiVoxelNavigation::SmoothPath(plDynamicArray<plVec3>& inout_waypoints) const
{
  if (inout_waypoints.GetCount() <= 2 || m_pGrid == nullptr)
    return;

  plDynamicArray<plVec3> smoothed;
  smoothed.PushBack(inout_waypoints[0]);

  plUInt32 uiCurrent = 0;

  while (uiCurrent < inout_waypoints.GetCount() - 1)
  {
    plUInt32 uiFarthestVisible = uiCurrent + 1;

    for (plUInt32 i = inout_waypoints.GetCount() - 1; i > uiCurrent + 1; --i)
    {
      if (m_pGrid->IsVisible(inout_waypoints[uiCurrent], inout_waypoints[i]))
      {
        uiFarthestVisible = i;
        break;
      }
    }

    smoothed.PushBack(inout_waypoints[uiFarthestVisible]);
    uiCurrent = uiFarthestVisible;
  }

  inout_waypoints = std::move(smoothed);
}

bool plAiVoxelNavigation::AdvanceWaypoint()
{
  if (m_uiCurrentWaypoint + 1 < m_Waypoints.GetCount())
  {
    ++m_uiCurrentWaypoint;
    return true;
  }
  return false;
}

plVec3 plAiVoxelNavigation::GetNextWaypoint() const
{
  if (m_Waypoints.IsEmpty())
    return plVec3::MakeZero();

  if (m_uiCurrentWaypoint < m_Waypoints.GetCount())
    return m_Waypoints[m_uiCurrentWaypoint];

  return m_Waypoints[m_Waypoints.GetCount() - 1];
}

bool plAiVoxelNavigation::IsPathComplete() const
{
  return m_Waypoints.IsEmpty() || m_uiCurrentWaypoint >= m_Waypoints.GetCount();
}

void plAiVoxelNavigation::CancelNavigation()
{
  m_Waypoints.Clear();
  m_uiCurrentWaypoint = 0;
  m_State = State::Idle;
}

void plAiVoxelNavigation::DebugDrawPath(const plDebugRendererContext& context, const plColor& color) const
{
  if (m_Waypoints.GetCount() < 2)
    return;

  plDynamicArray<plDebugRenderer::Line> lines;
  lines.Reserve(m_Waypoints.GetCount() - 1);

  for (plUInt32 i = 0; i + 1 < m_Waypoints.GetCount(); ++i)
  {
    auto& line = lines.ExpandAndGetRef();
    line.m_start = m_Waypoints[i];
    line.m_end = m_Waypoints[i + 1];

    if (i < m_uiCurrentWaypoint)
    {
      line.m_startColor = plColor::Grey;
      line.m_endColor = plColor::Grey;
    }
    else
    {
      line.m_startColor = color;
      line.m_endColor = color;
    }
  }

  plDebugRenderer::DrawLines(context, lines, color);
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Navigation_Implementation_VoxelNavigation);
