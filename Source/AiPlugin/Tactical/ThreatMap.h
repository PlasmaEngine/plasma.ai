#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/HashTable.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Types/RefCounted.h>

/// \brief A circular area a team should stay out of: a grenade, a fire, a collapsing floor.
///
/// Inside the radius the danger is 0.25 at the edge and 1 at the center, scaled by the area's fade (1 while it lasts,
/// falling to 0 as a timed area runs out).
struct PL_AIPLUGIN_DLL plAiThreatDangerArea
{
  PL_DECLARE_POD_TYPE();

  plVec3 m_vPosition = plVec3::MakeZero();
  float m_fRadius = 5.0f;
  float m_fFade = 1.0f;

  /// The danger this area causes at a position, 0 outside the radius.
  float GetDanger(const plVec3& vPosition) const;
};

/// \brief What one team knows about danger, on a grid of 2 m cells over the navmesh sectors.
///
/// Each cell holds the threat (how much the team's known enemies can hurt there: line of sight to a known enemy position,
/// falling off with distance and with how stale the enemy's position is) and whether any known enemy sees it. Danger
/// comes from the areas, evaluated per query.
///
/// The module updates one instance on the main thread and publishes copies, so tactical and EQS queries on worker
/// threads read a stable snapshot. A snapshot never changes after it is published.
class PL_AIPLUGIN_DLL plAiThreatSnapshot : public plRefCounted
{
public:
  /// The cells of one navmesh sector.
  struct Chunk
  {
    plDynamicArray<float> m_Z;             ///< ground height of each cell
    plDynamicArray<plUInt8> m_Valid;       ///< 1 where a navmesh polygon lies under the cell
    plDynamicArray<float> m_Threat;        ///< [0,1]
    plDynamicArray<plUInt8> m_Visible;     ///< 1 where a known enemy has line of sight
    bool m_bZStale = true;                 ///< the sector's navmesh changed: heights are recomputed on the next pass
    bool m_bAnyData = false;               ///< some cell has a threat or is visible
  };

  /// Sets the grid: the navmesh sectors, and the cell size inside them.
  void Configure(float fSectorSize, plUInt32 uiSectorsX, plUInt32 uiSectorsY, float fCellSize);

  float GetSectorSize() const { return m_fSectorSize; }
  float GetCellSize() const { return m_fCellSize; }
  plUInt32 GetSectorsX() const { return m_uiSectorsX; }
  plUInt32 GetSectorsY() const { return m_uiSectorsY; }
  plUInt32 GetCellsPerSide() const { return m_uiCellsPerSide; }
  plUInt32 GetCellsPerChunk() const { return m_uiCellsPerSide * m_uiCellsPerSide; }
  double GetTime() const { return m_fTime; }

  /// The sector (chunk key) a position is in, or plInvalidIndex outside the grid.
  plUInt32 GetChunkKey(const plVec3& vPosition) const;

  /// The world position of the lower corner of a chunk.
  plVec2 GetChunkOrigin(plUInt32 uiKey) const;

  /// The world position of a cell's center, without its height.
  plVec2 GetCellCenter(plUInt32 uiKey, plUInt32 uiCell) const;

  /// The chunk of a key, created with empty cells when it does not exist.
  Chunk& GetOrCreateChunk(plUInt32 uiKey);

  const Chunk* FindChunk(plUInt32 uiKey) const;
  Chunk* FindChunk(plUInt32 uiKey);

  const plHashTable<plUInt32, Chunk>& GetChunks() const { return m_Chunks; }
  plHashTable<plUInt32, Chunk>& GetChunks() { return m_Chunks; }

  /// \name Queries (any thread, on a published snapshot)
  ///@{

  /// The threat at a position, 0 where nothing is known or the position is off the grid.
  float GetThreat(const plVec3& vPosition) const;

  /// 1 where a known enemy has line of sight, else 0.
  float GetVisibility(const plVec3& vPosition) const;

  /// The strongest danger of the areas at a position.
  float GetDanger(const plVec3& vPosition) const;

  ///@}

  plDynamicArray<plAiThreatDangerArea>& GetAreas() { return m_Areas; }
  const plDynamicArray<plAiThreatDangerArea>& GetAreas() const { return m_Areas; }
  void SetTime(double fTime) { m_fTime = fTime; }

  /// Copies the grid, the chunks and the areas of another snapshot into this one.
  void CopyFrom(const plAiThreatSnapshot& other);

private:
  const Chunk* FindCell(const plVec3& vPosition, plUInt32& out_uiCell) const;

  float m_fSectorSize = 32.0f;
  float m_fCellSize = 2.0f;
  plUInt32 m_uiSectorsX = 0;
  plUInt32 m_uiSectorsY = 0;
  plUInt32 m_uiCellsPerSide = 16;
  double m_fTime = 0.0;

  plHashTable<plUInt32, Chunk> m_Chunks;
  plDynamicArray<plAiThreatDangerArea> m_Areas;
};
