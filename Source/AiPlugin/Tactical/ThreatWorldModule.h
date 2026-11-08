#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Tactical/ThreatMap.h>
#include <Core/World/WorldModule.h>
#include <DetourNavMeshQuery.h>
#include <Foundation/Containers/Deque.h>
#include <Foundation/Time/Time.h>
#include <Foundation/Types/SharedPtr.h>

class plAiNavMesh;
class plAiNavMeshWorldModule;
class plPhysicsWorldModuleInterface;

/// \brief What each team knows about danger: the threat map.
///
/// Per team, a grid of 2 m cells over the navmesh sectors holds the threat (line of sight to the enemy positions the
/// team's agents remember, by distance and by how sure the agent is) and which cells a known enemy sees. Danger comes from
/// danger areas that game code adds (a grenade, a fire). The grid refreshes at the squad tick rate, within a raycast
/// budget per frame, from the targets the agents' perception remembers; stimuli (gunshots, damage) reach it through the
/// agents' target records.
///
/// Tactical queries and EQS tests read published snapshots (GetSnapshot), which are stable on worker threads. The module
/// also writes 'Ai_ThreatHere' and 'Ai_DangerHere' into the blackboards of agents that have a plBlackboardComponent, and
/// ends the behavior of an agent whose claimed cover lies in a danger area, so it picks its cover again.
///
/// All public APIs are main-thread only, except the snapshot queries.
class PL_AIPLUGIN_DLL plAiThreatWorldModule final : public plWorldModule
{
  PL_DECLARE_WORLD_MODULE();
  PL_ADD_DYNAMIC_REFLECTION(plAiThreatWorldModule, plWorldModule);
  PL_DISALLOW_COPY_AND_ASSIGN(plAiThreatWorldModule);

public:
  plAiThreatWorldModule(plWorld* pWorld);
  ~plAiThreatWorldModule();

  virtual void Initialize() override;
  virtual void Deinitialize() override;

  /// The edge of a grid cell, in meters.
  static constexpr float CellSize = 2.0f;

  /// \name Danger areas
  ///@{

  using DangerID = plUInt32;
  static constexpr DangerID InvalidDangerID = plInvalidIndex;

  /// \brief Adds an area the team should stay out of.
  ///
  /// A duration above zero fades the danger over that time and then removes the area; zero keeps it until
  /// RemoveDangerArea. An empty team makes it danger for every team.
  DangerID AddDangerArea(const plVec3& vPosition, float fRadius, plTime duration, plStringView sTeam = {});
  void MoveDangerArea(DangerID id, const plVec3& vPosition);
  void RemoveDangerArea(DangerID id);

  plUInt32 GetDangerAreaCount() const;

  ///@}

  /// \brief The latest published map of a team, or nullptr before the module knows the team.
  ///
  /// The snapshot never changes; hold the pointer for as long as a query needs it, on any thread.
  plSharedPtr<const plAiThreatSnapshot> GetSnapshot(const plTempHashedString& sTeam) const;

  /// \brief Threat, visibility and danger of the latest snapshot; 0 for an unknown team.
  float GetThreat(const plTempHashedString& sTeam, const plVec3& vPosition) const;
  float GetVisibility(const plTempHashedString& sTeam, const plVec3& vPosition) const;
  float GetDanger(const plTempHashedString& sTeam, const plVec3& vPosition) const;

  /// \brief What the module counted in its latest update, for the stats of the views.
  struct Stats
  {
    plUInt32 m_uiTeams = 0;
    plUInt32 m_uiChunks = 0;
    plUInt32 m_uiCells = 0;
    plUInt32 m_uiSources = 0;
    plUInt32 m_uiDangerAreas = 0;
    plUInt32 m_uiRaycasts = 0;
    plUInt32 m_uiEvaded = 0; ///< behaviors ended because their cover lay in danger, since the world started
  };

  const Stats& GetStats() const { return m_Stats; }

