#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Navigation/Implementation/NavMeshGeneration.h>
#include <Core/World/WorldModule.h>

class plAiNavMesh;
class dtNavMesh;

/// This world module keeps track of all the configured navmeshes (for different character types)
/// and makes sure to build their sectors in the background.
///
/// Through this you can get access to one of the available navmeshes.
/// Additionally, it also provides access to the different path search filters.
class PL_AIPLUGIN_DLL plAiNavMeshWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiNavMeshWorldModule, plWorldModule);

public:
  plAiNavMeshWorldModule(plWorld* pWorld);
  ~plAiNavMeshWorldModule();

  virtual void Initialize() override;
  virtual void Deinitialize() override;

  plAiNavMesh* GetNavMesh(plStringView sName);
  const plAiNavMesh* GetNavMesh(plStringView sName) const;

  const dtQueryFilter& GetPathSearchFilter(plStringView sName) const;

  const plAiNavigationConfig& GetConfig() const { return m_Config; }

  /// \name Nav blockers (runtime poly-flag patches, e.g. doors)
  ///@{

  using BlockerID = plUInt32;
  static constexpr BlockerID InvalidBlockerID = plInvalidIndex;

  /// \brief Registers a world-space box that blocks (or releases) navmesh polygons at runtime.
  ///
  /// Blockers don't change the navmesh shape - they toggle the 'Blocked' poly flag (mode Block)
  /// or the avoid-zone area id (mode Avoid, high path cost instead of a hard block) on the polys
  /// in the box. Registering (or moving) a blocker invalidates the overlapping sectors once, so
  /// the build splits dedicated polygons out of the volume; from then on TOGGLING is instant
  /// (no rebuild). Rebuilt sectors are automatically re-stamped.
  BlockerID RegisterNavBlocker(const plBoundingBox& worldBox, bool bActive, plEnum<plAiNavObstacleMode> mode);
  void SetNavBlockerActive(BlockerID id, bool bActive);
  void UpdateNavBlockerBounds(BlockerID id, const plBoundingBox& worldBox);
  void UnregisterNavBlocker(BlockerID id);

  /// \brief Collects all registered blocker volumes (active or not) overlapping the given sector.
  /// The build marks them with plAiNavMeshBlockerZoneAreaID to force a poly split.
  void CollectBlockerZonesForSector(const plAiNavMesh& navMesh, plAiNavMesh::SectorID sectorID, plDynamicArray<plBoundingBox>& out_boxes) const;

  ///@}

  /// \brief The sectors of the given navmesh whose tiles were added/removed during THIS frame's update.
  ///
  /// Only valid in the PostTransform phase after this module's update ran - order your update
  /// function after "plAiNavMeshWorldModule::Update" via m_DependsOn (the tactical module does).
  plArrayPtr<const plAiNavMesh::SectorID> GetChangedSectorsThisFrame(const plAiNavMesh* pNavMesh) const;

  /// \brief Queues a rebuild of all navmesh sectors overlapping the world-space box.
  ///
  /// Use to reflect a runtime change of the world geometry (e.g. a destroyed wall) in the navmesh.
  /// The tactical layer re-bakes the cover of any changed sector automatically.
  void InvalidateRegion(const plBoundingBox& worldBox) { InvalidateSectorsForBox(worldBox); }

  /// \brief Requests all sectors of the given navmesh overlapping the world-space box to be built.
  ///
  /// sNavmesh empty = all configured navmeshes. Returns true when every overlapped sector is
  /// already usable - call again later otherwise (requests are idempotent and cheap once built).
  /// Sectors otherwise only build on demand (path searches, tactical/EQS queries), so cover and
  /// navigation data never exist where no agent has been; plAiNavMeshPrebuildComponent uses this
  /// to warm up an area ahead of time.
  bool RequestRegion(plStringView sNavmesh, const plBoundingBox& worldBox);

  /// \name Nav links (off-mesh connections: jumps, ladders, doors)
  ///@{

  using NavLinkID = plUInt32;
  static constexpr NavLinkID InvalidNavLinkID = plInvalidIndex;

  /// \brief Registers a nav link. The sectors containing its endpoints are invalidated, so the
  /// link gets baked into them as an off-mesh connection on the next rebuild.
  NavLinkID RegisterNavLink(const plAiNavLinkData& linkData, const plComponentHandle& hComponent);
  void UnregisterNavLink(NavLinkID id);

  /// \brief Resolves the authoring component of a baked off-mesh connection (via its user ID).
  plComponentHandle ResolveNavLink(plUInt32 uiUserID) const;

  /// \brief The link type stored for the given user ID (Jump if unknown).
  plEnum<plAiNavLinkType> GetNavLinkType(plUInt32 uiUserID) const;

  /// \brief Collects all registered links whose START point lies within the given sector of the navmesh.
  void CollectNavLinksForSector(const plAiNavMesh& navMesh, plAiNavMesh::SectorID sectorID, plDynamicArray<plAiNavLinkData>& out_links) const;

  ///@}

  /// \name Obstacle carve volumes (dynamic physics objects)
  ///@{

  using ObstacleCarveID = plUInt32;
  static constexpr ObstacleCarveID InvalidObstacleCarveID = plInvalidIndex;

  /// \brief Registers a world-space box that is stamped into every sector (re)build.
  ///
  /// Navmesh generation only collects STATIC physics geometry, so dynamic bodies (crates, doors on
  /// physics joints, ...) are invisible to it. plNavMeshObstacleComponent registers its bounds here
  /// and invalidates the overlapping sectors. Mode Block carves the box out of the walkable area;
  /// mode Avoid keeps it walkable but marks it with the high-cost avoid-zone area id.
  ObstacleCarveID RegisterObstacleCarve(const plBoundingBox& worldBox, plEnum<plAiNavObstacleMode> mode);
  void UpdateObstacleCarve(ObstacleCarveID id, const plBoundingBox& worldBox);
  void UnregisterObstacleCarve(ObstacleCarveID id);

  /// \brief Collects all registered obstacle boxes overlapping the given sector (with a border
  /// margin), separated by mode: carve boxes (Block) and avoid zones (Avoid).
  void CollectObstacleCarvesForSector(const plAiNavMesh& navMesh, plAiNavMesh::SectorID sectorID, plDynamicArray<plBoundingBox>& out_carveBoxes, plDynamicArray<plBoundingBox>& out_avoidZones) const;

  ///@}

