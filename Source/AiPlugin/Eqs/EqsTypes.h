#pragma once

#include <AiPlugin/AiPluginDLL.h>
#include <AiPlugin/Tactical/SmartObjects.h>
#include <AiPlugin/Tactical/TacticalTypes.h>
#include <Core/World/Declarations.h>
#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/Vec3.h>
#include <Foundation/Reflection/Reflection.h>
#include <Foundation/Strings/HashedString.h>
#include <Foundation/Threading/AtomicInteger.h>

class dtNavMeshQuery;
class dtQueryFilter;
class plAiTacticalWorldModule;
class plPhysicsWorldModuleInterface;

/// \brief What kind of payload an EQS item carries besides its position.
struct PL_AIPLUGIN_DLL plAiEqsPayloadType
{
  using StorageType = plUInt8;

  enum Enum
  {
    None,            ///< plain position
    CoverPoint,      ///< baked cover point (m_hCover valid)
    SmartObjectSlot, ///< free smart object slot (m_hSmartObject valid)
    GameObject,      ///< a game object (m_hObject valid)

    Default = None
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEqsPayloadType);

/// \brief How the query picks its result from the scored candidates.
struct PL_AIPLUGIN_DLL plAiEqsRunMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    SingleBest,         ///< the highest scoring candidate wins
    RandomOfTopPercent, ///< a random candidate from the top X percent wins (spreads agents out)
    AllMatching,        ///< all surviving candidates, best first (up to MaxResults)

    Default = SingleBest
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEqsRunMode);

/// \brief Whether a test discards items, scores them, or both.
struct PL_AIPLUGIN_DLL plAiEqsTestPurpose
{
  using StorageType = plUInt8;

  enum Enum
  {
    FilterOnly,     ///< apply the filter condition; no score contribution
    ScoreOnly,      ///< contributes weight * curve(raw) to the item's score
    FilterAndScore, ///< apply the filter condition, then score survivors

    Default = FilterAndScore
  };

  static bool Filters(Enum e) { return e != ScoreOnly; }
  static bool Scores(Enum e) { return e != FilterOnly; }
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEqsTestPurpose);

/// \brief How a test combines its per-position results against a multi-position context.
struct PL_AIPLUGIN_DLL plAiEqsContextCombine
{
  using StorageType = plUInt8;

  enum Enum
  {
    Min,     ///< worst case ("hidden from ALL enemies")
    Max,     ///< best case ("visible to ANY enemy")
    Average, ///< mean over all positions

    Default = Min
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEqsContextCombine);

struct PL_AIPLUGIN_DLL plAiEqsFilterCondition
{
  using StorageType = plUInt8;
  enum Enum
  {
    LegacyPositiveScore,
    IsTrue,
    AtLeast,
    AtMost,
    Between,
    Reachable,
    DirectPath,
    Default = LegacyPositiveScore
  };
};
PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEqsFilterCondition);

struct PL_AIPLUGIN_DLL plAiEqsMissingDataPolicy
{
  using StorageType = plUInt8;
  enum Enum
  {
    Legacy,
    RejectCandidate,
    SkipTest,
    FailQuery,
    Default = RejectCandidate
  };
};
PL_DECLARE_REFLECTABLE_TYPE(PL_AIPLUGIN_DLL, plAiEqsMissingDataPolicy);

enum class plAiEqsTestData : plUInt8
{
  Valid,
  MissingContext,
  MissingPhysics,
  MissingNavigation,
  MissingPayload,
  StaleHandle,
  InvalidMeasurement
};

struct PL_AIPLUGIN_DLL plAiEqsTestTrace
{
  float m_fMeasurement = 0;
  float m_fRaw = 0;
  float m_fCurved = 0;
  float m_fContribution = 0;
  plAiEqsTestData m_Data = plAiEqsTestData::Valid;
  bool m_bEvaluated = false;
  bool m_bPassed = true;
  bool m_bSkipped = false;
};

PL_AIPLUGIN_DLL const char* plAiEqsTestDataName(plAiEqsTestData data);

/// \brief One candidate of an EQS query: a position plus optional payload and scoring state.
struct PL_AIPLUGIN_DLL plAiEqsItem
{
  /// Per-test curved scores are recorded for the first MaxRecordedTests tests (debug breakdown).
  static constexpr plUInt32 MaxRecordedTests = 12;

  plVec3 m_vPosition = plVec3::MakeZero();

  plAiCoverPointHandle m_hCover;
  plAiSmartObjectHandle m_hSmartObject;
  plGameObjectHandle m_hObject;
  plEnum<plAiEqsPayloadType> m_Payload;

  float m_fRaw = 0.0f; ///< scratch: the current test's raw [0,1] result for this item
  float m_fMeasurement = 0.0f; ///< native units (meters, degrees, dot product or boolean)
  bool m_bHasMeasurement = false;
  bool m_bReachable = false;
  bool m_bDirectPath = false;
  plAiEqsTestData m_TestData = plAiEqsTestData::Valid;
  plUInt16 m_uiRejectedBy = 0xFFFF;
  float m_fScoreSum = 0.0f;
  float m_fWeightSum = 0.0f;
  float m_fFinal = 0.0f;
  bool m_bDiscarded = false;

  float m_TestScores[MaxRecordedTests] = {}; ///< curved per-test scores (score breakdown debugging)
  plAiEqsTestTrace m_TestTrace[MaxRecordedTests];
};

