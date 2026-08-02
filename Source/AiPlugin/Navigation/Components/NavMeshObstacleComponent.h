#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/NavigationConfig.h>
#include <Core/World/Component.h>
#include <Core/World/ComponentManager.h>
#include <Foundation/Math/BoundingBox.h>

using plNavMeshObstacleComponentManager = plComponentManagerSimple<class plNavMeshObstacleComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Represents an obstacle on a navmesh whose physics geometry changes the walkable space.
///
/// Automatically notifies the navmesh that sectors overlapping this object must be rebuilt.
///
/// Attach this to objects that are dynamically spawned or moved, but need to affect the navmesh,
/// blocking or unblocking pathing. For example: a wall - the obstacle component makes sure the
/// navmesh is carved around it, so all path queries go around it. Another example is a bridge
/// that connects two navmesh islands, allowing a direct path between the two.
///
/// Works with static AND dynamic (moving) game objects. For moving objects, the overlapped
/// sectors are re-built asynchronously whenever the object moved far enough (MoveThreshold),
/// rate-limited by ReinvalidateCooldown. Expect ~100ms until the navmesh reflects a move.
///
/// Static owners affect the navmesh through their collision geometry directly. Dynamic owners
/// (physics crates etc.) are NOT part of the navmesh generation input - for those, this component
/// registers its physics bounds as a carve volume that the sector build stamps unwalkable.
///
/// Mode 'Block' makes the volume impassable (the default). Mode 'Avoid' keeps it walkable but
/// expensive to cross (AI.Navmesh.AvoidZoneCost) - agents route around it when a reasonable
/// detour exists. Avoid mode also works on static owners, e.g. as a soft keep-away margin
/// around machinery.
///
/// For togglable, shape-preserving blockers (doors), use plAiNavBlockerComponent instead -
/// it takes effect instantly, without any rebuild.
class PL_AIPLUGIN_DLL plNavMeshObstacleComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plNavMeshObstacleComponent, plComponent, plNavMeshObstacleComponentManager);

public:
  plNavMeshObstacleComponent();
  ~plNavMeshObstacleComponent();

  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnActivated() override;
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

  void Update();

public:
  /// \brief Invalidates all navmesh sectors overlapping this obstacle's physics geometry.
  void InvalidateSectors(); // [ scriptable ]

  float m_fMoveThreshold = 0.5f;                          ///< [ property ] How far a dynamic obstacle must move before affected sectors are rebuilt.
  plTime m_ReinvalidateCooldown = plTime::Seconds(0.5);   ///< [ property ] Minimum time between rebuilds triggered by movement.
  plEnum<plAiNavObstacleMode> m_Mode;                     ///< [ property ] Hard block (carve) or soft (high path cost) avoidance.

private:
  void InvalidateObstacleBounds(bool bRebuildAsSoonAsPossible);
  plBoundingBox QueryCurrentBounds();
  void RefreshCarveVolume();

  plBoundingBox m_LastInvalidatedBounds = plBoundingBox::MakeInvalid();
  plTime m_NextAllowedReinvalidate;
  bool m_bPendingMove = false;
  plUInt32 m_uiCarveID = plInvalidIndex; ///< carve volume registered for dynamic owners
};