private:
  void Update(const UpdateContext& ctxt);

  /// Lazily spawns the single global plAiDestructionResponseComponent (once, when a navmesh exists),
  /// so destruction events dirty the navmesh without any manual scene setup.
  void EnsureDestructionResponder();
  plGameObjectHandle m_hDestructionResponder;

  bool IsSectorBuildInFlight(const plAiNavMesh* pNavMesh, plAiNavMesh::SectorID sectorID) const;
  void ApplyNavBlockers(plAiNavMesh& ref_navMesh, plArrayPtr<const plAiNavMesh::SectorID> changedSectors);

  struct FlagPatch
  {
    plBoundingBox m_WorldBox;
    plBoundingBox m_ClearBox; ///< region to clear before re-stamping; covers old+new box after a move
    plEnum<plAiNavObstacleMode> m_Mode;
    bool m_bInUse = false;
    bool m_bActive = false;
    bool m_bDirty = false;          ///< needs (re)application this frame
    bool m_bPendingRemoval = false; ///< clear the area once, then free the slot
  };

  plDeque<FlagPatch> m_FlagPatches;
  plDynamicArray<BlockerID> m_FreeFlagPatches;
  bool m_bAnyBlockerDirty = false;

  struct NavLink
  {
    plAiNavLinkData m_Data;
    plComponentHandle m_hComponent;
    bool m_bInUse = false;
  };

  plDeque<NavLink> m_NavLinks;
  plDynamicArray<NavLinkID> m_FreeNavLinks;

  struct ObstacleCarve
  {
    plBoundingBox m_WorldBox;
    plEnum<plAiNavObstacleMode> m_Mode;
    bool m_bInUse = false;
  };

  plDeque<ObstacleCarve> m_ObstacleCarves;
  plDynamicArray<ObstacleCarveID> m_FreeObstacleCarves;

  void InvalidateSectorsAtLinkEndpoints(const plAiNavLinkData& linkData);
  void InvalidateSectorsForBox(const plBoundingBox& worldBox);

  plMap<plString, plAiNavMesh*> m_WorldNavMeshes;

  /// Navmesh generation only starts once the physics scene contains the level geometry.
  /// This module updates in the PostTransform phase: if a physics world module exists, its
  /// simulation start (PreAsync) has already flushed all initially queued static bodies by the
  /// time this module first runs, so the gate opens on the first simulated frame.
  bool m_bReadyToGenerate = false;

  /// One slot per potentially concurrent sector build (count controlled by AI.Navmesh.MaxConcurrentBuilds).
  struct BuildSlot
  {
    plSharedPtr<plNavMeshSectorGenerationTask> m_pTask;
    plTaskGroupID m_TaskID;
    bool m_bInFlight = false;
  };

  plHybridArray<BuildSlot, 16> m_BuildSlots;

  plAiNavigationConfig m_Config;

  plMap<plString, dtQueryFilter> m_PathSearchFilters;

  float m_fAppliedAvoidZoneCost = -1.0f; ///< last avoid-zone cost pushed into the filters (cvar sync)

  // per-navmesh sectors finalized this frame; consumed by the tactical module (cover re-bakes)
  plMap<const plAiNavMesh*, plDynamicArray<plAiNavMesh::SectorID>> m_LastChangedSectors;
  plUInt32 m_uiChangedSectorsUpdateCounter = 0;
};

/* TODO:

Navmesh Generation
==================

* fix navmesh on hills
* collision group filtering
* invalidate sectors, re-generate
* Invalidate path searches after sector changes
* sector usage tracking
* unload unused sectors

Path Search
===========

* callback for touched sectors
* on-demand sector generation ???
* use max edge-length + poly flags for 'dynamic' obstacles

Steering
========

* movement with ineratia
* decoupled position and rotation
* avoid dynamic obstacles

*/
