#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/Component.h>
#include <Core/World/ComponentManager.h>
#include <Foundation/Math/BoundingBox.h>

using plAiVoxelObstacleComponentManager = plComponentManager<class plAiVoxelObstacleComponent, plBlockStorageType::Compact>;

/// Represents a dynamic obstacle in the voxel navigation grid.
///
/// When activated, injects its owner's bounding box into the voxel grid as occupied.
/// When deactivated, removes it. Useful for doors, moving platforms, or spawned barriers.
class PL_AIPLUGIN_DLL plAiVoxelObstacleComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plAiVoxelObstacleComponent, plComponent, plAiVoxelObstacleComponentManager);

public:
  plAiVoxelObstacleComponent();
  ~plAiVoxelObstacleComponent();

  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

  /// Manually re-inject the obstacle, e.g. after moving it.
  void UpdateObstacle(); ///< [ scriptable ]

  plUInt32 m_uiCollisionLayer = 0; ///< [ property ]

protected:
  virtual void OnActivated() override;
  virtual void OnSimulationStarted() override;
  virtual void OnDeactivated() override;

private:
  void InjectIntoGrid();
  void RemoveFromGrid();

  plBoundingBox m_LastInjectedBounds;
  bool m_bInjected = false;
};
