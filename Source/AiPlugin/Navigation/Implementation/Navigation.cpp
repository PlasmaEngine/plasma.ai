#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshQuery.h>
#include <AiPlugin/Navigation/Navigation.h>
#include <DetourNavMesh.h>
#include <Foundation/Math/Rect.h>
#include <Recast.h>
#include <RendererCore/Debug/DebugRenderer.h>

plResult FindNavMeshPolyAt(dtNavMeshQuery& ref_query, const dtQueryFilter* pQueryFilter, plRcPos position, dtPolyRef& out_polyRef, plVec3* out_pAdjustedPosition /*= nullptr*/, float fPlaneEpsilon /*= 0.01f*/, float fHeightEpsilon /*= 1.0f*/)
{
  plVec3 vSize(fPlaneEpsilon, fHeightEpsilon, fPlaneEpsilon);

  plRcPos resultPos;
  if (dtStatusFailed(ref_query.findNearestPoly(position, &vSize.x, pQueryFilter, &out_polyRef, resultPos)))
    return PL_FAILURE;

  if (out_polyRef == 0)
    return PL_FAILURE;

  if (!plMath::IsEqual(position.m_Pos[0], resultPos.m_Pos[0], fPlaneEpsilon) ||
      !plMath::IsEqual(position.m_Pos[1], resultPos.m_Pos[1], fHeightEpsilon) ||
      !plMath::IsEqual(position.m_Pos[2], resultPos.m_Pos[2], fPlaneEpsilon))
  {
    return PL_FAILURE;
  }

  if (out_pAdjustedPosition != nullptr)
  {
    *out_pAdjustedPosition = resultPos;
  }

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

plAiNavigation::plAiNavigation()
{
  m_uiCurrentPositionChangedBit = 0;
  m_uiTargetPositionChangedBit = 0;
  m_uiReinitQueryBit = 0;

  m_PathCorridor.init(MaxPathNodes);
}

plAiNavigation::~plAiNavigation() = default;

void plAiNavigation::Update()
{
  if (m_pNavmesh == nullptr || m_pFilter == nullptr)
    return;

  if (m_uiReinitQueryBit)
  {
    m_uiReinitQueryBit = 0;
    m_Query.init(m_pNavmesh->GetDetourNavMesh(), MaxSearchNodes);

    ResetPathContinuation();
    m_pContinueQuery.Clear();
  }

  if (!UpdatePathSearch())
    return;

  if (m_PathCorridor.getPathCount() == 0)
  {
    switch (m_State)
    {
      case State::Idle:
      case State::InvalidTargetPosition:
        if (m_uiTargetPositionChangedBit)
          m_State = State::StartNewSearch;
        break;

      case State::InvalidCurrentPosition:
        if (m_uiCurrentPositionChangedBit)
          m_State = State::StartNewSearch;
        break;

      case State::NoPathFound:
        // a changed navmesh (a door opened, a sector finished building) may have a path now
        if (m_uiCurrentPositionChangedBit || m_uiTargetPositionChangedBit || m_pNavmesh->GetNavMeshGeneration() != m_uiSearchNavMeshGeneration)
          m_State = State::StartNewSearch;
        break;

        PL_DEFAULT_CASE_NOT_IMPLEMENTED;
    }

    if (m_State == State::StartNewSearch)
    {
      // already kick off the path search
      UpdatePathSearch();
    }

    return;
  }

  // when tiles were added or removed since the corridor was computed, the corridor's cached
  // polygon references may be stale beyond the short lookahead that is checked during the
  // regular updates - validate the ENTIRE corridor once per navmesh change
  {
    const plUInt32 uiNavMeshGeneration = m_pNavmesh->GetNavMeshGeneration();

    if (uiNavMeshGeneration != m_uiLastNavMeshGeneration)
    {
      m_uiLastNavMeshGeneration = uiNavMeshGeneration;

      if (!m_PathCorridor.isValid(MaxPathNodes, &m_Query, m_pFilter))
      {
        CancelNavigation();
        m_State = State::StartNewSearch;
        return;
      }
    }
  }

  if (m_uiCurrentPositionChangedBit)
  {
    // make sure the system creates the necessary sectors at some point
    {
      plRectFloat r = plRectFloat::MakeInvalid();
      r.ExpandToInclude(m_vCurrentPosition.GetAsVec2());
      r.Grow(c_fPathSearchBoundary);
      m_pNavmesh->RequestSector(r.GetCenter(), r.GetHalfExtents());
    }

    const dtPolyRef firstPoly = m_PathCorridor.getFirstPoly();

    if (!m_PathCorridor.movePosition(plRcPos(m_vCurrentPosition), &m_Query, m_pFilter))
    {
      PL_REPORT_FAILURE("Steered into invalid position."); // not sure under which conditions this can happen
      CancelNavigation();
      m_State = State::StartNewSearch;
      return;
    }

    plVec3 resPos = plRcPos(m_PathCorridor.getPos());
    if (!resPos.GetAsVec2().IsEqual(m_vCurrentPosition.GetAsVec2(), 0.2f))
    {
      const float fHalfSearchVert = (m_fPolySearchUp + m_fPolySearchDown) * 0.5f;
      const plVec3 vPosOffset(0, 0, m_fPolySearchUp - fHalfSearchVert);

      dtPolyRef startRef;
      if (FindNavMeshPolyAt(m_Query, m_pFilter, m_vCurrentPosition + vPosOffset, startRef, nullptr, m_fPolySearchRadius, fHalfSearchVert).Failed())
      {
        CancelNavigation();
        m_State = State::InvalidCurrentPosition;
        return;
      }
      else if (startRef != m_PathCorridor.getFirstPoly())
      {
        CancelNavigation();
        m_State = State::StartNewSearch;
        return;
      }

      // the character's center is outside the navmesh, but the expanded poly query still returns the same polygon
      // so we keep this path corridor
    }

    if (firstPoly != m_PathCorridor.getFirstPoly())
    {
      // the path corridor got modified (may got shorter or longer)
      m_uiOptimizeTopologyCounter++;
    }

    m_uiCurrentPositionChangedBit = 0;
  }

  if (m_uiTargetPositionChangedBit)
  {
    // make sure the system creates the necessary sectors at some point
    {
      plRectFloat r = plRectFloat::MakeInvalid();
      r.ExpandToInclude(m_vTargetPosition.GetAsVec2());
      r.Grow(c_fPathSearchBoundary);
      m_pNavmesh->RequestSector(r.GetCenter(), r.GetHalfExtents());
    }

    const dtPolyRef lastPoly = m_PathCorridor.getLastPoly();

    if (!m_PathCorridor.moveTargetPosition(plRcPos(m_vTargetPosition), &m_Query, m_pFilter))
    {
      plLog::Error("Target position not reachable anymore.");
      CancelNavigation();
      m_State = State::StartNewSearch;
      return;
    }

    plVec3 resPos = plRcPos(m_PathCorridor.getTarget());
    if (!resPos.GetAsVec2().IsEqual(m_vTargetPosition.GetAsVec2(), 0.2f))
    {
      const float fHalfSearchVert = (m_fPolySearchUp + m_fPolySearchDown) * 0.5f;
      const plVec3 vPosOffset(0, 0, m_fPolySearchUp - fHalfSearchVert);

      dtPolyRef endRef;
      if (FindNavMeshPolyAt(m_Query, m_pFilter, m_vTargetPosition + vPosOffset, endRef, nullptr, m_fPolySearchRadius, fHalfSearchVert).Failed())
      {
        CancelNavigation();
        m_State = State::InvalidTargetPosition;
        return;
      }
      else if (endRef != m_PathCorridor.getLastPoly())
      {
        CancelNavigation();
        m_State = State::StartNewSearch;
        return;
      }

      // the target's center is outside the navmesh, but the expanded poly query still returns the same polygon
      // so we keep this path corridor
    }

    if (lastPoly != m_PathCorridor.getLastPoly())
    {
      // the path corridor got modified (may got shorter or longer)
      m_uiOptimizeTopologyCounter++;
    }

    m_uiTargetPositionChangedBit = 0;
  }

  if (m_uiOptimizeTopologyCounter > 10)
  {
    m_uiOptimizeTopologyCounter = 0;
    m_PathCorridor.optimizePathTopology(&m_Query, m_pFilter);
  }

  UpdatePathContinuation();
}

void plAiNavigation::ResetPathContinuation()
{
  m_bPathContinues = false;
  m_bContinueSearching = false;
}

void plAiNavigation::UpdatePathContinuation()
{
  if (!m_bPathContinues || (m_State != State::FullPathFound && m_State != State::PartialPathFound))
    return;

  if (!m_bContinueSearching)
  {
    if (m_PathCorridor.getPathCount() > ContinueAtPathNodes)
      return;

    const dtPolyRef endPoly = m_PathCorridor.getLastPoly();

    plRcPos startPos;
    if (dtStatusFailed(m_Query.closestPointOnPoly(endPoly, plRcPos(m_vPathSearchTargetPos), startPos, nullptr)))
    {
      CancelNavigation();
      m_State = State::StartNewSearch;
      return;
    }

    // the sectors between the corridor end and the target have to be there before the search can pass them
    plRectFloat r = plRectFloat::MakeInvalid();
    r.ExpandToInclude(plVec3(startPos).GetAsVec2());
    r.ExpandToInclude(m_vPathSearchTargetPos.GetAsVec2());
    r.Grow(c_fPathSearchBoundary);

    if (!m_pNavmesh->RequestSector(r.GetCenter(), r.GetHalfExtents()))
      return;

    if (m_pContinueQuery == nullptr)
    {
      m_pContinueQuery = PL_DEFAULT_NEW(dtNavMeshQuery);
      m_pContinueQuery->init(m_pNavmesh->GetDetourNavMesh(), MaxSearchNodes);
    }

    if (dtStatusFailed(m_pContinueQuery->initSlicedFindPath(endPoly, m_PathSearchTargetPoly, startPos, plRcPos(m_vPathSearchTargetPos), m_pFilter)))
    {
      CancelNavigation();
      m_State = State::StartNewSearch;
      return;
    }

    m_bContinueSearching = true;
    return;
  }

  int iIterationsDone = 0;
  const dtStatus searchStatus = m_pContinueQuery->updateSlicedFindPath(32, &iIterationsDone);

  if (dtStatusInProgress(searchStatus))
    return;

  m_bContinueSearching = false;

  plInt32 iTailLength = 0;
  dtPolyRef tailPolys[MaxPathNodes];
  dtStatus finalizeStatus = DT_FAILURE;

  if (dtStatusSucceed(searchStatus))
  {
    finalizeStatus = m_pContinueQuery->finalizeSlicedFindPath(tailPolys, &iTailLength, (int)MaxPathNodes);
  }

  if (dtStatusFailed(finalizeStatus) || iTailLength < 1)
  {
    // the way ahead is gone, so search again from where the agent is
    CancelNavigation();
    m_State = State::StartNewSearch;
    return;
  }

  const int iCorridorLength = m_PathCorridor.getPathCount();
  const dtPolyRef* pCorridor = m_PathCorridor.getPath();

  if (iCorridorLength == 0 || pCorridor[iCorridorLength - 1] != tailPolys[0])
  {
    // the end of the corridor moved while searching, so the result doesn't attach to it; try again
    return;
  }

  dtPolyRef mergedPolys[MaxPathNodes];
  int iMergedLength = 0;

  for (int i = 0; i < iCorridorLength; ++i)
  {
    mergedPolys[iMergedLength++] = pCorridor[i];
  }

  int iTailUsed = 1; // tailPolys[0] is the last corridor polygon
  while (iTailUsed < iTailLength && iMergedLength < (int)MaxPathNodes)
  {
    mergedPolys[iMergedLength++] = tailPolys[iTailUsed++];
  }

  const bool bTailCut = iTailUsed < iTailLength;
  const bool bReachedTarget = !bTailCut && tailPolys[iTailLength - 1] == m_PathSearchTargetPoly;
  const bool bProgress = iMergedLength > iCorridorLength;

  m_PathCorridor.setCorridor(m_PathCorridor.getTarget(), mergedPolys, (plUInt32)iMergedLength);

  if (!m_PathCorridor.isValid(MaxPathNodes, &m_Query, m_pFilter))
  {
    CancelNavigation();
    m_State = State::StartNewSearch;
    return;
  }

  if (bReachedTarget)
  {
    m_bPathContinues = false;
    return;
  }

  const bool bBufferCut = bTailCut || (finalizeStatus & DT_BUFFER_TOO_SMALL) != 0;
  const bool bOutOfNodes = (finalizeStatus & DT_PARTIAL_RESULT) != 0 && (finalizeStatus & DT_OUT_OF_NODES) != 0;

  if (!bProgress || (!bBufferCut && !bOutOfNodes))
  {
    // what is left of the way leads nowhere nearer: the target can't be reached, walk to the closest point
    m_bPathContinues = false;
    m_State = State::PartialPathFound;
  }
}

void plAiNavigation::CancelNavigation()
{
  ResetPathContinuation();
  m_PathCorridor.clear();
  m_uiTargetPositionChangedBit = 0; // don't start another path search
  m_State = State::Idle;
}

plResult plAiNavigation::TraverseOffMeshLink(dtPolyRef linkPoly, plVec3& out_vStart, plVec3& out_vEnd, plUInt32& out_uiUserID)
{
  if (m_pNavmesh == nullptr || m_PathCorridor.getPathCount() == 0)
    return PL_FAILURE;

  dtPolyRef refs[2];
  plRcPos startPos, endPos;

  if (!m_PathCorridor.moveOverOffmeshConnection(linkPoly, refs, startPos, endPos, &m_Query))
    return PL_FAILURE; // corridor is not at the link yet - keep steering normally, retry next frame

  out_vStart = startPos;
  out_vEnd = endPos;
  out_uiUserID = plInvalidIndex;

  if (const dtOffMeshConnection* pConnection = m_pNavmesh->GetDetourNavMesh()->getOffMeshConnectionByRef(linkPoly))
  {
    out_uiUserID = pConnection->userId;
  }

  // the corridor now continues at the link end - sync the bookkeeping so the next
  // SetCurrentPosition/movePosition doesn't fight it
  m_vCurrentPosition = out_vEnd;
  m_uiCurrentPositionChangedBit = 0;

  return PL_SUCCESS;
}

void plAiNavigation::SetCurrentPosition(const plVec3& vPosition)
{
  if (m_vCurrentPosition == vPosition)
    return;

  m_vCurrentPosition = vPosition;
  m_uiCurrentPositionChangedBit = 1;
}

void plAiNavigation::SetTargetPosition(const plVec3& vPosition)
{
  // a cut corridor can't take a moved target, it searches again; behaviors that repeat the same destination must not restart it
  if (m_bPathContinues && m_vTargetPosition == vPosition)
    return;

  m_vTargetPosition = vPosition;
  m_uiTargetPositionChangedBit = 1;
}

const plVec3& plAiNavigation::GetTargetPosition() const
{
  return m_vTargetPosition;
}

void plAiNavigation::SetNavmesh(plAiNavMesh* pNavmesh)
{
  if (m_pNavmesh == pNavmesh)
    return;

  m_pNavmesh = pNavmesh;
  m_uiReinitQueryBit = 1;
}

void plAiNavigation::SetQueryFilter(const dtQueryFilter& filter)
{
  if (m_pFilter == &filter)
    return;

  m_pFilter = &filter;
}

void plAiNavigation::ComputeAllWaypoints(plDynamicArray<plVec3>& out_waypoints) const
{
  out_waypoints.Clear();

  if (m_PathCorridor.getPathCount() == 0)
    return;

  plUInt8 cornerFlags[MaxPathNodes];
  dtPolyRef cornerPolys[MaxPathNodes];
  plRcPos straightPath[MaxPathNodes];

  const int straightLen = m_PathCorridor.findCorners(straightPath[0], cornerFlags, cornerPolys, MaxPathNodes, &m_Query);

  out_waypoints.SetCountUninitialized((plUInt32)straightLen);

  for (plUInt32 i = 0; i < (plUInt32)straightLen; ++i)
  {
    out_waypoints[i] = straightPath[i]; // automatically swaps Y and Z
  }
}

void plAiNavigation::SetNoPathFound()
{
  if (m_pNavmesh->GetNavMeshGeneration() != m_uiSearchNavMeshGeneration)
  {
    // tiles were added or removed during the search, which may be why it failed
    m_State = State::StartNewSearch;
    return;
  }

  m_State = State::NoPathFound;
}

bool plAiNavigation::UpdatePathSearch()
{
  if (m_State == State::StartNewSearch)
  {
    plRectFloat r = plRectFloat::MakeInvalid();
    r.ExpandToInclude(m_vCurrentPosition.GetAsVec2());
    r.ExpandToInclude(m_vTargetPosition.GetAsVec2());
    r.Grow(c_fPathSearchBoundary);

    if (!m_pNavmesh->RequestSector(r.GetCenter(), r.GetHalfExtents()))
    {
      // navmesh sectors aren't loaded yet
      return false;
    }

    m_uiCurrentPositionChangedBit = 0;

    const float fHalfSearchVert = (m_fPolySearchUp + m_fPolySearchDown) * 0.5f;
    const plVec3 vPosOffset(0, 0, m_fPolySearchUp - fHalfSearchVert);

    dtPolyRef startRef;
    if (FindNavMeshPolyAt(m_Query, m_pFilter, m_vCurrentPosition + vPosOffset, startRef, nullptr, m_fPolySearchRadius, fHalfSearchVert).Failed())
    {
      m_State = State::InvalidCurrentPosition;
      return false;
    }

    m_uiTargetPositionChangedBit = 0;

    if (FindNavMeshPolyAt(m_Query, m_pFilter, m_vTargetPosition + vPosOffset, m_PathSearchTargetPoly, nullptr, m_fPolySearchRadius, fHalfSearchVert).Failed())
    {
      m_State = State::InvalidTargetPosition;
      return false;
    }

    m_vPathSearchTargetPos = m_vTargetPosition;
    m_uiSearchNavMeshGeneration = m_pNavmesh->GetNavMeshGeneration();

    if (dtStatusFailed(m_Query.initSlicedFindPath(startRef, m_PathSearchTargetPoly, plRcPos(m_vCurrentPosition), plRcPos(m_vTargetPosition), m_pFilter)))
    {
      m_State = State::NoPathFound;
      PL_REPORT_FAILURE("Detour: initSlicedFindPath failed.");
      return false;
    }

    m_State = State::Searching;
    return false;
  }

  if (m_State == State::Searching)
  {
    const int iMaxIterations = 32;
    int iIterationsDone = 0;
    dtStatus res = m_Query.updateSlicedFindPath(iMaxIterations, &iIterationsDone);

    if (dtStatusInProgress(res))
    {
      // still searching
      return false;
    }

    if (dtStatusFailed(res))
    {
      SetNoPathFound();
      return false;
    }

    plInt32 iPathCorridorLength = 0;
    dtPolyRef resultPolys[MaxPathNodes];

    const dtStatus finalizeStatus = m_Query.finalizeSlicedFindPath(resultPolys, &iPathCorridorLength, (int)MaxPathNodes);

    if (dtStatusFailed(finalizeStatus))
    {
      SetNoPathFound();
      return false;
    }

    // reduce to actual length
    PL_ASSERT_DEV(iPathCorridorLength >= 1, "Expected path corridor to have at least length 1");

    ResetPathContinuation();

    if (resultPolys[iPathCorridorLength - 1] == m_PathSearchTargetPoly)
    {
      m_State = State::FullPathFound;
    }
    else if ((finalizeStatus & DT_BUFFER_TOO_SMALL) != 0 || ((finalizeStatus & DT_PARTIAL_RESULT) != 0 && (finalizeStatus & DT_OUT_OF_NODES) != 0))
    {
      // the path is longer than the corridor, or the search ran out of nodes on the way: the corridor ends at the best point
      // the search got to, and is extended from there while the agent walks (see UpdatePathContinuation())
      m_bPathContinues = true;
      m_State = State::FullPathFound;
    }
    else
    {
      // if this is the case, the target position cannot be reached, but we can walk close to it
      m_State = State::PartialPathFound;
    }

    // the target position here may already differ from the target position when the search was started
    // so we need to use m_vPathSearchTargetPos
    // the final target position will be updated in the next Update()
    m_PathCorridor.setCorridor(plRcPos(m_vPathSearchTargetPos), resultPolys, (plUInt32)iPathCorridorLength);

    m_uiOptimizeTopologyCounter = 0;
    m_uiOptimizeVisibilityCounter = 0;
  }

  if (m_State == State::FullPathFound || m_State == State::PartialPathFound)
  {
    constexpr plInt32 PathLookahead = 10;

    dtPolyRef currentPoly = m_PathCorridor.getFirstPoly();
    if (!m_Query.isValidPolyRef(currentPoly, m_pFilter)
        || !m_Query.isValidPolyRef(m_PathSearchTargetPoly, m_pFilter)
        || !m_PathCorridor.isValid(PathLookahead, &m_Query, m_pFilter))
    {
      CancelNavigation();
      m_State = State::StartNewSearch;
      return false;
    }
  }

  return true;
}

void plAiNavigation::DebugDrawPathCorridor(const plDebugRendererContext& context, plColor tilesColor, float fPolyRenderOffsetZ)
{
  const plUInt32 uiCorrLen = m_PathCorridor.getPathCount();
  const dtPolyRef* pCorrArr = m_PathCorridor.getPath();

  plHybridArray<plDebugRenderer::Triangle, 64> tris;

  const auto pNavmesh = m_Query.getAttachedNavMesh();
  if (pNavmesh == nullptr)
    return;

  for (plUInt32 c = 0; c < uiCorrLen; ++c)
  {
    dtPolyRef poly = pCorrArr[c];

    // a failed agent keeps its corridor, whose tiles may have been rebuilt or unloaded since
    const dtMeshTile* pTile = nullptr;
    const dtPoly* pPoly = nullptr;
    if (dtStatusFailed(pNavmesh->getTileAndPolyByRef(poly, &pTile, &pPoly)))
      continue;

    for (plUInt32 i = 2; i < pPoly->vertCount; ++i)
    {
      plRcPos rcPos[3];
      rcPos[0] = &(pTile->verts[pPoly->verts[0] * 3]);
      rcPos[1] = &(pTile->verts[pPoly->verts[i - 1] * 3]);
      rcPos[2] = &(pTile->verts[pPoly->verts[i] * 3]);

      auto& tri = tris.ExpandAndGetRef();
      tri.m_position[0] = plVec3(rcPos[0]);
      tri.m_position[2] = plVec3(rcPos[1]);
      tri.m_position[1] = plVec3(rcPos[2]);

      tri.m_position[0].z += fPolyRenderOffsetZ;
      tri.m_position[1].z += fPolyRenderOffsetZ;
      tri.m_position[2].z += fPolyRenderOffsetZ;
    }
  }

  plDebugRenderer::DrawSolidTriangles(context, tris, tilesColor);
}

void plAiNavigation::DebugDrawPathLine(const plDebugRendererContext& context, plColor straightLineColor, float fLineRenderOffsetZ)
{
  plHybridArray<plDebugRenderer::Line, 64> lines;
  plHybridArray<plVec3, 64> waypoints;
  ComputeAllWaypoints(waypoints);

  if (!waypoints.IsEmpty())
  {
    plVec3 vStart = m_vCurrentPosition;
    vStart.z += fLineRenderOffsetZ;

    for (plUInt32 i = 0; i < waypoints.GetCount(); ++i)
    {
      plVec3 vthis = waypoints[i];
      vthis.z += fLineRenderOffsetZ;

      auto& line = lines.ExpandAndGetRef();
      line.m_start = vStart;
      line.m_end = vthis;
      vStart = vthis;
    }

    if(this->GetState() == State::FullPathFound)
    {
      plAiNavmeshQuery query;
      query.SetNavmesh(m_pNavmesh);
      query.SetQueryFilter(*m_pFilter);
    }

    plDebugRenderer::DrawLinesOccluded(context, lines, straightLineColor.GetDarker());
    plDebugRenderer::DrawLines(context, lines, straightLineColor);
  }
}

float plAiNavigation::GetCurrentElevation() const
{
  if (m_PathCorridor.getPathCount() > 0)
  {
    float h = m_vCurrentPosition.z;
    m_Query.getPolyHeight(m_PathCorridor.getFirstPoly(), m_PathCorridor.getPos(), &h);
    return h;
  }

  return m_vCurrentPosition.z;
}

void plAiNavigation::ComputeSteeringInfo(plAiSteeringInfo& out_info, const plVec2& vForwardDir, float fMaxLookAhead)
{
  out_info.m_vNextWaypoint = m_vCurrentPosition;

  if (m_PathCorridor.getPathCount() <= 0)
    return;

  static constexpr plUInt32 MaxTempNodes = 8;

  plUInt8 cornerFlags[MaxTempNodes];
  dtPolyRef cornerPolys[MaxTempNodes];
  plRcPos straightPath[MaxTempNodes];

  const bool bOptimize = m_uiOptimizeVisibilityCounter++ > 30;

  const int straightLen = m_PathCorridor.findCorners(straightPath[0], cornerFlags, cornerPolys, MaxTempNodes, &m_Query);

  if (straightLen > 0 && bOptimize)
  {
    m_uiOptimizeVisibilityCounter = 0;

    m_PathCorridor.optimizePathVisibility(straightPath[straightLen - 1], 10.0f, &m_Query, m_pFilter);
  }

  bool bFoundWaypoint = false;
  float fDistToPt = 0;

  out_info.m_fDistanceToWaypoint = 0;
  out_info.m_fArrivalDistance = plMath::HighValue<float>();
  out_info.m_vNextWaypoint = m_vCurrentPosition;
  out_info.m_vDirectionTowardsWaypoint = vForwardDir;
  out_info.m_AbsRotationTowardsWaypoint = plAngle::MakeZero();
  out_info.m_MaxAbsRotationAfterWaypoint = plAngle::MakeZero();
  out_info.m_bNextWaypointIsLink = false;
  out_info.m_LinkPolyRef = 0;
  // out_info.m_fWaypointCorridorWidth = plMath::HighValue<float>();

  plVec3 vPrevPos = m_vCurrentPosition;

  for (int idx = 0; idx < straightLen; ++idx)
  {
    fDistToPt += (plVec3(straightPath[idx]) - vPrevPos).GetLength();
    vPrevPos = straightPath[idx];

    if (cornerFlags[idx] & dtStraightPathFlags::DT_STRAIGHTPATH_END)
    {
      out_info.m_fArrivalDistance = fDistToPt;
    }

    if (!bFoundWaypoint && ((cornerFlags[idx] & dtStraightPathFlags::DT_STRAIGHTPATH_START) == 0))
    {
      bFoundWaypoint = true;
      out_info.m_vNextWaypoint = straightPath[idx];
      out_info.m_fDistanceToWaypoint = fDistToPt;
      out_info.m_vDirectionTowardsWaypoint = out_info.m_vNextWaypoint.GetAsVec2() - m_vCurrentPosition.GetAsVec2();
      out_info.m_vDirectionTowardsWaypoint.NormalizeIfNotZero(vForwardDir).IgnoreResult();

      out_info.m_AbsRotationTowardsWaypoint = out_info.m_vDirectionTowardsWaypoint.GetAngleBetween(vForwardDir);

      if (cornerFlags[idx] & dtStraightPathFlags::DT_STRAIGHTPATH_OFFMESH_CONNECTION)
      {
        // the next waypoint is the start of a nav link (jump, ladder, door, ...)
        out_info.m_bNextWaypointIsLink = true;
        out_info.m_LinkPolyRef = cornerPolys[idx];
      }

      continue;
    }

    if (bFoundWaypoint && fDistToPt < fMaxLookAhead)
    {
      const plVec3 vNextPt = straightPath[idx];
      plVec2 vNextDir = (vNextPt - out_info.m_vNextWaypoint).GetAsVec2();
      if (vNextDir.NormalizeIfNotZero(plVec2::MakeZero()).Succeeded())
      {
        plAngle absDir = vNextDir.GetAngleBetween(out_info.m_vDirectionTowardsWaypoint);
        out_info.m_MaxAbsRotationAfterWaypoint = plMath::Max(absDir, out_info.m_MaxAbsRotationAfterWaypoint);
      }
    }
  }
}

void plAiNavigation::DebugDrawState(const plDebugRendererContext& context, const plVec3& vPosition) const
{
  switch (m_State)
  {
    case plAiNavigation::State::Idle:
      plDebugRenderer::Draw3DText(context, "Idle", vPosition, plColor::Grey);
      break;
    case plAiNavigation::State::StartNewSearch:
      plDebugRenderer::Draw3DText(context, "Starting Search...", vPosition, plColor::Yellow);
      break;
    case plAiNavigation::State::InvalidCurrentPosition:
      plDebugRenderer::Draw3DText(context, "Invalid Start Position", vPosition, plColor::Black);
      break;
    case plAiNavigation::State::InvalidTargetPosition:
      plDebugRenderer::Draw3DText(context, "Invalid Target Position", vPosition, plColor::IndianRed);
      break;
    case plAiNavigation::State::NoPathFound:
      plDebugRenderer::Draw3DText(context, "No Path Found", vPosition, plColor::White);
      break;
    case plAiNavigation::State::PartialPathFound:
      plDebugRenderer::Draw3DText(context, "Partial Path Found", vPosition, plColor::Turquoise);
      break;
    case plAiNavigation::State::FullPathFound:
      plDebugRenderer::Draw3DText(context, "Full Path Found", vPosition, plColor::LawnGreen);
      break;
    case plAiNavigation::State::Searching:
      plDebugRenderer::Draw3DText(context, "Searching...", vPosition, plColor::Yellow);
      break;
  }
}
