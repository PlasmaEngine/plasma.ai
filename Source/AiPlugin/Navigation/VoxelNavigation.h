#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Types/UniquePtr.h>

class plVoxelGrid;
class plDebugRendererContext;

/// A* pathfinding through an plVoxelGrid.
///
/// Uses 26-connected neighbors. A diagonal step needs the voxels on its sides free as well, so paths
/// never squeeze between two occupied corners. With an agent radius, paths keep that much clearance
/// from occupied voxels (the grid's clearance field), except right at the start and target.
/// After finding a raw voxel path, applies line-of-sight string-pulling with the same clearance.
///
/// Searches run incrementally: StartPathSearch, then ContinuePathSearch with a node budget each frame.
/// The current path stays usable until a new search succeeds.
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
    Searching, ///< a search is in progress; call ContinuePathSearch
  };

  void SetVoxelGrid(const plVoxelGrid* pGrid);

  /// Begins a search from vStart to vTarget for an agent with the given radius. Returns Searching, or why it can't search.
  State StartPathSearch(const plVec3& vStart, const plVec3& vTarget, float fAgentRadius = 0.0f);

  /// Expands up to uiMaxNodes A* nodes. Returns Searching until the search finished.
  ///
  /// A search that expands more than uiMaxTotalNodes nodes in total gives up with NoPathFound.
  State ContinuePathSearch(plUInt32 uiMaxNodes, plUInt32 uiMaxTotalNodes = 200000);

  /// Whether a search is in progress.
  bool IsSearching() const { return m_pSearch != nullptr; }

  /// The grid generation the current path was computed for.
  plUInt32 GetPathGeneration() const { return m_uiPathGeneration; }

  /// Computes a path in one go (StartPathSearch plus ContinuePathSearch with uiMaxIterations).
  State FindPath(const plVec3& vStart, const plVec3& vTarget, plUInt32 uiMaxIterations, float fAgentRadius);

  /// FindPath for a point-sized agent. Kept for plugins built against 1.2.0 before the agent radius existed.
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

  /// The voxel clearance an agent of the given radius needs (see plVoxelGrid::GetClearance).
  static plUInt8 GetRequiredClearance(float fAgentRadius, float fVoxelSize);

private:
  struct SearchState;

  void FinishSearch(State result);
  void SmoothPath(plDynamicArray<plVec3>& inout_waypoints, plUInt8 uiClearance) const;
  bool IsSegmentClear(const plVec3& vFrom, const plVec3& vTo, plUInt8 uiClearance) const;

  const plVoxelGrid* m_pGrid = nullptr;
  State m_State = State::Idle;
  plDynamicArray<plVec3> m_Waypoints;
  plUInt32 m_uiCurrentWaypoint = 0;
  plUInt32 m_uiPathGeneration = 0;
  plUniquePtr<SearchState> m_pSearch;
};
