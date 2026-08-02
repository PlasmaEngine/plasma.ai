#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Navigation/NavMeshQuery.h>

static constexpr plUInt32 MaxSearchNodes = 16;

plAiNavmeshQuery::plAiNavmeshQuery()
{
  m_uiReinitQueryBit = 1;
}

void plAiNavmeshQuery::SetNavmesh(plAiNavMesh* pNavmesh)
{
  if (m_pNavmesh == pNavmesh)
    return;

  m_pNavmesh = pNavmesh;
  m_uiReinitQueryBit = 1;
}

void plAiNavmeshQuery::SetQueryFilter(const dtQueryFilter& filter)
{
  if (m_pFilter == &filter)
    return;

  m_pFilter = &filter;
}

bool plAiNavmeshQuery::PrepareQueryArea(const plVec3& vCenter, float fRadius)
{
  PL_ASSERT_DEV(m_pNavmesh != nullptr, "Navmesh has not been set.");
  return m_pNavmesh->RequestSector(vCenter.GetAsVec2(), plVec2(fRadius));
}

bool plAiNavmeshQuery::Raycast(const plVec3& vStart, const plVec3& vDir, float fDistance, plAiNavmeshRaycastHit& out_raycastHit)
{
  if (m_uiReinitQueryBit)
  {
    PL_ASSERT_DEV(m_pNavmesh != nullptr, "Navmesh has not been set.");
    PL_ASSERT_DEV(m_pFilter != nullptr, "Navmesh filter has not been set.");

    m_uiReinitQueryBit = 0;
    m_Query.init(m_pNavmesh->GetDetourNavMesh(), MaxSearchNodes);
  }

  // TODO: hardcoded 'epsilon'
  float he[3] = {2, 2, 2};

  dtPolyRef ref;
  float pt[3];

  if (dtStatusFailed(m_Query.findNearestPoly(plRcPos(vStart), he, m_pFilter, &ref, pt)))
    return false;

  if (ref == 0)
    return false;

  dtRaycastHit hit{};
  if (dtStatusFailed(m_Query.raycast(ref, plRcPos(vStart), plRcPos(vStart + vDir * fDistance), m_pFilter, 0, &hit)))
    return false;

  if (hit.t > 1.0f)
    return false;

  out_raycastHit.m_fHitDistanceNormalized = hit.t;
  out_raycastHit.m_fHitDistance = hit.t * fDistance;
  out_raycastHit.m_vHitPosition = vStart + (vDir * fDistance * hit.t);

  return true;
}

thread_local plRandom* tl_pRandom = nullptr;

static float frand()
{
  return tl_pRandom->FloatZeroToOneInclusive();
}

bool plAiNavmeshQuery::FindRandomPointAroundCircle(const plVec3& vStart, float fRadius, plRandom& ref_rng, plVec3& out_vPoint)
{
  if (m_uiReinitQueryBit)
  {
    PL_ASSERT_DEV(m_pNavmesh != nullptr, "Navmesh has not been set.");
    PL_ASSERT_DEV(m_pFilter != nullptr, "Navmesh filter has not been set.");

    m_uiReinitQueryBit = 0;
    m_Query.init(m_pNavmesh->GetDetourNavMesh(), MaxSearchNodes);
  }

  // TODO: hardcoded 'epsilon'
  float he[3] = {2, 2, 2};

  dtPolyRef ref;
  float pt[3];

  if (dtStatusFailed(m_Query.findNearestPoly(plRcPos(vStart), he, m_pFilter, &ref, pt)))
    return false;

  if (ref == 0)
    return false;

  tl_pRandom = &ref_rng;

  dtPolyRef resultRef;
  plRcPos resPt;
  if (dtStatusFailed(m_Query.findRandomPointAroundCircle(ref, plRcPos(vStart), fRadius, m_pFilter, frand, &resultRef, resPt)))
    return false;

  out_vPoint = resPt;
  return true;
}
