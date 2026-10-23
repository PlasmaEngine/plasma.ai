#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Navigation/VoxelGrid.h>
#include <AiPlugin/Navigation/VoxelNavigation.h>
#include <Foundation/Containers/HashTable.h>
#include <Foundation/Math/Math.h>
#include <RendererCore/Debug/DebugRenderer.h>

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

struct plAiVoxelNavigation::SearchState
{
  plVec3I32 m_vStart;
  plVec3I32 m_vTarget;
  plUInt32 m_uiStartPacked = 0;
  plUInt32 m_uiTargetPacked = 0;
  plUInt8 m_uiClearance = 1;
  plUInt32 m_uiGeneration = 0;
  plUInt32 m_uiExpanded = 0;
  plHashTable<plUInt32, AStarNode> m_Nodes;
  plDynamicArray<OpenSetEntry> m_OpenSet;

  /// Free, and clear enough for the agent. Within the clearance distance of the start or the target only
  /// free is required, so agents can start and stop next to geometry.
  bool IsPassable(const plVoxelGrid& grid, const plVec3I32& vCoord) const
  {
    if (!grid.IsCoordValid(vCoord) || grid.CheckVoxel(vCoord))
      return false;

    if (m_uiClearance <= 1 || grid.GetClearance(vCoord) >= m_uiClearance)
      return true;

    auto Distance = [&](const plVec3I32& vOther) { return plMath::Max(plMath::Abs(vCoord.x - vOther.x), plMath::Max(plMath::Abs(vCoord.y - vOther.y), plMath::Abs(vCoord.z - vOther.z))); };
    return Distance(m_vStart) < m_uiClearance || Distance(m_vTarget) < m_uiClearance;
  }

  /// A diagonal step needs every voxel on its sides passable, not just its destination.
  bool IsStepPassable(const plVoxelGrid& grid, const plVec3I32& vFrom, const plVec3I32& vStep) const
  {
    for (plInt32 iMask = 1; iMask < 7; ++iMask)
    {
      const plVec3I32 vOffset((iMask & 1) ? vStep.x : 0, (iMask & 2) ? vStep.y : 0, (iMask & 4) ? vStep.z : 0);

      // skip the empty offset and the step itself; with zero components of vStep, masks repeat these
      if ((vOffset.x == 0 && vOffset.y == 0 && vOffset.z == 0) || vOffset == vStep)
        continue;

      if (!IsPassable(grid, vFrom + vOffset))
        return false;
    }

    return true;
  }
};

plAiVoxelNavigation::plAiVoxelNavigation() = default;
plAiVoxelNavigation::~plAiVoxelNavigation() = default;

plUInt8 plAiVoxelNavigation::GetRequiredClearance(float fAgentRadius, float fVoxelSize)
{
  // an agent centered in a voxel touches a voxel c steps away once its radius exceeds (c - 0.5) voxels
  const float fRequired = plMath::Ceil(plMath::Max(0.0f, fAgentRadius) / plMath::Max(fVoxelSize, 0.001f) + 0.5f);
  return static_cast<plUInt8>(plMath::Clamp<float>(fRequired, 1.0f, plVoxelGrid::MaxClearance));
}

plAiVoxelNavigation::State plAiVoxelNavigation::FindPath(const plVec3& vStart, const plVec3& vTarget, plUInt32 uiMaxIterations, float fAgentRadius)
{
  if (StartPathSearch(vStart, vTarget, fAgentRadius) != State::Searching)
    return m_State;

  return ContinuePathSearch(uiMaxIterations, uiMaxIterations);
}

plAiVoxelNavigation::State plAiVoxelNavigation::FindPath(const plVec3& vStart, const plVec3& vTarget, plUInt32 uiMaxIterations)
{
  return FindPath(vStart, vTarget, uiMaxIterations, 0.0f);
}

plAiVoxelNavigation::State plAiVoxelNavigation::StartPathSearch(const plVec3& vStart, const plVec3& vTarget, float fAgentRadius)
{
  m_pSearch.Clear();

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

  m_pSearch = PL_DEFAULT_NEW(SearchState);
  m_pSearch->m_vStart = vStartCoord;
  m_pSearch->m_vTarget = vTargetCoord;
  m_pSearch->m_uiStartPacked = PackCoord(vStartCoord, m_pGrid->GetDimX(), m_pGrid->GetDimY());
  m_pSearch->m_uiTargetPacked = PackCoord(vTargetCoord, m_pGrid->GetDimX(), m_pGrid->GetDimY());
  m_pSearch->m_uiClearance = GetRequiredClearance(fAgentRadius, m_pGrid->GetVoxelSize());
  m_pSearch->m_uiGeneration = m_pGrid->GetGeneration();

  AStarNode startNode;
  startNode.fGCost = 0.0f;
  startNode.fFCost = OctileHeuristic(vStartCoord, vTargetCoord);
  m_pSearch->m_Nodes.Insert(m_pSearch->m_uiStartPacked, startNode);
  HeapPush(m_pSearch->m_OpenSet, m_pSearch->m_uiStartPacked, startNode.fFCost);

  m_State = State::Searching;
  return m_State;
}

