#include <Tests/AiTestPhysics.h>

#include <Core/World/World.h>

// clang-format off
PL_IMPLEMENT_WORLD_MODULE(plAiTestPhysicsWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiTestPhysicsWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_IMPLEMENT_WORLD_MODULE(plAiTestNavGeoWorldModule);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plAiTestNavGeoWorldModule, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plAiTestNavGeoWorldModule::plAiTestNavGeoWorldModule(plWorld* pWorld)
  : plNavmeshGeoWorldModuleInterface(pWorld)
{
}

void plAiTestNavGeoWorldModule::AddFloor(const plVec2& vMin, const plVec2& vMax, float fZ, const plSurfaceResource* pSurface)
{
  Floor& floorEntry = m_Floors.ExpandAndGetRef();
  floorEntry.m_Box = plBoundingBox::MakeFromMinMax(vMin.GetAsVec3(fZ), vMax.GetAsVec3(fZ));
  floorEntry.m_pSurface = pSurface;
}

void plAiTestNavGeoWorldModule::RetrieveGeometryInArea(plUInt32 uiCollisionLayer, const plBoundingBox& box, plDynamicArray<plNavmeshTriangle>& out_triangles) const
{
  for (const Floor& floorEntry : m_Floors)
  {
    const plBoundingBox& floor = floorEntry.m_Box;

    if (!floor.Overlaps(box))
      continue;

    // clipped to the query box; counter-clockwise seen from above, so the triangles face up
    const plVec3 a(plMath::Max(floor.m_vMin.x, box.m_vMin.x), plMath::Max(floor.m_vMin.y, box.m_vMin.y), floor.m_vMin.z);
    const plVec3 c(plMath::Min(floor.m_vMax.x, box.m_vMax.x), plMath::Min(floor.m_vMax.y, box.m_vMax.y), floor.m_vMin.z);
    const plVec3 b(c.x, a.y, a.z);
    const plVec3 d(a.x, c.y, a.z);

    plNavmeshTriangle& first = out_triangles.ExpandAndGetRef();
    first.m_Vertices[0] = a;
    first.m_Vertices[1] = b;
    first.m_Vertices[2] = c;
    first.m_pSurface = floorEntry.m_pSurface;

    plNavmeshTriangle& second = out_triangles.ExpandAndGetRef();
    second.m_Vertices[0] = a;
    second.m_Vertices[1] = c;
    second.m_Vertices[2] = d;
    second.m_pSurface = floorEntry.m_pSurface;
  }
}

namespace
{
  /// Entry distance of a ray into a box (slab test); false when it misses within fDistance.
  bool IntersectRay(const plBoundingBox& box, const plVec3& vStart, const plVec3& vDir, float fDistance, float& out_fEntry)
  {
    float fNear = 0.0f;
    float fFar = fDistance;

    for (plUInt32 i = 0; i < 3; ++i)
    {
      if (plMath::Abs(vDir.GetData()[i]) < 1e-6f)
      {
        if (vStart.GetData()[i] < box.m_vMin.GetData()[i] || vStart.GetData()[i] > box.m_vMax.GetData()[i])
          return false;

        continue;
      }

      float t0 = (box.m_vMin.GetData()[i] - vStart.GetData()[i]) / vDir.GetData()[i];
      float t1 = (box.m_vMax.GetData()[i] - vStart.GetData()[i]) / vDir.GetData()[i];

      if (t0 > t1)
      {
        plMath::Swap(t0, t1);
      }

      fNear = plMath::Max(fNear, t0);
      fFar = plMath::Min(fFar, t1);

      if (fNear > fFar)
        return false;
    }

    out_fEntry = fNear;
    return true;
  }
} // namespace

plAiTestPhysicsWorldModule::plAiTestPhysicsWorldModule(plWorld* pWorld)
  : plPhysicsWorldModuleInterface(pWorld)
{
}

void plAiTestPhysicsWorldModule::AddBox(const plBoundingBox& box, plPhysicsShapeType::Enum type, const plGameObjectHandle& hOwner)
{
  m_Boxes.PushBack({box, type, hOwner});
}

bool plAiTestPhysicsWorldModule::Raycast(plPhysicsCastResult& out_result, const plVec3& vStart, const plVec3& vDir, float fDistance, const plPhysicsQueryParameters& params, plPhysicsHitCollection collection) const
{
  plPhysicsCastResultArray results;
  if (!RaycastAll(results, vStart, vDir, fDistance, params))
    return false;

  out_result = results.m_Results[0];

  for (const plPhysicsCastResult& hit : results.m_Results)
  {
    if (hit.m_fDistance < out_result.m_fDistance)
    {
      out_result = hit;
    }
  }

  return true;
}

bool plAiTestPhysicsWorldModule::RaycastAll(plPhysicsCastResultArray& out_results, const plVec3& vStart, const plVec3& vDir, float fDistance, const plPhysicsQueryParameters& params) const
{
  for (const Box& box : m_Boxes)
  {
    float fEntry = 0.0f;
    if (!params.m_ShapeTypes.IsAnySet(box.m_Type) || !IntersectRay(box.m_Box, vStart, vDir, fDistance, fEntry))
      continue;

    plPhysicsCastResult& hit = out_results.m_Results.ExpandAndGetRef();
    hit.m_vPosition = vStart + vDir * fEntry;
    hit.m_vNormal = -vDir;
    hit.m_fDistance = fEntry;
    hit.m_hShapeObject = box.m_hOwner;
    hit.m_hActorObject = box.m_hOwner;
  }

  return !out_results.m_Results.IsEmpty();
}

bool plAiTestPhysicsWorldModule::OverlapTestBox(const plVec3& vBoxExtents, const plVec3& vPosition, const plTransform& transform, const plPhysicsQueryParameters& params) const
{
  const plBoundingBox query = plBoundingBox::MakeFromCenterAndHalfExtents(vPosition, vBoxExtents * 0.5f);

  for (const Box& box : m_Boxes)
  {
    if (params.m_ShapeTypes.IsAnySet(box.m_Type) && box.m_Box.Overlaps(query))
      return true;
  }

  return false;
}

plBoundingBoxSphere plAiTestPhysicsWorldModule::GetWorldSpaceBounds(plGameObject* pOwner, plUInt32 uiCollisionLayer, plBitflags<plPhysicsShapeType> shapeTypes, bool bIncludeChildObjects) const
{
  for (const Box& box : m_Boxes)
  {
    if (box.m_hOwner == pOwner->GetHandle() && shapeTypes.IsAnySet(box.m_Type))
      return plBoundingBoxSphere::MakeFromBox(box.m_Box);
  }

  return plBoundingBoxSphere::MakeInvalid();
}
