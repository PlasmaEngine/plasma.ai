#pragma once

#include <AiPlugin/Navigation/NavMesh.h>
#include <Foundation/Threading/TaskSystem.h>

class plNavmeshGeoWorldModuleInterface;

class plNavMeshSectorGenerationTask : public plTask
{
public:
  plAiNavMesh::SectorID m_SectorID = plInvalidIndex;
  plAiNavMesh* m_pWorldNavMesh = nullptr;
  const plNavmeshGeoWorldModuleInterface* m_pNavGeo = nullptr;

  /// Snapshot of the nav links whose start point lies in this sector (copied at dispatch time,
  /// on the main thread - the task never touches the link registry).
  plHybridArray<plAiNavLinkData, 8> m_Links;

  /// Snapshot of dynamic-obstacle carve boxes overlapping this sector (copied at dispatch time).
  /// These are stamped unwalkable during the build - dynamic physics bodies are not part of the
  /// collected input geometry (static only), so this is how they carve the navmesh.
  plHybridArray<plBoundingBox, 4> m_CarveBoxes;

  /// Snapshot of nav blocker volumes overlapping this sector (copied at dispatch time).
  /// Marked with plAiNavMeshBlockerZoneAreaID during the build so the polygons under each blocker
  /// are split from the surrounding floor and the runtime Blocked flag hits exactly them.
  plHybridArray<plBoundingBox, 4> m_BlockerZones;

  /// Snapshot of soft avoidance volumes overlapping this sector (copied at dispatch time).
  /// Marked with plAiNavMeshAvoidZoneAreaID, which stays in the runtime data and carries a high
  /// traversal cost in all path search filters.
  plHybridArray<plBoundingBox, 4> m_AvoidZones;

protected:
  virtual void Execute() override;
};
