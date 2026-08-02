#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/VoxelGrid.h>
#include <Core/World/WorldModule.h>

/// World module that manages a voxel grid for 3D navigation.
///
/// Provides the voxel grid to navigation components.
/// Can voxelize the world from physics collision geometry on demand.
///
/// Access it via GetWorld()->GetOrCreateModule<plAiVoxelWorldModule>().
class PL_AIPLUGIN_DLL plAiVoxelWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiVoxelWorldModule, plWorldModule);

public:
  plAiVoxelWorldModule(plWorld* pWorld);
  ~plAiVoxelWorldModule();

  virtual void Initialize() override;
  virtual void Deinitialize() override;

  plVoxelGrid* GetVoxelGrid() { return &m_VoxelGrid; }
  const plVoxelGrid* GetVoxelGrid() const { return &m_VoxelGrid; }

  /// Returns true once the grid has been voxelized and is ready for pathfinding.
  bool IsReady() const { return m_bIsReady; }

  /// Triggers voxelization of the world using physics overlap tests.
  ///
  /// This iterates every voxel cell and does a box overlap test to determine occupancy.
  /// Expensive for large grids. Should be called once at startup or when the world changes significantly.
  void VoxelizeWorld(plUInt32 uiCollisionLayer);

  /// Injects a box obstacle into the voxel grid.
  void InjectObstacle(const plBoundingBox& box);

  /// Removes a box obstacle from the voxel grid.
  void RemoveObstacle(const plBoundingBox& box);

  plUInt32 m_uiResolutionX = 128;
  plUInt32 m_uiResolutionY = 128;
  plUInt32 m_uiResolutionZ = 64;
  float m_fVoxelSize = 0.5f;
  plVec3 m_vGridCenter = plVec3::MakeZero();
  plUInt32 m_uiCollisionLayer = 0;

private:
  void Update(const UpdateContext& ctxt);

  plVoxelGrid m_VoxelGrid;
  bool m_bNeedsVoxelization = true;
  bool m_bIsReady = false;
  plUInt32 m_uiUpdateDelay = 10;
};