  /// \brief The map of the team the views show: AI.Threat.VisualizeTeam, else the team with the most data. nullptr if none.
  const plAiThreatSnapshot* GetVisualizedSnapshot(plHashedString* out_pTeam = nullptr) const;

  /// \brief What the views and the debugger read of a team's current knowledge.
  struct DebugSource
  {
    plVec3 m_vPosition = plVec3::MakeZero();
    float m_fConfidence = 0.0f;
  };

  void GetDebugSources(const plTempHashedString& sTeam, plDynamicArray<DebugSource>& out_sources) const;

private:
  struct Source
  {
    PL_DECLARE_POD_TYPE();

    plVec3 m_vPosition = plVec3::MakeZero();
    float m_fConfidence = 0.0f;
    plUInt32 m_uiTargetId = 0; ///< 0 for events without an object
  };

  struct TeamMap
  {
    plHashedString m_sTeam;
    plSharedPtr<plAiThreatSnapshot> m_pWorking;
    plSharedPtr<const plAiThreatSnapshot> m_pPublished;

    plDynamicArray<Source> m_Sources;      ///< the sources the running pass computes from
    plDynamicArray<plUInt32> m_PassChunks; ///< the chunks of the running pass
    plUInt32 m_uiPassChunk = 0;
    plUInt32 m_uiPassCell = 0;
    bool m_bPassActive = false;
    bool m_bPublishPending = false;
  };

  struct Area
  {
    plVec3 m_vPosition = plVec3::MakeZero();
    float m_fRadius = 0.0f;
    plTime m_Start;
    plTime m_Duration; ///< zero = until removed
    plHashedString m_sTeam;
    plUInt16 m_uiGeneration = 0;
    bool m_bInUse = false;
  };

  void Update(const UpdateContext& ctxt);

  plAiNavMesh* ResolveNavMesh(plAiNavMeshWorldModule& navMeshModule) const;
  void ConfigureGrid(const plAiNavMesh& navMesh);
  TeamMap& GetOrCreateTeam(const plHashedString& sTeam);
  void MarkChangedSectors(plArrayPtr<const plUInt32> changedSectors);

  void UpdateAreas(plTime now);
  void FillAreas(const TeamMap& team, plAiThreatSnapshot& ref_snapshot, plTime now) const;
  void EnsureAreaChunks(const Area& area);

  void TickAgents(plTime now);
  void StartPass(TeamMap& ref_team, plArrayPtr<const Source> sources);
  void RunPasses(float fBudget);
  void RunPass(TeamMap& ref_team, float& ref_fBudget);
  void PrepareChunk(plAiThreatSnapshot& ref_snapshot, plUInt32 uiKey, plAiThreatSnapshot::Chunk& ref_chunk);
  void Publish(TeamMap& ref_team, plTime now);

  void EvadeDanger(plTime now);
  void Draw();

  friend struct plAiThreatWorldModuleTestAccess;

  /// Enemy positions the tests make up, merged with what the agents remember.
  struct InjectedSource
  {
    plHashedString m_sTeam;
    Source m_Source;
  };

  plDynamicArray<InjectedSource> m_InjectedSources;

  plAiNavMesh* m_pNavMesh = nullptr;
  plAiNavMeshWorldModule* m_pNavMeshModule = nullptr;
  const plPhysicsWorldModuleInterface* m_pPhysics = nullptr;
  dtNavMeshQuery m_NavQuery;
  bool m_bNavQueryReady = false;

  plHashTable<plUInt32, plUInt32> m_TeamLookup; ///< team name hash -> index in m_Teams
  plDeque<TeamMap> m_Teams;

  plDeque<Area> m_Areas;
  plDynamicArray<plUInt32> m_FreeAreas;
  bool m_bAreasDirty = false;

  plTime m_NextTick;
  plHashTable<plUInt32, plTime> m_EvadedAt; ///< agent owner id -> when its cover was last abandoned

  Stats m_Stats;
  plInt32 m_iRaycastsThisFrame = 0;
};
