#pragma once

#include <AiPlugin/Navigation/NavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourPathCorridor.h>
#include <Foundation/Math/Angle.h>
#include <Foundation/Math/Vec3.h>

class plDebugRendererContext;

/// \brief Aggregated data by plAiNavigation that should be sufficient to implement a steering behavior.
struct plAiSteeringInfo
{
  plVec3 m_vNextWaypoint;
  float m_fDistanceToWaypoint = 0;
  float m_fArrivalDistance = plMath::HighValue<float>();
  plVec2 m_vDirectionTowardsWaypoint = plVec2::MakeZero();
  plAngle m_AbsRotationTowardsWaypoint = plAngle::MakeZero();
  plAngle m_MaxAbsRotationAfterWaypoint = plAngle::MakeZero();
  // float m_fWaypointCorridorWidth = plMath::HighValue<float>();

  /// True when the next waypoint is the start of an off-mesh connection (nav link).
  /// Once close enough, call plAiNavigation::TraverseOffMeshLink() with m_LinkPolyRef.
  bool m_bNextWaypointIsLink = false;
  dtPolyRef m_LinkPolyRef = 0;
};

/// \brief Computes a path through a navigation mesh.
///
/// First call SetNavmesh() and SetQueryFilter().
///
/// When you need a path, call SetCurrentPosition() and SetTargetPosition() to inform the
/// system of the current position and desired target location.
/// Then call Update() once per frame to have it compute the path.
/// Call GetState() to figure out whether a path exists.
/// Use ComputeAllWaypoints() to get an entire path, e.g. for visualization.
/// For steering this is not necessary. Instead use ComputeSteeringInfo() to plan the next step.
/// Apply your steering behavior to your character as desired.
/// Keep calling SetCurrentPosition() and SetTargetPosition() to inform the plAiNavigation of the
/// new state, and keep calling ComputeSteeringInfo() every frame for the updated path.
///
/// If the destination was reached, a completely different path should be computed, or the current
/// path should be canceled, call CancelNavigation().
/// To start a new path search, call SetTargetPosition() again (and Update() every frame).
class PL_AIPLUGIN_DLL plAiNavigation final
{
public:
  plAiNavigation();
  ~plAiNavigation();

  enum class State
  {
    Idle,
    StartNewSearch,
    InvalidCurrentPosition,
    InvalidTargetPosition,
    NoPathFound,
    PartialPathFound,
    FullPathFound,
    Searching,
  };

  static constexpr plUInt32 MaxPathNodes = 64;
  static constexpr plUInt32 MaxSearchNodes = MaxPathNodes * 8;

  State GetState() const { return m_State; }

  void Update();

  void CancelNavigation();

  void SetCurrentPosition(const plVec3& vPosition);
  void SetTargetPosition(const plVec3& vPosition);
  const plVec3& GetTargetPosition() const;
  void SetNavmesh(plAiNavMesh* pNavmesh);
  void SetQueryFilter(const dtQueryFilter& filter);

  void ComputeAllWaypoints(plDynamicArray<plVec3>& out_waypoints) const;

  void DebugDrawPathCorridor(const plDebugRendererContext& context, plColor tilesColor, float fPolyRenderOffsetZ = 0.1f);
  void DebugDrawPathLine(const plDebugRendererContext& context, plColor straightLineColor, float fLineRenderOffsetZ = 0.2f);
  void DebugDrawState(const plDebugRendererContext& context, const plVec3& vPosition) const;


  /// \brief Returns the height of the navmesh at the current position.
  float GetCurrentElevation() const;

  /// \brief The navmesh polygon the corridor currently starts on (0 if there is no corridor).
  dtPolyRef GetCurrentPolyRef() const { return m_PathCorridor.getFirstPoly(); }

  /// \brief The navmesh this navigation currently operates on.
  plAiNavMesh* GetNavmesh() const { return m_pNavmesh; }

  /// \brief The path search filter in use. Points into the nav world module's filter map (stable).
  const dtQueryFilter* GetFilter() const { return m_pFilter; }

  void ComputeSteeringInfo(plAiSteeringInfo& out_info, const plVec2& vForwardDir, float fMaxLookAhead = 5.0f);

  /// \brief Moves the path corridor across an off-mesh connection (nav link).
  ///
  /// Call when the steering info reported a link waypoint and the agent is close enough to it.
  /// On success, the corridor continues at the link's end position: move the character from
  /// out_vStart to out_vEnd by game means (animation, lerp), then resume normal steering.
  /// out_uiUserID identifies the link in plAiNavMeshWorldModule::ResolveNavLink().
  plResult TraverseOffMeshLink(dtPolyRef linkPoly, plVec3& out_vStart, plVec3& out_vEnd, plUInt32& out_uiUserID);

  // in what radius / up / down distance navigation mesh polygons should be searched around a given position
  // this should relate to the character size, ie at least the character radius
  // otherwise a character that barely left the navmesh area may not know where it is, anymore
  float m_fPolySearchRadius = 0.5f;
  float m_fPolySearchUp = 1.5f;
  float m_fPolySearchDown = 1.5f;

  // when a path search is started, all tiles in a rectangle around the start and end point are loaded first
  // this is the amount to increase that rectangle size, to overestimate which sectors may be needed during the path search
  constexpr static float c_fPathSearchBoundary = 10.0f;


private:
  State m_State = State::Idle;

  plVec3 m_vCurrentPosition = plVec3::MakeZero();
  plVec3 m_vTargetPosition = plVec3::MakeZero();

  plUInt8 m_uiCurrentPositionChangedBit : 1;
  plUInt8 m_uiTargetPositionChangedBit : 1;
  plUInt8 m_uiReinitQueryBit : 1;

  plAiNavMesh* m_pNavmesh = nullptr;
  dtNavMeshQuery m_Query;
  const dtQueryFilter* m_pFilter = nullptr;
  dtPathCorridor m_PathCorridor;

  dtPolyRef m_PathSearchTargetPoly;
  plVec3 m_vPathSearchTargetPos;

  plUInt8 m_uiOptimizeTopologyCounter = 0;
  plUInt8 m_uiOptimizeVisibilityCounter = 0;

  /// last seen plAiNavMesh::GetNavMeshGeneration() - when it changes, tiles were added/removed
  /// and the entire corridor must be re-validated (cached poly refs may be stale)
  plUInt32 m_uiLastNavMeshGeneration = 0;

  bool UpdatePathSearch();
};
