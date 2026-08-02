#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Math/Angle.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/String.h>

static constexpr plUInt32 plAiNumGroundTypes = 32;

/// \brief Polygon flags stored per navmesh polygon.
///
/// Ground type filtering uses area bits (see plAiPathSearchConfig); these flags are orthogonal:
/// they are cheap to change at RUNTIME without rebuilding any navmesh data.
struct plAiNavMeshPolyFlags
{
  enum Enum : plUInt16
  {
    Walkable = 0x0001,    ///< set on every valid polygon at generation time
    OffMeshLink = 0x0002, ///< set on off-mesh connections (nav links)
    Blocked = 0x8000,     ///< set/cleared at runtime by plAiNavBlockerComponent (doors etc.); excluded by all path filters
  };
};

/// \brief Recast area id used at build time to split nav blocker volumes into their own polygons.
///
/// Recast never merges regions across differing area ids, so marking a blocker's box with this id
/// forces the polygons under it to be separate from the surrounding floor. Without the split, the
/// runtime 'Blocked' flag would hit whole floor polygons (they can span most of a sector).
/// The id is outside the ground-type range (0-31) and is remapped to the default ground type
/// before the Detour data is built, so path filters never see it.
static constexpr plUInt8 plAiNavMeshBlockerZoneAreaID = 32;

/// \brief Recast area id for soft avoidance volumes (obstacle/blocker Mode 'Avoid').
///
/// Unlike the blocker-zone id this one SURVIVES into the runtime navmesh data: all path search
/// filters include it and assign it a high traversal cost (AI.Navmesh.AvoidZoneCost), so agents
/// cross such polygons only when no reasonably cheap detour exists. The original ground type
/// under the volume is not preserved.
static constexpr plUInt8 plAiNavMeshAvoidZoneAreaID = 33;

/// \brief How a nav obstacle or blocker volume affects pathing.
struct PL_AIPLUGIN_DLL plAiNavObstacleMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    Block, ///< the volume is impassable (carved out / flagged Blocked)
    Avoid, ///< the volume stays walkable but costs extra to cross - agents route around it when a reasonable alternative exists

    Default = Block
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiNavObstacleMode);

struct PL_AIPLUGIN_DLL plAiNavmeshConfig
{
  plString m_sName;

  plUInt16 m_uiNumSectorsX = 64;
  plUInt16 m_uiNumSectorsY = 64;

  float m_fSectorSize = 32.0f;

  /// The physics collision layer to use for building this navmesh (retrieving the physics geometry).
  plUInt8 m_uiCollisionLayer = 0;

  float m_fCellSize = 0.2f;
  float m_fCellHeight = 0.2f;

  float m_fAgentRadius = 0.2f;
  float m_fAgentHeight = 1.5f;
  float m_fAgentStepHeight = 0.6f;

  plAngle m_WalkableSlope = plAngle::MakeFromDegree(45);

  float m_fMaxEdgeLength = 4.0f;
  float m_fMaxSimplificationError = 1.3f;
  float m_fMinRegionSize = 0.5f;
  float m_fRegionMergeSize = 5.0f;
  float m_fDetailMeshSampleDistanceFactor = 1.0f;
  float m_fDetailMeshSampleErrorFactor = 1.0f;
};

/// \brief What kind of traversal an off-mesh nav link represents. Games use this to pick animations.
struct PL_AIPLUGIN_DLL plAiNavLinkType
{
  using StorageType = plUInt8;

  enum Enum
  {
    Jump,
    Vault,
    Ladder,
    Drop,
    Door,
    Custom,

    Default = Jump
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiNavLinkType);

/// \brief POD snapshot of one nav link, baked into navmesh tiles as an off-mesh connection.
struct plAiNavLinkData
{
  plVec3 m_vStart = plVec3::MakeZero();
  plVec3 m_vEnd = plVec3::MakeZero();
  float m_fRadius = 0.3f;
  plUInt32 m_uiUserID = 0; ///< registry ID, resolves back to the authoring component
  plUInt8 m_uiAreaID = 1;
  bool m_bBidirectional = true;
  plEnum<plAiNavLinkType> m_Type;
};

struct PL_AIPLUGIN_DLL plAiPathSearchConfig
{
  plAiPathSearchConfig();

  plString m_sName;
  float m_fGroundTypeCost[plAiNumGroundTypes];   // = 1.0f
  bool m_bGroundTypeAllowed[plAiNumGroundTypes]; // = true
};

/// \brief Project settings for the AI tactical layer (cover generation).
///
/// Edited in the AI project settings dialog, stored in AiPluginConfig.cfg. Cover probing uses the
/// cover navmesh's collision layer, so there is no separate layer setting.
struct PL_AIPLUGIN_DLL plAiTacticalConfig
{
  plString m_sCoverNavmesh;                   ///< which navmesh config cover points are baked for; empty = the first one
  float m_fCoverSpacing = 1.0f;               ///< distance between generated cover points along walls, in meters
  float m_fCoverProbeDistance = 1.2f;         ///< how far behind a navmesh boundary edge blocking geometry may be
  float m_fCoverCrouchHeight = 0.6f;          ///< probe height for Low cover
  float m_fCoverStandHeight = 1.5f;           ///< probe height for High cover
  plUInt16 m_uiMaxCoverPointsPerSector = 256; ///< upper limit of cover points per navmesh sector (dense sectors are decimated evenly, not truncated)
};

/// \brief Project settings for AI squads (see plAiSquadWorldModule).
///
/// Defines what a squad IS; live-tuning knobs (maneuver thresholds, budgets) are cvars.
struct PL_AIPLUGIN_DLL plAiSquadConfig
{
  plUInt8 m_uiAttackTokens = 2;      ///< how many squad members may actively fire at the same time
  float m_fShareConfidence = 0.6f;   ///< a member's target confidence needed before its target is shared squad-wide
  float m_fShareStrength = 0.6f;     ///< confidence squadmates receive from a share (< 1, so shared knowledge is weaker than first-hand sight)
  float m_fShareInterval = 0.25f;    ///< minimum seconds between target shares per squad
};

struct PL_AIPLUGIN_DLL plAiNavigationConfig
{
  plAiNavigationConfig();

  struct GroundType
  {
    bool m_bUsed = false;
    plString m_sName;
    plColorGammaUB m_Color;
  };

  GroundType m_GroundTypes[plAiNumGroundTypes];

  plDynamicArray<plAiPathSearchConfig> m_PathSearchConfigs;
  plDynamicArray<plAiNavmeshConfig> m_NavmeshConfigs;
  plAiTacticalConfig m_TacticalConfig;
  plAiSquadConfig m_SquadConfig;

  static constexpr const plStringView s_sConfigFile = ":project/RuntimeConfigs/AiPluginConfig.cfg"_plsv;

  plResult Save(plStringView sFile = s_sConfigFile) const;
  plResult Load(plStringView sFile = s_sConfigFile);

  void Save(plStreamWriter& inout_stream) const;
  void Load(plStreamReader& inout_stream);
};
