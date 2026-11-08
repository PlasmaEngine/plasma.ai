#pragma once

#include <AiPlugin/Navigation/NavMeshStatus.h>
#include <AiPlugin/Navigation/NavigationConfig.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <Foundation/Containers/Deque.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/Map.h>
#include <Foundation/Math/Vec2.h>
#include <Foundation/Threading/AtomicInteger.h>
#include <Foundation/Types/RefCounted.h>
#include <Foundation/Types/SharedPtr.h>
#include <Recast.h>
#include <RendererCore/Debug/DebugRendererContext.h>

using plDataBuffer = plDynamicArray<plUInt8>;

class plNavmeshGeoWorldModuleInterface;
class dtNavMesh;

/// \brief Stores indices for a triangle.
struct plAiNavMeshTriangle final
{
  PL_DECLARE_POD_TYPE();

  plAiNavMeshTriangle() = default;
  plAiNavMeshTriangle(plInt32 a, plInt32 b, plInt32 c)
  {
    m_VertexIdx[0] = a;
    m_VertexIdx[1] = b;
    m_VertexIdx[2] = c;
  }

  plInt32 m_VertexIdx[3];
};

/// \brief Stores the geometry from which a navmesh should be generated.
struct plAiNavMeshInputGeo final
{
  plDynamicArray<plVec3> m_Vertices;
  plDynamicArray<plAiNavMeshTriangle> m_Triangles;
  plDynamicArray<plUInt8> m_TriangleAreaIDs;
};

/// \brief State about a single sector (tile / cell) of an plAiNavMesh
///
/// The flags belong to the main thread. While a build runs, its task only writes m_NavmeshDataNew and
/// the build results below; FinalizeSectorUpdates reads them on the main thread once the task reported.
struct plAiNavMeshSector final
{
  plAiNavMeshSector();
  ~plAiNavMeshSector();

  plUInt8 m_FlagRequested : 1;
  plUInt8 m_FlagInvalidate : 1;
  plUInt8 m_FlagBuilding : 1; ///< a build was dispatched and hasn't been finalized yet
  plUInt8 m_FlagUsable : 1;
  plUInt8 m_FlagDirtyDuringBuild : 1; ///< invalidated while building; rebuilt once the build is finalized
  plUInt8 m_FlagLastBuildFailed : 1;

  plAiNavMeshRebuildReason m_PendingReason = plAiNavMeshRebuildReason::FirstBuild; ///< for the next build
  plAiNavMeshRebuildReason m_BuildReason = plAiNavMeshRebuildReason::None;         ///< of the running build
  plAiNavMeshRebuildReason m_LastReason = plAiNavMeshRebuildReason::None;          ///< of the last finished build
  plUInt32 m_uiBuildCount = 0;

  // written by the build task
  plDataBuffer m_NavmeshDataNew;
  plUInt32 m_uiBuildPolyCount = 0;
  plTime m_BuildDuration;
  bool m_bBuildFailed = false;

  plDataBuffer m_NavmeshDataCur;
  dtTileRef m_TileRef = 0;
  plUInt32 m_uiPolyCount = 0; ///< of the tile in use
  plTime m_LastBuildDuration;

  plTime m_BuildStarted; ///< when the running build was dispatched

  /// Height range of the tile in use, for the sector views. Kept when the tile goes away.
  float m_fTileMinZ = 0.0f;
  float m_fTileMaxZ = 0.0f;
  bool m_bHasTileBounds = false;
};

/// \brief A navmesh generated with a specific configuration.
///
/// Each game may use multiple navmeshes for different character types (large, small, etc).
/// All navmeshes always exist, but only some may contain data.
/// You get access to a navmesh through the plAiNavMeshWorldModule.
///
/// To do a path search, use plAiNavigation.
/// Since the navmesh is built in the background, a path search may need to run for multiple frames,
/// before it can return any result.
class PL_AIPLUGIN_DLL plAiNavMesh final
{
  PL_DISALLOW_COPY_AND_ASSIGN(plAiNavMesh);

public:
  plAiNavMesh(const plAiNavmeshConfig& navmeshConfig);
  ~plAiNavMesh();

  /// \brief False when the Detour navmesh could not be initialized for the configured sector grid.
  bool IsValid() const { return m_bValid; }

