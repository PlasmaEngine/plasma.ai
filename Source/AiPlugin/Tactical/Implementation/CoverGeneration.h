#pragma once

#include <AiPlugin/Navigation/NavMesh.h>
#include <AiPlugin/Tactical/TacticalTypes.h>
#include <Foundation/Threading/TaskSystem.h>

class plPhysicsWorldModuleInterface;
struct dtMeshTile;

/// \brief One walkable-boundary segment extracted from a navmesh tile (engine space).
struct plAiCoverEdge
{
  PL_DECLARE_POD_TYPE();

  plVec3 m_vStart = plVec3::MakeZero();
  plVec3 m_vEnd = plVec3::MakeZero();
  plVec3 m_vOutwardDir = plVec3::MakeZero(); ///< unit XY, pointing from the walkable area toward the potential wall
};

namespace plAiCoverGen
{
  /// \brief Extracts outer boundary edges (poly.neis[e] == 0) from a live tile.
  ///
  /// Main thread only - reads the dtNavMesh. Skips off-mesh connections, Blocked polys and
  /// tile-seam edges (DT_EXT_LINK). The outward direction is oriented away from the poly centroid,
  /// which is winding-independent.
  void ExtractBoundaryEdges(const dtMeshTile& tile, plDynamicArray<plAiCoverEdge>& out_edges);

  /// \brief Parameters for probing a single cover candidate (see ProbePoint).
  struct ProbeConfig
  {
    float m_fRayDistance = 1.55f;   ///< how far the horizontal probes reach (inset + probe distance for baked points)
    float m_fCrouchHeight = 0.6f;   ///< probe height for Low cover
    float m_fStandHeight = 1.5f;    ///< probe height for High cover
    plUInt8 m_uiCollisionLayer = 0; ///< static geometry layer to probe against
  };

  /// \brief Probes one candidate stand position against the wall in vWallDir.
  ///
  /// Ground-snaps the position with a downward ray (ramps/stairs: the caller's Z estimate can be
  /// badly off), classifies Low/High with horizontal rays at crouch/stand height and measures the
  /// wall-top height with a small ray ladder. Returns false when nothing blocks fire at crouch
  /// height (a ledge, not cover). Thread-safe: physics queries only.
  bool ProbePoint(const plPhysicsWorldModuleInterface& physics, const plVec3& vStandPos, const plVec3& vWallDir, const ProbeConfig& cfg, plAiCoverPoint& out_point);
} // namespace plAiCoverGen

/// \brief Samples cover candidates along a snapshot of boundary edges and probes them with physics
/// raycasts. Runs as a LongRunning task; touches ONLY its own members + thread-safe physics queries.
///
/// When the sector holds more samples than m_uiMaxPoints, an evenly strided subset is probed
/// instead of truncating in navmesh-poly order, so coverage stays uniform across the whole sector.
/// Ray budget: ~5-6 raycasts per probed sample (ground snap + crouch/stand + wall-top ladder).
///
/// Edges are walked in wall-contour order (chained end-to-start), so consecutive accepted points
/// along one wall - including around corners - form a cover SURFACE (m_Surfaces + the points'
/// m_uiSurface index). Surfaces break on probe failures, position gaps and contour ends.
class plAiCoverBakeTask final : public plTask
{
public:
  // inputs, set on the main thread before the task starts
  plAiNavMesh::SectorID m_SectorID = plInvalidIndex;
  const plPhysicsWorldModuleInterface* m_pPhysics = nullptr;
  plDynamicArray<plAiCoverEdge> m_Edges;

  // configured via plAiTacticalConfig (AI project settings)
  float m_fSpacing = 1.0f;        ///< distance between samples along an edge
  float m_fInset = 0.35f;         ///< how far the standing position is pulled back from the edge (agent radius + margin)
  float m_fProbeDistance = 1.2f;  ///< how far past the edge the wall may be
  float m_fCrouchHeight = 0.6f;   ///< probe height for Low cover
  float m_fStandHeight = 1.5f;    ///< probe height for High cover
  plUInt8 m_uiCollisionLayer = 0; ///< = the cover navmesh's collision layer
  plUInt32 m_uiMaxPoints = 128;

  // output
  plDynamicArray<plAiCoverPoint> m_Points;
  plDynamicArray<plAiCoverSurface> m_Surfaces; ///< wall strips over m_Points (see plAiCoverSurface)

protected:
  virtual void Execute() override;
};
