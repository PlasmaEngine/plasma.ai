#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/VoxelGrid.h>
#include <Core/World/WorldModule.h>
#include <Foundation/Containers/HashTable.h>

/// World module that manages a voxel grid for 3D navigation.
///
/// The grid combines two layers: static collision geometry, voxelized once from physics, and obstacles
/// registered with AddObstacle (doors, platforms, spawned barriers). Obstacles are reference counted per
/// voxel, so removing one never clears static geometry or another obstacle. Obstacles may be added
/// before the grid is built; they are applied once it is.
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

  /// The combined grid (static geometry plus obstacles) with its clearance field.
  plVoxelGrid* GetVoxelGrid() { return &m_VoxelGrid; }
  const plVoxelGrid* GetVoxelGrid() const { return &m_VoxelGrid; }

  /// Returns true once the grid has been voxelized from physics. Stays false in a world without physics.
  bool IsReady() const { return m_bIsReady; }

  /// Voxelizes the static collision geometry of the given layer. Returns false without a physics module.
  ///
  /// Expensive for large grids; runs once when the grid is first needed.
  bool VoxelizeWorld(plUInt32 uiCollisionLayer);

  using ObstacleID = plUInt32;
  static constexpr ObstacleID InvalidObstacleID = plInvalidIndex;

  /// Marks the voxels overlapping the box as occupied until the obstacle is removed or moved.
  ObstacleID AddObstacle(const plBoundingBox& box);

  /// Moves an obstacle to a new box.
  void MoveObstacle(ObstacleID id, const plBoundingBox& box);

  /// Frees the voxels the obstacle occupied, unless static geometry or another obstacle also occupies them.
  void RemoveObstacle(ObstacleID id);

  /// \name Obstacles by box, kept for plugins built against 1.2.0 before obstacle IDs existed.
  /// InjectObstacle adds an obstacle; RemoveObstacle removes one that was added with an equal box.
  ///@{
  void InjectObstacle(const plBoundingBox& box);
  void RemoveObstacle(const plBoundingBox& box);
  ///@}

  /// \brief Grants up to uiWanted A* node expansions from this frame's shared budget.
  ///
  /// Each search gets at most AI.VoxelNav.MaxNodesPerSearch per frame; all searches together at most
  /// AI.VoxelNav.MaxNodesPerFrame.
  plUInt32 TakeSearchBudget(plUInt32 uiWanted);

  plUInt32 m_uiResolutionX = 128;
  plUInt32 m_uiResolutionY = 128;
  plUInt32 m_uiResolutionZ = 64;
  float m_fVoxelSize = 0.5f;
  plVec3 m_vGridCenter = plVec3::MakeZero();
  plUInt32 m_uiCollisionLayer = 0;

private:
  void Update(const UpdateContext& ctxt);
  void BuildGrid();
  void ApplyObstacle(const plBoundingBox& box, bool bAdd); ///< updates the reference counts and the combined grid

  struct Obstacle
  {
    plBoundingBox m_Box;
    bool m_bInUse = false;
  };

  plVoxelGrid m_StaticGrid;                         ///< voxelized collision geometry only
  plVoxelGrid m_VoxelGrid;                          ///< static geometry plus obstacles
  plHashTable<plUInt32, plUInt16> m_ObstacleCounts; ///< voxel index -> number of obstacles covering it
  plDynamicArray<Obstacle> m_Obstacles;
  plDynamicArray<ObstacleID> m_FreeObstacles;

  bool m_bNeedsVoxelization = true;
  bool m_bIsReady = false;
  bool m_bWarnedNoPhysics = false;
  plUInt32 m_uiUpdateDelay = 10;
  plInt32 m_iSearchBudget = 0;
};