  using SectorID = plUInt32;

  plVec2I32 CalculateSectorCoord(float fPositionX, float fPositionY) const;
  plVec2 GetSectorPositionOffset(plVec2I32 vCoord) const;
  plBoundingBox GetSectorBounds(plVec2I32 vCoord, float fMinZ = 0.0f, float fMaxZ = 1.0f) const;
  float GetSectorSize() const { return m_fSectorMetersXY; }
  SectorID CalculateSectorID(plVec2I32 vCoord) const { return vCoord.y * m_uiNumSectorsX + vCoord.x; }
  plVec2I32 CalculateSectorCoord(SectorID sectorID) const;

  const plAiNavMeshSector* GetSector(SectorID sectorID) const;

  /// \brief Marks the sector as requested.
  ///
  /// Returns true, if the sector is already available, false when it needs to be built first.
  bool RequestSector(SectorID sectorID);

  /// \brief Marks all sectors within the given rectangle as requested.
  ///
  /// Returns true, if all the sectors are already available, false when any of them needs to be built first.
  bool RequestSector(const plVec2& vCenter, const plVec2& vHalfExtents);

  /// \brief Marks the sector as invalidated.
  ///
  /// Invalidated sectors are considered out of date and must be rebuilt before they can be used again.
  /// If bRebuildAsSoonAsPossible is true, the sector is queued to be rebuilt as soon as possible.
  /// Otherwise, it will be unloaded and will not be rebuilt until it is requested again.
  ///
  /// An invalidation that arrives while the sector is being built makes it rebuild once that build finished.
  void InvalidateSector(SectorID sectorID, bool bRebuildAsSoonAsPossible, plAiNavMeshRebuildReason reason = plAiNavMeshRebuildReason::Changed);

  /// \brief Marks all sectors within the given rectangle as invalidated.
  ///
  /// Invalidated sectors are considered out of date and must be rebuilt before they can be used again.
  /// If bRebuildAsSoonAsPossible is true, the sector is queued to be rebuilt as soon as possible.
  /// Otherwise, it will be unloaded and will not be rebuilt until it is requested again.
  void InvalidateSector(const plVec2& vCenter, const plVec2& vHalfExtents, bool bRebuildAsSoonAsPossible, plAiNavMeshRebuildReason reason = plAiNavMeshRebuildReason::Changed);

  /// \brief Marks a sector as being built and returns it for the build task. Main thread only.
  ///
  /// The returned sector stays valid (sectors are never removed); the task must only write its build results.
  plAiNavMeshSector& BeginSectorBuild(SectorID sectorID);

  /// \brief The status of every sector that was ever requested. Main thread only.
  void GetSectorStatus(plDynamicArray<plAiNavMeshSectorStatus>& out_status) const;

  /// \brief Counts the sectors by state. A sector is near the budget at 85 % of uiPolyBudget polygons.
  static plAiNavMeshStats CalculateStats(plArrayPtr<const plAiNavMeshSectorStatus> sectors, plUInt32 uiPolyBudget);

  /// \brief Whether the polygon count is close to the budget the views and the warning use.
  static bool IsNearPolyBudget(plUInt32 uiPolyCount, plUInt32 uiPolyBudget) { return plAiIsNearPolyBudget(uiPolyCount, uiPolyBudget); }

  /// \brief Draws the sectors for one of the sector views: filled by state, reason or polygon count, with a border.
  ///
  /// Sectors that are building show a progress bar, failed ones a cross. vHighlight, if valid (>= 0), gets a bold outline.
  void DebugDrawSectors(plDebugRendererContext context, plAiNavMeshVisualizeMode::Enum mode, plUInt32 uiPolyBudget, plVec2I32 vHighlight) const;

  /// \brief The most polygons one sector can hold; sectors close to it should be made smaller.
  static constexpr plUInt32 MaxPolysPerSector = 1 << 16;

  /// \brief Swaps finished sector builds into the dtNavMesh (tile add/remove). Main thread only.
  ///
  /// Optionally returns the IDs of all sectors whose tiles changed (updated or unloaded).
  void FinalizeSectorUpdates(plDynamicArray<SectorID>* out_pChangedSectors = nullptr);

