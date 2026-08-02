#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/BoundingBox.h>
#include <Foundation/Math/BoundingSphere.h>
#include <Foundation/Math/Vec3.h>

class plDebugRendererContext;

/// Stores a 3D voxel grid using packed 4x4x4 bit blocks.
///
/// Each block is stored as a single plUInt64 (64 bits = 4*4*4 voxels).
/// A set bit means the voxel is occupied (solid). A cleared bit means the voxel is free (passable).
///
/// The grid has a fixed resolution and is centered at a configurable world position.
/// Use WorldToCoord / CoordToWorld to convert between world space and voxel coordinates.
/// Use InjectBox / InjectSphere to mark voxels as occupied based on world-space shapes.
class PL_AIPLUGIN_DLL plVoxelGrid
{
public:
  plVoxelGrid();
  ~plVoxelGrid();

  /// Initializes the grid with the given resolution (in voxels).
  ///
  /// Each dimension is rounded up to a multiple of 4 internally.
  /// The grid is cleared to all-free after init.
  void Init(plUInt32 uiDimX, plUInt32 uiDimY, plUInt32 uiDimZ);

  /// Clears all voxel data (sets everything to free). Keeps allocated memory.
  void ClearData();

  /// Sets the world-space center and per-voxel size. Call after Init.
  void SetWorldParameters(const plVec3& vCenter, float fVoxelSize);

  /// Converts a world-space position to integer voxel coordinates.
  /// Returns false if the position is outside the grid.
  bool WorldToCoord(const plVec3& vWorldPos, plVec3I32& out_vCoord) const;

  /// Converts integer voxel coordinates to the world-space center of that voxel.
  plVec3 CoordToWorld(const plVec3I32& vCoord) const;

  /// Returns true if the coordinate is inside the grid bounds.
  bool IsCoordValid(const plVec3I32& vCoord) const;

  /// Sets a voxel to occupied (true) or free (false).
  void SetVoxel(const plVec3I32& vCoord, bool bSolid);

  /// Atomically marks a voxel as occupied (solid).
  ///
  /// Thread-safe: multiple threads may call this concurrently, even on voxels that share the same
  /// internal 4x4x4 bit block. Only ever sets bits (never clears), which is all the parallel
  /// voxelization needs. Do not mix with non-atomic SetVoxel on the same data concurrently.
  void SetVoxelThreadSafe(const plVec3I32& vCoord);

  /// Returns true if the voxel at the given coordinate is occupied.
  bool CheckVoxel(const plVec3I32& vCoord) const;

  /// Marks all voxels overlapping the given world-space AABB as occupied.
  void InjectBox(const plBoundingBox& box);

  /// Marks all voxels overlapping the given world-space sphere as occupied.
  void InjectSphere(const plVec3& vCenter, float fRadius);

  /// Clears all voxels overlapping the given world-space AABB (sets to free).
  void SubtractBox(const plBoundingBox& box);

  /// Ray-march visibility test between two world-space points.
  ///
  /// Returns true if no occupied voxel blocks the line of sight.
  bool IsVisible(const plVec3& vObserver, const plVec3& vSubject) const;

  /// Returns the world-space AABB of the entire grid.
  plBoundingBox GetAABB() const;

  /// Returns the approximate memory usage in bytes.
  plUInt64 GetMemoryUsage() const;

  /// Draws a debug visualization of occupied surface voxels.
  void DebugDraw(const plDebugRendererContext& context, const plColor& color) const;

  plUInt32 GetDimX() const { return m_uiDimX; }
  plUInt32 GetDimY() const { return m_uiDimY; }
  plUInt32 GetDimZ() const { return m_uiDimZ; }
  float GetVoxelSize() const { return m_fVoxelSize; }
  const plVec3& GetCenter() const { return m_vCenter; }
  bool IsInitialized() const { return !m_Blocks.IsEmpty(); }

private:
  plUInt32 GetBlockIndex(plUInt32 uiBlockX, plUInt32 uiBlockY, plUInt32 uiBlockZ) const;
  static plUInt32 GetBitIndex(plUInt32 uiLocalX, plUInt32 uiLocalY, plUInt32 uiLocalZ);

  bool IsVisibleCoord(const plVec3I32& vStart, const plVec3I32& vGoal) const;

  plUInt32 m_uiDimX = 0;
  plUInt32 m_uiDimY = 0;
  plUInt32 m_uiDimZ = 0;
  plUInt32 m_uiBlocksX = 0;
  plUInt32 m_uiBlocksY = 0;
  plUInt32 m_uiBlocksZ = 0;

  float m_fVoxelSize = 1.0f;
  float m_fInvVoxelSize = 1.0f;
  plVec3 m_vCenter = plVec3::MakeZero();
  plVec3 m_vOrigin = plVec3::MakeZero();

  plDynamicArray<plUInt64> m_Blocks;
};