/// \brief One resolved context slot: world positions / objects snapshotted at submit time.
struct PL_AIPLUGIN_DLL plAiEqsResolvedSlot
{
  plHashedString m_sName;
  plHybridArray<plVec3, 2> m_Positions;
  plHybridArray<plGameObjectHandle, 2> m_Objects;
};

/// \brief Per-submit runtime parameters of an EQS query.
struct PL_AIPLUGIN_DLL plAiEqsQueryParams
{
  /// The asking agent: source of the querier position, its blackboard (context resolution) and
  /// the claimant identity for Unclaimed tests. Optional if m_bHasQuerierPosition is set.
  plGameObjectHandle m_hQuerier;

  /// Explicit querier position; used when set (or when no querier object is given).
  plVec3 m_vQuerierPosition = plVec3::MakeZero();
  bool m_bHasQuerierPosition = false;

  struct NamedPosition
  {
    plHashedString m_sName;
    plVec3 m_vPosition = plVec3::MakeZero();
  };

  /// Positions consumed by plAiEqsContext_ExplicitPosition slots (matched by slot name).
  plHybridArray<NamedPosition, 2> m_Positions;

  /// When non-zero, overrides the asset's MaxResults.
  plUInt8 m_uiMaxResultsOverride = 0;
};

/// \brief Result of an EQS query. Poll via plAiEqsWorldModule::TryGetResult.
struct PL_AIPLUGIN_DLL plAiEqsQueryResult
{
  enum class Status : plUInt8
  {
    Invalid,      ///< no such query
    Pending,      ///< still executing (possibly suspended on budget), poll again next frame
    Ready,        ///< m_TopN holds at least one candidate, best first
    NoResult,     ///< executed, but no candidate survived the filters
    AreaNotReady, ///< navmesh sectors not loaded yet (they have been requested; retry shortly)
    MissingData,  ///< a test's FailQuery policy encountered unavailable data
  };

  Status m_Status = Status::Invalid;
  plUInt32 m_uiFrameStamp = 0;

  struct Candidate
  {
    plVec3 m_vPosition = plVec3::MakeZero();
    float m_fScore = 0.0f;
    plAiCoverPointHandle m_hCover;
    plAiSmartObjectHandle m_hSmartObject;
    plGameObjectHandle m_hObject;
    plEnum<plAiEqsPayloadType> m_Payload;

    plUInt8 m_uiRecordedTests = 0;
    float m_TestScores[plAiEqsItem::MaxRecordedTests] = {}; ///< curved per-test scores (score breakdown)
    plAiEqsTestTrace m_TestTrace[plAiEqsItem::MaxRecordedTests];
  };

  plHybridArray<Candidate, 8> m_TopN; ///< [0] = best
};

class plAiEqsQueryDesc;

/// \brief Everything a generator or test may touch while executing on a worker thread.
///
/// All world state was snapshotted at submit time (contexts, pre-collected registry candidates);
/// the only live accesses allowed are the per-thread navmesh scratch query, the thread-safe
/// physics raycast interface and the tactical module's read-only registries (stable during the
/// EQS module's own PostAsync parallel section).
struct PL_AIPLUGIN_DLL plAiEqsEvalContext
{
  const plAiEqsQueryDesc* m_pDesc = nullptr;

  plVec3 m_vQuerier = plVec3::MakeZero();
  plGameObjectHandle m_hQuerier; ///< also the claimant for Unclaimed tests

  plArrayPtr<const plAiEqsResolvedSlot> m_ResolvedSlots;

  /// Registry candidates pre-collected on the main thread at submit (smart object generators).
  plArrayPtr<const plAiEqsItem> m_PreCollectedItems;

  dtNavMeshQuery* m_pNavQuery = nullptr;
  const dtQueryFilter* m_pFilter = nullptr;
  const plPhysicsWorldModuleInterface* m_pPhysics = nullptr;
  const plAiTacticalWorldModule* m_pTactical = nullptr;
  plUInt8 m_uiCollisionLayer = 0;

  plUInt32 m_uiRngSeed = 0;
  plUInt32 m_uiMaxCandidates = 32;

  plInt32 m_iRaycastBudget = 0;                     ///< per query per frame; 0 = exhausted
  plAtomicInteger32* m_pPathQueryBudget = nullptr;  ///< shared across all queries this frame
  plAtomicInteger32* m_pStatRaycasts = nullptr;

  const plAiEqsResolvedSlot* FindSlot(const plTempHashedString& sName) const
  {
    for (const auto& slot : m_ResolvedSlots)
    {
      if (slot.m_sName == sName)
        return &slot;
    }

    return nullptr;
  }

  bool TryConsumeRaycast()
  {
    if (m_iRaycastBudget <= 0)
      return false;

    --m_iRaycastBudget;

    if (m_pStatRaycasts != nullptr)
    {
      m_pStatRaycasts->Increment();
    }

    return true;
  }

  bool TryConsumePathQuery()
  {
    return m_pPathQueryBudget != nullptr && m_pPathQueryBudget->Decrement() >= 0;
  }
};

/// \brief Normalized trapezoid band score: 1 inside [fFullMin, fFullMax], linear falloff outside.
PL_AIPLUGIN_DLL float plAiEqsTrapezoidScore(float fValue, float fFullMin, float fFullMax);
