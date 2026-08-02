#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <Core/World/Declarations.h>
#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/HashedString.h>

/// \brief How well a cover point protects an agent.
struct PL_AIPLUGIN_DLL plAiCoverQuality
{
  using StorageType = plUInt8;

  enum Enum
  {
    None, ///< no protection (never stored in the registry; used as a 'no minimum' filter value)
    Low,  ///< blocks fire while crouching (crates, low walls)
    High, ///< blocks fire while standing (walls, pillars)

    Default = None
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiCoverQuality);

/// \brief Default XY / vertical half-extents for projecting positions onto the navmesh
/// (dtNavMeshQuery::findNearestPoly; Recast is Y-up, so the vertical value is the middle extent).
constexpr float plAiNavProjectionExtentXY = 2.0f;
constexpr float plAiNavProjectionExtentZ = 3.0f;

/// \brief One baked cover position on the navmesh.
struct plAiCoverPoint
{
  PL_DECLARE_POD_TYPE();

  plVec3 m_vPosition = plVec3::MakeZero(); ///< on-navmesh standing position (engine space)
  plVec3 m_vWallDir = plVec3::MakeZero();  ///< unit XY direction from the point toward the blocking geometry
  plEnum<plAiCoverQuality> m_Quality;
  plUInt16 m_uiClaimIndex = 0xFFFF;        ///< index into the tactical module's claim table, 0xFFFF = unclaimed
  plUInt8 m_uiWallTopHeight = 0;           ///< measured wall top above m_vPosition in 0.1m units, capped at 2.25m; 0 = unknown (authored/legacy)
  plUInt16 m_uiSurface = 0xFFFF;           ///< index of the owning cover surface in the sector's surface array; 0xFFFF = standalone point
};

/// \brief A contiguous strip of cover points along one wall contour (a CryEngine-style cover
/// surface).
///
/// The points of a surface are stored consecutively in their sector's point array, ordered along
/// the wall (including around corners) - agents can move between them while staying in cover.
/// Claims stay per-point: a long surface holds several agents at different points.
struct plAiCoverSurface
{
  PL_DECLARE_POD_TYPE();

  plUInt16 m_uiFirstPoint = 0; ///< index of the surface's first point in the sector's point array
  plUInt16 m_uiNumPoints = 0;  ///< number of consecutive points (>= 1)
};

/// \brief Identifies a cover point in the registry. Only valid until the point's sector re-bakes.
struct plAiCoverPointHandle
{
  PL_DECLARE_POD_TYPE();

  plUInt32 m_uiSectorEntry = plInvalidIndex; ///< index into the cover sector registry
  plUInt16 m_uiPoint = 0;                    ///< index into the sector's point array
  plUInt16 m_uiBakeGeneration = 0;           ///< must match the sector entry's generation, else stale

  bool IsValid() const { return m_uiSectorEntry != plInvalidIndex; }
  void Invalidate() { m_uiSectorEntry = plInvalidIndex; }

  bool operator==(const plAiCoverPointHandle& rhs) const
  {
    return m_uiSectorEntry == rhs.m_uiSectorEntry && m_uiPoint == rhs.m_uiPoint && m_uiBakeGeneration == rhs.m_uiBakeGeneration;
  }
};

/// \brief One scoring/filter step of a tactical query. POD, trivially copyable into the query job.
struct plAiTacticalScorerDesc
{
  PL_DECLARE_POD_TYPE();

  enum class Type : plUInt8
  {
    LosToThreat,           ///< physics ray threat-eye -> candidate-eye; param0: 0 = prefer BLOCKED (cover), 1 = prefer VISIBLE (flank/attack)
    DistanceBandToQuerier, ///< trapezoid: full score inside [param0, param1], falls off outside
    DistanceBandToThreat,  ///< trapezoid on distance to the threat
    DirectionFromThreat,   ///< alignment of (candidate - threat) with the desired flank angle; param0 = desired angle in radians relative to threat->querier
    CoverQualityMin,       ///< hard filter: candidate must be a cover point with quality >= param0
    UnclaimedOnly,         ///< hard filter: cover point must be unclaimed (or claimed by the querier itself)
    CoverFacingThreat,     ///< cover point's wall must face the threat (the wall is between us and them); param0 = min dot of wallDir vs to-threat direction (~0.2). Non-cover candidates pass.
    ReachableApprox        ///< navmesh raycast from querier toward candidate; penalizes unreachable candidates
  };

