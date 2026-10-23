#pragma once

#include <Core/Interfaces/NavmeshGeoWorldModule.h>
#include <Core/Interfaces/PhysicsWorldModule.h>
#include <Foundation/Math/BoundingBox.h>

class plSurfaceResource;

/// \brief Navmesh input geometry made of flat floor rectangles, for tests without a physics engine.
class plAiTestNavGeoWorldModule : public plNavmeshGeoWorldModuleInterface
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiTestNavGeoWorldModule, plNavmeshGeoWorldModuleInterface);

public:
  plAiTestNavGeoWorldModule(plWorld* pWorld);

  /// Adds a horizontal floor rectangle from vMin to vMax (x, y) at height fZ, optionally of a surface (its ground type).
  void AddFloor(const plVec2& vMin, const plVec2& vMax, float fZ, const plSurfaceResource* pSurface = nullptr);

  virtual void RetrieveGeometryInArea(plUInt32 uiCollisionLayer, const plBoundingBox& box, plDynamicArray<plNavmeshTriangle>& out_triangles) const override;

private:
  struct Floor
  {
    plBoundingBox m_Box;
    const plSurfaceResource* m_pSurface = nullptr;
  };

  plDynamicArray<Floor> m_Floors;
};

/// \brief A physics world made of axis-aligned boxes, for tests that need physics queries without a physics engine.
///
/// Answers raycasts, box overlap tests and object bounds against the added boxes; every other query
/// reports nothing.
class plAiTestPhysicsWorldModule : public plPhysicsWorldModuleInterface
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiTestPhysicsWorldModule, plPhysicsWorldModuleInterface);

public:
  plAiTestPhysicsWorldModule(plWorld* pWorld);

  /// Adds a box; static boxes are voxelized, dynamic ones are reported as the bounds of their owner.
  void AddBox(const plBoundingBox& box, plPhysicsShapeType::Enum type, const plGameObjectHandle& hOwner = plGameObjectHandle());

  virtual plUInt32 GetCollisionLayerByName(plStringView sName) const override { return 0; }
  virtual plUInt8 GetWeightCategoryByName(plStringView sName) const override { return 0; }
  virtual plUInt8 GetImpulseTypeByName(plStringView sName) const override { return 0; }

  virtual bool Raycast(plPhysicsCastResult& out_result, const plVec3& vStart, const plVec3& vDir, float fDistance, const plPhysicsQueryParameters& params, plPhysicsHitCollection collection) const override;
  virtual bool RaycastAll(plPhysicsCastResultArray& out_results, const plVec3& vStart, const plVec3& vDir, float fDistance, const plPhysicsQueryParameters& params) const override;
  virtual bool OverlapTestBox(const plVec3& vBoxExtents, const plVec3& vPosition, const plTransform& transform, const plPhysicsQueryParameters& params) const override;
  virtual plBoundingBoxSphere GetWorldSpaceBounds(plGameObject* pOwner, plUInt32 uiCollisionLayer, plBitflags<plPhysicsShapeType> shapeTypes, bool bIncludeChildObjects) const override;

  virtual bool SweepTestSphere(plPhysicsCastResult&, float, const plVec3&, const plVec3&, float, const plPhysicsQueryParameters&, plPhysicsHitCollection) const override { return false; }
  virtual bool SweepTestBox(plPhysicsCastResult&, const plVec3&, const plTransform&, const plVec3&, float, const plPhysicsQueryParameters&, plPhysicsHitCollection) const override { return false; }
  virtual bool SweepTestCapsule(plPhysicsCastResult&, float, float, const plTransform&, const plVec3&, float, const plPhysicsQueryParameters&, plPhysicsHitCollection) const override { return false; }
  virtual bool SweepTestCylinder(plPhysicsCastResult&, float, float, const plTransform&, const plVec3&, float, const plPhysicsQueryParameters&, plPhysicsHitCollection) const override { return false; }
  virtual bool OverlapTestSphere(float, const plVec3&, const plPhysicsQueryParameters&) const override { return false; }
  virtual bool OverlapTestCapsule(float, float, const plTransform&, const plPhysicsQueryParameters&) const override { return false; }
  virtual bool OverlapTestCylinder(float, float, const plTransform&, const plPhysicsQueryParameters&) const override { return false; }
  virtual void QueryShapesInSphere(plPhysicsOverlapResultArray&, float, const plVec3&, const plPhysicsQueryParameters&) const override {}
  virtual void QueryShapesInBox(plPhysicsOverlapResultArray&, const plVec3&, const plTransform&, const plPhysicsQueryParameters&) const override {}
  virtual void QueryShapesInCapsule(plPhysicsOverlapResultArray&, float, float, const plTransform&, const plPhysicsQueryParameters&) const override {}
  virtual void QueryShapesInCylinder(plPhysicsOverlapResultArray&, float, float, const plTransform&, const plPhysicsQueryParameters&) const override {}
  virtual plVec3 GetGravity() const override { return plVec3(0, 0, -10); }

private:
  struct Box
  {
    plBoundingBox m_Box;
    plPhysicsShapeType::Enum m_Type;
    plGameObjectHandle m_hOwner;
  };

  plDynamicArray<Box> m_Boxes;
};