  /// \brief Pops the next sector to build.
  ///
  /// Rebuilds of currently usable sectors are preferred over first-time builds (an invalidated
  /// tile that agents may be standing on is a visible regression until rebuilt).
  /// Sectors whose previous build still awaits finalization are skipped (kept queued).
  SectorID RetrieveRequestedSector();

  /// \brief Puts a sector back at the end of the request queue (e.g. when its build must be deferred).
  void RequeueSector(SectorID sectorID);

  /// \brief Number of sectors currently waiting to be built.
  plUInt32 GetNumRequestedSectors() const { return m_RequestedSectors.GetCount(); }

  /// \brief Increased every time tiles are added to or removed from the navmesh.
  ///
  /// Path corridors cache polygon references; compare this counter to detect that a
  /// re-validation is necessary. Also bumped when runtime poly flags change.
  plUInt32 GetNavMeshGeneration() const { return static_cast<plUInt32>(m_uiNavMeshGeneration); }

  /// \brief Forces path corridors to re-validate (e.g. after runtime poly flag changes).
  void BumpNavMeshGeneration() { m_uiNavMeshGeneration.Increment(); }

  /// \brief Sets/clears polygon flags (and optionally the area id) on all polygons whose centroid
  /// lies inside the given world-space box.
  ///
  /// This is how doors and other togglable blockers work: no rebuild, no data change, just flag
  /// bits that the path search filters include/exclude. uiSetArea != 0xFF additionally rewrites
  /// the polygon area id (used to toggle soft avoidance zones, plAiNavMeshAvoidZoneAreaID); the area
  /// it replaces is remembered, and RestoreBakedArea puts it back.
  /// Main thread only (mutates the dtNavMesh). Returns the number of polygons touched.
  plUInt32 ApplyFlagPatch(const plBoundingBox& worldBox, plUInt16 uiSetFlags, plUInt16 uiClearFlags, plUInt8 uiSetArea = 0xFF);

  /// For ApplyFlagPatch: restore the area a previous patch replaced (the ground type baked into the polygon).
  static constexpr plUInt8 RestoreBakedArea = 0xFE;

  /// \brief Builds a sector returned by BeginSectorBuild. Runs on a worker thread.
  void BuildSector(plAiNavMeshSector& ref_sector, SectorID sectorID, const plNavmeshGeoWorldModuleInterface* pGeo, plArrayPtr<const plAiNavLinkData> links, plArrayPtr<const plBoundingBox> carveBoxes, plArrayPtr<const plBoundingBox> blockerZones, plArrayPtr<const plBoundingBox> avoidZones);

  const dtNavMesh* GetDetourNavMesh() const { return m_pNavMesh; }

  void DebugDraw(plDebugRendererContext context, const plAiNavigationConfig& config);

  const plAiNavmeshConfig& GetConfig() const { return m_NavmeshConfig; }

private:
  void DebugDrawSector(plDebugRendererContext context, const plAiNavigationConfig& config, int iTileIdx);

  plAiNavmeshConfig m_NavmeshConfig;

  plUInt32 m_uiNumSectorsX = 0;
  plUInt32 m_uiNumSectorsY = 0;
  float m_fSectorMetersXY = 0;
  float m_fInvSectorMetersXY = 0;

  dtNavMesh* m_pNavMesh = nullptr;
  bool m_bValid = false;
  plMap<SectorID, plAiNavMeshSector> m_Sectors;
  plDynamicArray<SectorID> m_RequestedSectors;
  plAtomicInteger32 m_uiNavMeshGeneration = 0;

  plMutex m_Mutex;
  plDynamicArray<SectorID> m_UpdatingSectors;

  plDynamicArray<SectorID> m_UnloadingSectors;

  // lazily initialized scratch query for ApplyFlagPatch
  dtNavMeshQuery m_FlagPatchQuery;
  bool m_bFlagPatchQueryInitialized = false;

  /// polygon -> the area ApplyFlagPatch replaced; entries of rebuilt or unloaded tiles are dropped when tiles change
  plHashTable<plUInt64, plUInt8> m_PatchedAreas;
  void DropStalePatchedAreas();

  /// Sets/clears the flags of the nav links with an end inside the box (ApplyFlagPatch's polygon query skips them).
  plUInt32 PatchNavLinks(const plBoundingBox& containTest, plUInt16 uiSetFlags, plUInt16 uiClearFlags);
};
