#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Vec3.h>

class plVoxelGrid;
class plDebugRendererContext;

/// A* pathfinding through an plVoxelGrid.
///
/// Uses 26-connected neighbors (including diagonals) for smooth paths.
/// After finding a raw voxel path, applies line-of-sight string-pulling
/// to remove unnecessary waypoints.
class PL_AIPLUGIN_DLL plAiVoxelNavigation
{
public:
  plAiVoxelNavigation();
  ~plAiVoxelNavigation();

  enum class State
  {
    Idle,
    PathFound,
    NoPathFound,
    InvalidStartPosition,
    InvalidTargetPosition,
  };

  void SetVoxelGrid(const plVoxelGrid* pGrid);

  /// Computes a path from vStart to vTarget through free voxels.
  ///
  /// uiMaxIterations limits the A* node expansion to prevent frame stalls.
  State FindPath(const plVec3& vStart, const plVec3& vTarget, plUInt32 uiMaxIterations = 10000);

  const plDynamicArray<plVec3>& GetWaypoints() const { return m_Waypoints; }

  plUInt32 GetCurrentWaypointIndex() const { return m_uiCurrentWaypoint; }

  /// Advances to the next waypoint. Returns true if there are more waypoints.
  bool AdvanceWaypoint();

  /// Returns the next waypoint to move towards.
  plVec3 GetNextWaypoint() const;

  /// Returns true if the end of the path has been reached.
  bool IsPathComplete() const;

  void CancelNavigation();

  State GetState() const { return m_State; }

  /// Draws the path as a line strip.
  void DebugDrawPath(const plDebugRendererContext& context, const plColor& color) const;

private:
  void SmoothPath(plDynamicArray<plVec3>& inout_waypoints) const;

  const plVoxelGrid* m_pGrid = nullptr;
  State m_State = State::Idle;
  plDynamicArray<plVec3> m_Waypoints;
  plUInt32 m_uiCurrentWaypoint = 0;
};
