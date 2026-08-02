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
/// Because no navmesh data is rebuilt, toggling takes effect within a frame or two.
/// The navmesh SHAPE stays unchanged - use plNavMeshObstacleComponent when walkable space
/// itself changes (movers, destructibles).
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
  plEnum<plAiNavObstacleMode> m_Mode;        ///< [ property ] Hard block or soft (high path cost) avoidance.

protected:
  void Update();

  plBoundingBox GetWorldBox() const;

  bool m_bBlocked = true;
  plAiNavMeshWorldModule* m_pNavMeshModule = nullptr;
  plAiNavMeshWorldModule::BlockerID m_BlockerID = plAiNavMeshWorldModule::InvalidBlockerID;
  plVec3 m_vLastPosition = plVec3::MakeZero();
};
