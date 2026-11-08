#include <AiPlugin/AiPluginPCH.h>

#include <AiPlugin/Tactical/ThreatMap.h>

float plAiThreatDangerArea::GetDanger(const plVec3& vPosition) const
{
  const float fDistance = (vPosition - m_vPosition).GetAsVec2().GetLength();

  if (m_fRadius <= 0.0f || fDistance >= m_fRadius)
    return 0.0f;

  // a floor of 0.25 inside the radius, so the edge of a grenade already counts as danger
  return plMath::Saturate((0.25f + 0.75f * (1.0f - fDistance / m_fRadius)) * m_fFade);
}

void plAiThreatSnapshot::Configure(float fSectorSize, plUInt32 uiSectorsX, plUInt32 uiSectorsY, float fCellSize)
{
  m_fSectorSize = fSectorSize;
  m_uiSectorsX = uiSectorsX;
  m_uiSectorsY = uiSectorsY;
  m_fCellSize = plMath::Max(fCellSize, 0.5f);
  m_uiCellsPerSide = plMath::Max(1u, static_cast<plUInt32>(plMath::Round(fSectorSize / m_fCellSize)));
  m_Chunks.Clear();
}

plUInt32 plAiThreatSnapshot::GetChunkKey(const plVec3& vPosition) const
{
  if (m_fSectorSize <= 0.0f)
    return plInvalidIndex;

  const plInt32 x = static_cast<plInt32>(plMath::Floor(vPosition.x / m_fSectorSize + m_uiSectorsX * 0.5f));
  const plInt32 y = static_cast<plInt32>(plMath::Floor(vPosition.y / m_fSectorSize + m_uiSectorsY * 0.5f));

  if (x < 0 || y < 0 || x >= static_cast<plInt32>(m_uiSectorsX) || y >= static_cast<plInt32>(m_uiSectorsY))
    return plInvalidIndex;

  return static_cast<plUInt32>(y) * m_uiSectorsX + static_cast<plUInt32>(x);
}

plVec2 plAiThreatSnapshot::GetChunkOrigin(plUInt32 uiKey) const
{
  const plInt32 x = static_cast<plInt32>(uiKey % m_uiSectorsX);
  const plInt32 y = static_cast<plInt32>(uiKey / m_uiSectorsX);

  return plVec2((x - m_uiSectorsX * 0.5f) * m_fSectorSize, (y - m_uiSectorsY * 0.5f) * m_fSectorSize);
}

plVec2 plAiThreatSnapshot::GetCellCenter(plUInt32 uiKey, plUInt32 uiCell) const
{
  const plVec2 vOrigin = GetChunkOrigin(uiKey);
  const plUInt32 x = uiCell % m_uiCellsPerSide;
  const plUInt32 y = uiCell / m_uiCellsPerSide;

  return plVec2(vOrigin.x + (x + 0.5f) * m_fCellSize, vOrigin.y + (y + 0.5f) * m_fCellSize);
}

plAiThreatSnapshot::Chunk& plAiThreatSnapshot::GetOrCreateChunk(plUInt32 uiKey)
{
  Chunk* pChunk = nullptr;
  if (m_Chunks.TryGetValue(uiKey, pChunk))
    return *pChunk;

  Chunk& chunk = m_Chunks[uiKey];
  const plUInt32 uiCells = GetCellsPerChunk();

  chunk.m_Z.SetCount(uiCells, 0.0f);
  chunk.m_Valid.SetCount(uiCells, 0);
  chunk.m_Threat.SetCount(uiCells, 0.0f);
  chunk.m_Visible.SetCount(uiCells, 0);
  chunk.m_bZStale = true;
  chunk.m_bAnyData = false;
  return chunk;
}

const plAiThreatSnapshot::Chunk* plAiThreatSnapshot::FindChunk(plUInt32 uiKey) const
{
  const Chunk* pChunk = nullptr;
  return m_Chunks.TryGetValue(uiKey, pChunk) ? pChunk : nullptr;
}

plAiThreatSnapshot::Chunk* plAiThreatSnapshot::FindChunk(plUInt32 uiKey)
{
  Chunk* pChunk = nullptr;
  return m_Chunks.TryGetValue(uiKey, pChunk) ? pChunk : nullptr;
}

const plAiThreatSnapshot::Chunk* plAiThreatSnapshot::FindCell(const plVec3& vPosition, plUInt32& out_uiCell) const
{
  const plUInt32 uiKey = GetChunkKey(vPosition);
  if (uiKey == plInvalidIndex)
    return nullptr;

  const Chunk* pChunk = FindChunk(uiKey);
  if (pChunk == nullptr)
    return nullptr;

  const plVec2 vOrigin = GetChunkOrigin(uiKey);
  const plUInt32 x = plMath::Clamp(static_cast<plUInt32>((vPosition.x - vOrigin.x) / m_fCellSize), 0u, m_uiCellsPerSide - 1);
  const plUInt32 y = plMath::Clamp(static_cast<plUInt32>((vPosition.y - vOrigin.y) / m_fCellSize), 0u, m_uiCellsPerSide - 1);

  out_uiCell = y * m_uiCellsPerSide + x;
  return pChunk;
}

float plAiThreatSnapshot::GetThreat(const plVec3& vPosition) const
{
  plUInt32 uiCell = 0;
  const Chunk* pChunk = FindCell(vPosition, uiCell);

  return (pChunk != nullptr && pChunk->m_Valid[uiCell] != 0) ? pChunk->m_Threat[uiCell] : 0.0f;
}

float plAiThreatSnapshot::GetVisibility(const plVec3& vPosition) const
{
  plUInt32 uiCell = 0;
  const Chunk* pChunk = FindCell(vPosition, uiCell);

  return (pChunk != nullptr && pChunk->m_Valid[uiCell] != 0 && pChunk->m_Visible[uiCell] != 0) ? 1.0f : 0.0f;
}

float plAiThreatSnapshot::GetDanger(const plVec3& vPosition) const
{
  float fDanger = 0.0f;

  for (const plAiThreatDangerArea& area : m_Areas)
  {
    fDanger = plMath::Max(fDanger, area.GetDanger(vPosition));
  }

  return fDanger;
}

void plAiThreatSnapshot::CopyFrom(const plAiThreatSnapshot& other)
{
  m_fSectorSize = other.m_fSectorSize;
  m_fCellSize = other.m_fCellSize;
  m_uiSectorsX = other.m_uiSectorsX;
  m_uiSectorsY = other.m_uiSectorsY;
  m_uiCellsPerSide = other.m_uiCellsPerSide;
  m_fTime = other.m_fTime;
  m_Chunks = other.m_Chunks;
  m_Areas = other.m_Areas;
}

PL_STATICLINK_FILE(AiPlugin, AiPlugin_Tactical_Implementation_ThreatMap);
