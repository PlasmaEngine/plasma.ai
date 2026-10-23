#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/Component.h>
#include <Core/World/ComponentManager.h>
#include <Foundation/Math/BoundingBox.h>

using plAiVoxelObstacleComponentManager = plComponentManager<class plAiVoxelObstacleComponent, plBlockStorageType::Compact>;

/// Represents a dynamic obstacle in the voxel navigation grid.
///
/// When activated, marks its owner's physics bounds as occupied in the voxel grid. When deactivated,
/// frees them again, unless static geometry or another obstacle also occupies them. Useful for doors,
/// moving platforms, or spawned barriers. The grid voxelizes only static shapes, so anything that
/// moves and should block flying agents needs this component.
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

  plUInt32 m_ObstacleID = plInvalidIndex; ///< registration with plAiVoxelWorldModule
};
