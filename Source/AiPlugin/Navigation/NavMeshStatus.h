#pragma once

#include <Foundation/Math/Vec2.h>
#include <Foundation/Time/Time.h>

// What the navmesh reports about its sectors, without Detour and Recast: the sector views, the stats box and the editor's
// AI Debugger use these.

/// \brief Why a navmesh sector was (re)built.
enum class plAiNavMeshRebuildReason : plUInt8
{
  None,       ///< never built
  FirstBuild, ///< requested for the first time
  Obstacle,   ///< an obstacle, carve or blocker changed
  NavLink,    ///< a nav link was added, moved or removed
  Requested,  ///< game code or a script asked for it
  Changed,    ///< something else invalidated it
};

/// \brief What a navmesh sector is doing, for status displays.
enum class plAiNavMeshSectorState : plUInt8
{
  Queued,     ///< requested, waiting for a build slot
  Building,   ///< a first build is running
  Ready,      ///< built and in use
  Rebuilding, ///< in use while a rebuild is queued or running
  Failed,     ///< the last build failed; the sector has no polygons
};

inline const char* plAiNavMeshSectorStateName(plAiNavMeshSectorState state)
{
  switch (state)
  {
    case plAiNavMeshSectorState::Queued:
      return "Queued";
    case plAiNavMeshSectorState::Building:
      return "Building";
    case plAiNavMeshSectorState::Ready:
      return "Ready";
    case plAiNavMeshSectorState::Rebuilding:
      return "Rebuilding";
    case plAiNavMeshSectorState::Failed:
      return "Failed";
  }

  return "";
}

inline const char* plAiNavMeshRebuildReasonName(plAiNavMeshRebuildReason reason)
{
  switch (reason)
  {
    case plAiNavMeshRebuildReason::None:
      return "None";
    case plAiNavMeshRebuildReason::FirstBuild:
      return "Initial";
    case plAiNavMeshRebuildReason::Obstacle:
      return "Obstacle";
    case plAiNavMeshRebuildReason::NavLink:
      return "NavLink";
    case plAiNavMeshRebuildReason::Requested:
      return "Requested";
    case plAiNavMeshRebuildReason::Changed:
      return "Changed";
  }

  return "";
}

/// \brief Whether a sector's polygon count is close to the budget the density view and the warning use: 85 % of it.
inline bool plAiIsNearPolyBudget(plUInt32 uiPolyCount, plUInt32 uiPolyBudget)
{
  return uiPolyBudget > 0 && uiPolyCount * 100ull >= uiPolyBudget * 85ull;
}

/// \brief One sector's status, for the navmesh views and the AI Debugger.
struct plAiNavMeshSectorStatus
{
  plUInt32 m_uiSectorID = 0;
  plVec2I32 m_vCoord = plVec2I32::MakeZero();
  plAiNavMeshSectorState m_State = plAiNavMeshSectorState::Queued;
  plAiNavMeshRebuildReason m_LastReason = plAiNavMeshRebuildReason::None;
  plUInt32 m_uiPolyCount = 0;
  plUInt32 m_uiBuildCount = 0;
  plTime m_LastBuildDuration;

  /// While a build runs: how far it probably is, from the time the sector's last build took. Never reaches 1 before the
  /// build is done.
  float m_fBuildProgress = 0.0f;

  /// A build of the sector runs now, as opposed to waiting for a build slot.
  bool m_bBuildRunning = false;

  /// Height range of the sector's tile, valid with m_bHasBounds. A sector that was never built has none.
  float m_fMinZ = 0.0f;
  float m_fMaxZ = 0.0f;
  bool m_bHasBounds = false;
};

/// \brief What the navmesh view draws.
struct plAiNavMeshVisualizeMode
{
  using StorageType = plUInt8;

  enum Enum : plUInt8
  {
    Polygons,       ///< the polygons, as before
    SectorStatus,   ///< each sector by its state
    RebuildReason,  ///< each sector by why it was last built
    PolygonDensity, ///< each sector by its polygon count against the budget

    Count,
    Default = Polygons
  };
};

/// \brief The counts of the navmesh stats box.
struct plAiNavMeshStats
{
  plUInt32 m_uiSectors = 0;
  plUInt32 m_uiReady = 0;
  plUInt32 m_uiQueued = 0;
  plUInt32 m_uiBuilding = 0; ///< builds that run now
  plUInt32 m_uiFailed = 0;
  plUInt32 m_uiNearPolyBudget = 0;
  float m_fAverageBuildMs = 0.0f; ///< of the sectors that were built
  plUInt32 m_uiPolyBudget = 0;
};
