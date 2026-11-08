#pragma once

#include <AiPlugin/Navigation/NavMeshWorldModule.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>

using plAiNavBlockerComponentManager = plComponentManagerSimple<class plAiNavBlockerComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Blocks (and releases) navmesh polygons inside a box volume at runtime - without any rebuild.
///
/// The classic use case is a door: place this component on the door with the box covering the
/// doorway, toggle SetBlocked() when the door opens/closes, and agents instantly re-path.
///
/// Mode 'Block' toggles the 'Blocked' polygon flag which all path search filters exclude.
/// Mode 'Avoid' is a soft variant: the polys stay walkable but cost extra to cross
/// (AI.Navmesh.AvoidZoneCost), so agents route around when a reasonable detour exists -
/// good for danger zones, fire, turret sightlines etc.
/// In both modes no navmesh data is rebuilt, so toggling takes effect within a frame or two,
/// and the navmesh SHAPE stays unchanged.
///
/// Mode 'Carve' cuts the box out of the navmesh, like solid geometry (agents keep their radius
/// away from it). No collision geometry is needed - use it to keep agents out of an area for good.
/// Toggling or moving a carve rebuilds the overlapping sectors (~100ms).
///
/// The volume acts on floor surfaces inside it or up to 0.5 m below its bottom.
class PL_AIPLUGIN_DLL plAiNavBlockerComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiNavBlockerComponent, plComponent, plAiNavBlockerComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  //////////////////////////////////////////////////////////////////////////
  // plAiNavBlockerComponent

public:
  plAiNavBlockerComponent();
  ~plAiNavBlockerComponent();

  /// \brief Whether the volume is currently active. Toggle at runtime for doors etc.
  void SetBlocked(bool bBlocked); // [ property ] [ scriptable ]
  bool GetBlocked() const { return m_bBlocked; }

  plVec3 m_vBoxSize = plVec3(1.0f);          ///< [ property ] Size of the volume, centered on the owner.
  plEnum<plAiNavBlockerMode> m_Mode;         ///< [ property ] Hard block, soft (high path cost) avoidance, or a hole in the navmesh.

protected:
  void Update();

  plBoundingBox GetWorldBox() const;

  bool m_bBlocked = true;
  plAiNavMeshWorldModule* m_pNavMeshModule = nullptr;
  plAiNavMeshWorldModule::BlockerID m_BlockerID = plAiNavMeshWorldModule::InvalidBlockerID;
  plVec3 m_vLastPosition = plVec3::MakeZero();
};