plAiVoxelNavigation::State plAiVoxelNavigation::ContinuePathSearch(plUInt32 uiMaxNodes, plUInt32 uiMaxTotalNodes)
{
  if (m_pSearch == nullptr)
    return m_State;

  SearchState& search = *m_pSearch;
  const plUInt32 uiDimX = m_pGrid->GetDimX();
  const plUInt32 uiDimY = m_pGrid->GetDimY();

  static const float fSqrt2 = plMath::Sqrt(2.0f);
  static const float fSqrt3 = plMath::Sqrt(3.0f);

  for (plUInt32 uiStep = 0; uiStep < uiMaxNodes; ++uiStep)
  {
    if (search.m_OpenSet.IsEmpty() || search.m_uiExpanded >= uiMaxTotalNodes)
    {
      FinishSearch(State::NoPathFound);
      return m_State;
    }

    const OpenSetEntry current = HeapPop(search.m_OpenSet);

    AStarNode* pCurrentNode = nullptr;
    search.m_Nodes.TryGetValue(current.uiIndex, pCurrentNode);

    if (pCurrentNode == nullptr || pCurrentNode->bClosed)
      continue;

    pCurrentNode->bClosed = true;
    ++search.m_uiExpanded;

    if (current.uiIndex == search.m_uiTargetPacked)
    {
      FinishSearch(State::PathFound);
      return m_State;
    }

    const plVec3I32 vCurrentCoord = UnpackCoord(current.uiIndex, uiDimX, uiDimY);
    const float fCurrentG = pCurrentNode->fGCost;

    for (plInt32 dz = -1; dz <= 1; ++dz)
    {
      for (plInt32 dy = -1; dy <= 1; ++dy)
      {
        for (plInt32 dx = -1; dx <= 1; ++dx)
        {
          const plVec3I32 vStep(dx, dy, dz);
          const plInt32 iAxes = plMath::Abs(dx) + plMath::Abs(dy) + plMath::Abs(dz);

          if (iAxes == 0)
            continue;

          const plVec3I32 vNeighborCoord = vCurrentCoord + vStep;

          if (!search.IsPassable(*m_pGrid, vNeighborCoord) || (iAxes > 1 && !search.IsStepPassable(*m_pGrid, vCurrentCoord, vStep)))
            continue;

          const plUInt32 uiNeighborPacked = PackCoord(vNeighborCoord, uiDimX, uiDimY);
          const float fTentativeG = fCurrentG + ((iAxes == 1) ? 1.0f : ((iAxes == 2) ? fSqrt2 : fSqrt3));

          AStarNode* pNeighborNode = nullptr;
          if (!search.m_Nodes.TryGetValue(uiNeighborPacked, pNeighborNode))
          {
            AStarNode newNode;
            newNode.fGCost = fTentativeG;
            newNode.fFCost = fTentativeG + OctileHeuristic(vNeighborCoord, search.m_vTarget);
            newNode.uiParent = current.uiIndex;
            search.m_Nodes.Insert(uiNeighborPacked, newNode);
            HeapPush(search.m_OpenSet, uiNeighborPacked, newNode.fFCost);
          }
          else if (!pNeighborNode->bClosed && fTentativeG < pNeighborNode->fGCost)
          {
            pNeighborNode->fGCost = fTentativeG;
            pNeighborNode->fFCost = fTentativeG + OctileHeuristic(vNeighborCoord, search.m_vTarget);
            pNeighborNode->uiParent = current.uiIndex;
            // Re-insert into open set (lazy deletion handles stale entries)
            HeapPush(search.m_OpenSet, uiNeighborPacked, pNeighborNode->fFCost);
          }
        }
      }
    }
  }

  return m_State;
}

void plAiVoxelNavigation::FinishSearch(State result)
{
  plUniquePtr<SearchState> pSearch = std::move(m_pSearch);
  m_State = result;

  if (result != State::PathFound)
    return;

  const plUInt32 uiDimX = m_pGrid->GetDimX();
  const plUInt32 uiDimY = m_pGrid->GetDimY();

  // Back-trace path
  plDynamicArray<plVec3> rawPath;
  plUInt32 uiCurrent = pSearch->m_uiTargetPacked;

  while (uiCurrent != plInvalidIndex)
  {
    rawPath.PushBack(m_pGrid->CoordToWorld(UnpackCoord(uiCurrent, uiDimX, uiDimY)));

    AStarNode* pNode = nullptr;
    if (!pSearch->m_Nodes.TryGetValue(uiCurrent, pNode))
      break;

    uiCurrent = pNode->uiParent;
  }

  // Reverse to get start-to-target order
  for (plUInt32 i = 0; i < rawPath.GetCount() / 2; ++i)
  {
    plMath::Swap(rawPath[i], rawPath[rawPath.GetCount() - 1 - i]);
  }

  SmoothPath(rawPath, pSearch->m_uiClearance);

  // the first waypoint is where the agent was when the search started; it has moved on since
  m_Waypoints = std::move(rawPath);
  m_uiCurrentWaypoint = (m_Waypoints.GetCount() > 1) ? 1 : 0;
  m_uiPathGeneration = pSearch->m_uiGeneration;
}

void plAiVoxelNavigation::SmoothPath(plDynamicArray<plVec3>& inout_waypoints, plUInt8 uiClearance) const
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
      if (IsSegmentClear(inout_waypoints[uiCurrent], inout_waypoints[i], uiClearance))
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

bool plAiVoxelNavigation::IsSegmentClear(const plVec3& vFrom, const plVec3& vTo, plUInt8 uiClearance) const
{
  if (!m_pGrid->IsVisible(vFrom, vTo))
    return false;

  if (uiClearance <= 1)
    return true;

  // the shortcut must keep the agent's clearance; the waypoints themselves already do
  const float fLength = (vTo - vFrom).GetLength();
  const plUInt32 uiSamples = static_cast<plUInt32>(plMath::Ceil(fLength / (m_pGrid->GetVoxelSize() * 0.25f)));

  for (plUInt32 i = 1; i < uiSamples; ++i)
  {
    plVec3I32 vCoord;
    if (!m_pGrid->WorldToCoord(plMath::Lerp(vFrom, vTo, static_cast<float>(i) / uiSamples), vCoord) || m_pGrid->GetClearance(vCoord) < uiClearance)
      return false;
  }

  return true;
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
  m_pSearch.Clear();
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