  Type m_Type = Type::DistanceBandToQuerier;
  bool m_bHardFilter = false; ///< when set, a zero score discards the candidate instead of just scoring it
  float m_fParam0 = 0.0f;
  float m_fParam1 = 0.0f;
  float m_fWeight = 1.0f;
};

/// \brief Ready-made query configurations for the common tactical questions.
struct PL_AIPLUGIN_DLL plAiTacticalQueryPreset
{
  using StorageType = plUInt8;

  enum Enum
  {
    TakeCoverFromTarget, ///< nearest unclaimed cover point that breaks the threat's line of sight AND faces them (fight from cover)
    FlankTarget,         ///< position on a ring around the threat, off to its side, with line of sight
    RetreatFromTarget,   ///< position away from the threat, preferably out of its line of sight
    RandomNearSelf,      ///< random reachable navmesh position around the querier
    HideFromTarget,      ///< nearest unclaimed cover point out of the threat's line of sight - facing does NOT matter (hide, not fight)

    Default = TakeCoverFromTarget
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiTacticalQueryPreset);

/// \brief Full description of a tactical position query.
struct PL_AIPLUGIN_DLL plAiTacticalQueryDesc
{
  enum class Generator : plUInt8
  {
    RingAroundPosition, ///< ring of navmesh-projected points in [radiusMin, radiusMax] around m_vCenter
    CoverPointsNear,    ///< baked cover points within m_fRadiusMax of m_vCenter
    NavmeshRandom       ///< random reachable points around m_vCenter
  };

  Generator m_Generator = Generator::CoverPointsNear;

  plVec3 m_vCenter = plVec3::MakeZero();  ///< generator center
  plVec3 m_vQuerier = plVec3::MakeZero(); ///< the asking agent's position
  plVec3 m_vThreat = plVec3::MakeZero();  ///< the threat position (target)
  bool m_bHasThreat = false;

  float m_fRadiusMin = 2.0f;
  float m_fRadiusMax = 12.0f;
  plUInt8 m_uiCandidates = 24; ///< generated candidates, clamped to 32
  plUInt8 m_uiMaxResults = 8;  ///< top-N results kept, clamped to 16

  float m_fQuerierEyeHeight = 1.55f;
  float m_fThreatEyeHeight = 1.55f;

  /// Cover candidates further than this above/below m_vCenter are skipped (multi-floor layouts:
  /// keeps wrong-floor points from crowding the candidate set). 0 = no height filter.
  float m_fMaxCoverZDelta = 2.5f;

  plGameObjectHandle m_hClaimant; ///< this object's own claim is ignored by UnclaimedOnly

  plHashedString m_sNavmeshConfig;    ///< empty = first configured navmesh
  plHashedString m_sPathSearchConfig; ///< empty = default filter

  plHybridArray<plAiTacticalScorerDesc, 8> m_Scorers;

  /// \brief Fills generator + scorers for the given preset. Positions/handles must be set by the caller.
  static plAiTacticalQueryDesc MakeFromPreset(plAiTacticalQueryPreset::Enum preset, const plVec3& vQuerier, const plVec3& vThreat, bool bHasThreat, float fRadiusMin, float fRadiusMax, plAngle flankAngle = plAngle::MakeFromDegree(100), plAiCoverQuality::Enum minQuality = plAiCoverQuality::Low);
};

/// \brief Result of a tactical query. Poll via plAiTacticalWorldModule::TryGetResult.
struct plAiTacticalQueryResult
{
  enum class Status : plUInt8
  {
    Invalid,      ///< no such query
    Pending,      ///< still executing, poll again next frame
    Ready,        ///< m_TopN holds at least one candidate, best first
    NoResult,     ///< executed, but no candidate survived the filters
    AreaNotReady, ///< navmesh sectors not loaded yet (they have been requested; retry shortly)
  };

  Status m_Status = Status::Invalid;
  plUInt32 m_uiFrameStamp = 0;

  struct Candidate
  {
    PL_DECLARE_POD_TYPE();

    plVec3 m_vPosition = plVec3::MakeZero();
    float m_fScore = 0.0f;
    plAiCoverPointHandle m_CoverHandle; ///< valid when the candidate is a baked cover point
  };

  plHybridArray<Candidate, 8> m_TopN; ///< [0] = best
};
